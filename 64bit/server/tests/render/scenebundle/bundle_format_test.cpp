// bundle_format_test.cpp - the tests of the scene bundle library (srv_scene_bundle.{h,cpp}; epic 0301, task 3902).
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// Every byte here is synthetic, made by the test (D83: nothing from the game). Positives: a round trip of every member
// kind of contract 2.1-2.9 through the Writer, CheckBundle and the parsers; the manifest written and read back; C5HF
// through c5h::readTap; the asset-set port (PYCHECK lines, compared with visualcheck.py by the runner). Negatives
// (contract 10): each bundle is checked by a child process of this program (`--check DIR`), asserting its exit code
// (RefusalExitCode), its output line, and that the bundle's files are unchanged (nothing written); the same check runs
// in process for the refusal code.
//
//   bundle_format_test.exe --work DIR        the suite (writes only below DIR); last line RESULT: ...
//   bundle_format_test.exe --check DIR       CheckBundle on DIR: prints the result line, exits RefusalExitCode
#include "srv_scene_bundle.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include <limits>   // issue 3911
#include <cmath>    // issue 0440 (std::nan)
#include "decal_image_fixture.h"   // issue 0364
#include "shell_model_tap.h"       // ticket 4875: the model tap member's frame image
#include <stddef.h>
#include "srv_scene_sources.h"      // issue 4457: the file and recording feeds' unit calls
#include "c5r/fow/fow.h"            // ticket 0403: a real particle state for particles.bin
#include <cmath>                   // issue 4457

using namespace srvrender::scenebundle;
using srvrender::LoadModelTapMembers;   // ticket 4875: the player's loader (srv_scene_sources.h)
using srvrender::ModelTapMembers;
using srvrender::ApplyModelTapFrame;   // ticket 4888: the geometry member handed over before the tap
namespace hf = c5r::heightfield;

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...)                                                                                                  \
	do                                                                                                                    \
	{                                                                                                                     \
		if(cond)                                                                                                          \
			++g_pass;                                                                                                     \
		else                                                                                                              \
		{                                                                                                                 \
			++g_fail;                                                                                                     \
			printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                                        \
			printf(__VA_ARGS__);                                                                                          \
			printf("\n");                                                                                                 \
			fflush(stdout);                                                                                               \
		}                                                                                                                 \
	} while(0)

// ---- files ---------------------------------------------------------------------------------------------------------------
static std::wstring g_work;

static std::wstring W(const std::string& s)
{
	return Widen(s);
}

static bool WriteBytes(const std::wstring& path, const void* p, size_t n)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"wb") || !f)
		return false;
	bool ok = fwrite(p, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

static bool ReadBytes(const std::wstring& path, std::vector<uint8_t>* out)
{
	out->clear();
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"rb") || !f)
		return false;
	uint8_t buf[65536];
	size_t k;
	while((k = fread(buf, 1, sizeof(buf), f)) > 0)
		out->insert(out->end(), buf, buf + k);
	fclose(f);
	return true;
}

static void RemoveTree(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"/*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
				continue;
			std::wstring p = dir + L"/" + fd.cFileName;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveTree(p);
			else
				DeleteFileW(p.c_str());
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

static void MkDir(const std::wstring& d)
{
	CreateDirectoryW(d.c_str(), NULL);
}

// path -> size and SHA-256 of every file below dir: "nothing was written" compares two of these
typedef std::map<std::string, std::string> Snap;
static void SnapInto(const std::wstring& dir, const std::string& rel, Snap* s)
{
	WIN32_FIND_DATAW fd;
	std::wstring base = rel.empty() ? dir : dir + L"/" + W(rel);
	HANDLE h = FindFirstFileW((base + L"/*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
			continue;
		std::string name = (rel.empty() ? std::string() : rel + "/") + Narrow(fd.cFileName);
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			(*s)[name + "/"] = "dir";
			SnapInto(dir, name, s);
		}
		else
		{
			std::string hex;
			uint64_t n = 0;
			Sha256FileHex(dir + L"/" + W(name), &hex, &n);
			char b[32];
			sprintf_s(b, "%llu ", (unsigned long long)n);
			(*s)[name] = b + hex;
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}
static Snap SnapDir(const std::wstring& dir)
{
	Snap s;
	SnapInto(dir, "", &s);
	return s;
}

static bool ReplaceInFile(const std::wstring& path, const std::string& from, const std::string& to)
{
	std::vector<uint8_t> b;
	if(!ReadBytes(path, &b))
		return false;
	std::string s(b.begin(), b.end());
	size_t at = s.find(from);
	if(at == std::string::npos)
		return false;
	s.replace(at, from.size(), to);
	return WriteBytes(path, s.data(), s.size());
}

// Runs this program with --check DIR; output and exit code.
static bool RunCheckChild(const std::wstring& dir, int* code, std::string* out)
{
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --check \"" + dir + L"\"";
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
	HANDLE rd = NULL, wr = NULL;
	if(!CreatePipe(&rd, &wr, &sa, 0))
		return false;
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = wr;
	si.hStdError = wr;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> line(cmd.begin(), cmd.end());
	line.push_back(0);
	BOOL ok = CreateProcessW(NULL, line.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(wr);
	if(!ok)
	{
		CloseHandle(rd);
		return false;
	}
	out->clear();
	char buf[4096];
	DWORD got;
	while(ReadFile(rd, buf, sizeof(buf), &got, NULL) && got)
		out->append(buf, got);
	CloseHandle(rd);
	WaitForSingleObject(pi.hProcess, 60000);
	DWORD ec = 0;
	GetExitCodeProcess(pi.hProcess, &ec);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	*code = (int)ec;
	return true;
}

// ---- synthetic members -----------------------------------------------------------------------------------------------------
static std::vector<uint8_t> MakeHeightfield(bool withMat)
{
	hf::HeightGrid g;
	g.mapW = 2;
	g.mapH = 3;
	g.samplesX = 128;
	g.samplesY = 192;
	g.wrapX = true;
	g.ox = 64;
	g.oy = 83.1f;
	g.waterZ = 0.7f;
	g.mapSeed = 12345;
	g.hexToWorld[0] = 1;
	g.hexToWorld[5] = 1;
	g.h.resize((size_t)g.samplesX * g.samplesY);
	for(size_t k = 0; k < g.h.size(); ++k)
		g.h[k] = (uint16_t)(k * 2654435761u >> 16);
	if(withMat)
	{
		g.mat.resize(g.h.size());
		for(size_t k = 0; k < g.mat.size(); ++k)
			g.mat[k] = (uint8_t)(k % 7);
	}
	std::vector<uint8_t> out;
	hf::writeTap(g, out);
	return out;
}

struct WaterParts
{
	VPShellWaterTap block;
	std::vector<uint8_t> image;
};

static WaterParts MakeWaterParts()
{
	WaterParts w;
	memset(&w.block, 0, sizeof(w.block));
	VPShellWaterImage img = {};
	img.magic = VPSHELL_WATER_MAGIC;
	img.version = VPSHELL_WATER_IMAGE_VERSION;
	img.headerBytes = sizeof(VPShellWaterImage);
	img.recordBytes = sizeof(VPShellWaterRecord);
	img.recordCount = 1;
	img.loadSeq = 3;
	img.exePid = 4242;
	img.mapW = 2;
	img.mapH = 3;
	img.waterZ = 1.5f;
	img.imageBytes = 256 + 64;
	VPShellWaterRecord rec = {};
	rec.role = VPWROLE_TEXTURE;
	rec.dataOffset = 256;
	rec.dataBytes = 64;
	rec.stride = 16;
	rec.count = 4;
	rec.height = 1;
	w.image.assign((size_t)img.imageBytes, 0);
	memcpy(w.image.data(), &img, sizeof(img));
	memcpy(w.image.data() + sizeof(img), &rec, sizeof(rec));
	for(size_t k = 256; k < w.image.size(); ++k)
		w.image[k] = (uint8_t)(k * 31);
	w.block.state = 2;
	w.block.loadSeq = 3;
	w.block.exePid = 4242;
	w.block.copySeq = 1;
	w.block.recordCount = 1;
	w.block.imageBytes = img.imageBytes;
	w.block.sectionBytes = 65536;
	w.block.fnv1a64 = 0x1234567890abcdefull;
	w.block.copyMs = 0.25f;
	wcscpy_s(w.block.sectionName, L"Local\\vp_test_water_1");
	return w;
}

static std::vector<uint8_t> MakeWaterTap(uint32_t copySeq = 1)
{
	WaterParts w = MakeWaterParts();
	w.block.copySeq = copySeq;   // ticket 0622: a later copy of the same load (a Waves_Reload)
	std::vector<uint8_t> out;
	EncodeWaterTap(w.block, 1, 3, 128, 192, w.image.data(), w.image.size(), &out);
	return out;
}

// ticket 0622 (format version 11): a water_frames.bin record naming a .wtap member's bytes (load 3 as MakeWaterParts)
static WaterFrameRecord WaterRec(int64_t frameId, const std::vector<uint8_t>* wtap, uint32_t copySeq = 1)
{
	WaterFrameRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = frameId;
	if(wtap)
	{
		r.flags = kWaterCopyInForce;
		r.loadSeq = 3;
		r.copySeq = copySeq;
		const std::string h = Sha256Hex(wtap->data(), wtap->size());
		for(int i = 0; i < 32 && h.size() == 64; ++i)
			r.wtapSha256[i] = (uint8_t)strtoul(h.substr(2 * i, 2).c_str(), nullptr, 16);
	}
	return r;
}

static std::vector<uint8_t> WaterFramesBytes(const std::vector<WaterFrameRecord>& recs)
{
	std::vector<uint8_t> b;
	EncodeStreamHead(MemberKind::WaterFrames, &b);
	const uint8_t* p = (const uint8_t*)recs.data();
	b.insert(b.end(), p, p + recs.size() * sizeof(WaterFrameRecord));
	return b;
}

static const std::vector<uint8_t>& OverlayTexels()
{
	static std::vector<uint8_t> t;
	if(t.empty())
	{
		t.resize((size_t)kOverlayTexelBytes);
		uint32_t x = 0x9E3779B9u;
		for(size_t k = 0; k < t.size(); ++k)
		{
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			t[k] = (uint8_t)x;
		}
	}
	return t;
}

static std::vector<uint8_t> MakeOverlay()
{
	std::vector<uint8_t> out;
	EncodeOverlay(OverlayTexels().data(), OverlayTexels().size(), &out);
	return out;
}

static std::vector<SrvFeed::PlotRecord> MakeTable(uint32_t w, uint32_t h, int salt)
{
	std::vector<SrvFeed::PlotRecord> t((size_t)w * h);
	for(size_t k = 0; k < t.size(); ++k)
	{
		memset(&t[k], 0, sizeof(t[k]));
		t[k].terrain = (int8_t)(k % 7);
		t[k].plotType = (int8_t)(k % 4);
		t[k].feature = (int8_t)(-1 + (int)(k % 3));
		t[k].resource = (int16_t)(k + salt);
		t[k].improvement = -1;
		t[k].owner = (int8_t)(k % 2);
		t[k].changeGen = 1;
		t[k].improvementState = -1;
		t[k].routeState = -1;
	}
	return t;
}

static SrvFeed::Batch MakeBatch()
{
	SrvFeed::Batch b;
	b.snapshotGen = 1;
	b.width = 4;
	b.height = 3;
	b.dropped = 0;
	for(uint32_t k = 0; k < 3; ++k)
	{
		SrvFeed::Event e = {};
		e.seq = 10 + k;
		e.kind = k == 2 ? (uint16_t)SrvFeed::kUiMirror : (uint16_t)SrvFeed::kPlotState;
		e.plot = (int32_t)k;
		e.turn = 9;
		e.a[0] = (int32_t)(k * 3);
		e.a[3] = -7;
		b.events.push_back(e);
	}
	b.changedPlots = {1, 5};
	return b;
}

// PLOTS full (baseline), DRAIN, PLOTS diff (two plots changed), DRAIN with nothing; returns the expected tables too
static std::vector<uint8_t> MakeFeed(std::vector<std::vector<SrvFeed::PlotRecord>>* tables)
{
	std::vector<uint8_t> out, rec;
	EncodeStreamHead(MemberKind::Feed, &out);
	FeedEncoder enc;
	std::vector<SrvFeed::PlotRecord> t0 = MakeTable(4, 3, 0);
	enc.addPlots(5, 1, 1, 4, 3, t0, &rec);
	out.insert(out.end(), rec.begin(), rec.end());
	enc.addDrain(6, 100, MakeBatch(), &rec);
	out.insert(out.end(), rec.begin(), rec.end());
	std::vector<SrvFeed::PlotRecord> t1 = t0;
	t1[1].improvement = 3;
	t1[5].fow = 3;
	enc.addPlots(6, 2, 1, 4, 3, t1, &rec);
	out.insert(out.end(), rec.begin(), rec.end());
	SrvFeed::Batch empty;
	empty.snapshotGen = 1;
	empty.width = 4;
	empty.height = 3;
	enc.addDrain(7, -1, empty, &rec);
	out.insert(out.end(), rec.begin(), rec.end());
	if(tables)
		*tables = {t0, t1};
	return out;
}

static VPShellCamera MakeCamera(int k)
{
	VPShellCamera c;
	memset(&c, 0, sizeof(c));
	c.status = 1;
	c.presentCount = 1000 + k;
	c.frameStamp = 100 + k;
	c.numViews = 1;
	for(int i = 0; i < 16; ++i)
	{
		c.proj[i] = (float)(i + k);
		c.view[0][i] = (float)(i * 2 - k);
		c.camWorld[i] = (float)(i % 5);
	}
	c.vpW = 8;
	c.vpH = 4;
	c.bbW = 8;
	c.bbH = 4;
	c.gridSeq = 3;
	c.wrapPeriod = 127;
	c.sx = 1;
	c.sy = 0.866f;
	c.samplesX = 128;
	c.samplesY = 192;
	return c;
}

static std::vector<uint8_t> MakeCameras(int n)
{
	std::vector<uint8_t> out;
	EncodeStreamHead(MemberKind::CameraBlocks, &out);
	for(int k = 0; k < n; ++k)
	{
		VPShellCamera c = MakeCamera(k);
		const uint8_t* p = (const uint8_t*)&c;
		out.insert(out.end(), p, p + sizeof(c));
	}
	return out;
}

static FrameRecord MakeFrame(int k, const std::string& overlaySha)
{
	FrameRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = 100 + k;
	r.loopIter = 6 + (uint64_t)k;
	r.nowMs = 5000.0 + 16.6 * k;
	r.width = 8;
	r.height = 4;
	r.flags = (k == 0 || k == 2) ? (kFrameDumped | kFrameWaterModule) : kFrameWaterModule;
	if(k == 1)
		r.flags |= kFrameFog;   // format version 2 (issue 3911)
	if(k == 1 && !overlaySha.empty())
	{
		r.overlayBound = 1;
		r.overlayGeneration = 7;
		r.overlayPresentCount = 1001;
		r.overlayNumViews = 1;
		for(int i = 0; i < 16; ++i)
			r.overlayProj[0][i] = (float)i * 0.5f;
		for(int i = 0; i < 32; ++i)
		{
			auto hx = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
			r.overlaySha256[i] = (uint8_t)(hx(overlaySha[2 * i]) * 16 + hx(overlaySha[2 * i + 1]));
		}
	}
	return r;
}

static WaterClockFile MakeClock()
{
	WaterClockFile c;
	c.clockStartMs = 1000.5;
	c.waveLastMs = 4990.25;
	c.lastNowMs = 4990.25;
	for(int k = 0; k < 5; ++k)
	{
		c5r::water::WaveRecord w;
		w.u = 0.125f * (float)k;
		w.v = 0.25f;
		w.age = k == 3 ? -0.5f : 0.1f * (float)k;
		w.unused = 0;
		c.waves.push_back(w);
	}
	return c;
}

static FogClockFile MakeFogClock()   // issue 3911
{
	FogClockFile c;
	c.clockS = 41.765625;
	c.clockLastMs = 123456.789;
	return c;
}

static std::vector<uint8_t> MakeBmp()
{
	std::vector<uint8_t> px(8 * 4 * 4), out;
	for(size_t k = 0; k < px.size(); ++k)
		px[k] = (uint8_t)(k * 7);
	EncodeDumpColour(8, 4, px.data(), &out);
	return out;
}

static std::vector<uint8_t> MakeDepth()
{
	std::vector<float> d(8 * 4);
	for(size_t k = 0; k < d.size(); ++k)
		d[k] = 0.01f * (float)k;
	return std::vector<uint8_t>((const uint8_t*)d.data(), (const uint8_t*)(d.data() + d.size()));
}

static std::vector<uint8_t> MakePng()
{
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	std::vector<uint8_t> b(sig, sig + 8);
	for(int k = 0; k < 40; ++k)
		b.push_back((uint8_t)k);
	return b;
}

static std::vector<uint8_t> Str(const char* s)
{
	return std::vector<uint8_t>(s, s + strlen(s));
}

static Manifest BaseManifest()
{
	Manifest m;
	m.bundleId = "SYNTH-3902_20261115-000000_test";
	m.created = "2026-11-15T00:00:00-05:00";
	m.recorderKeys["render_test_record"] = "C:/c5-64bit-work/0301/3902/x";
	m.recorderKeys["render_test_record_frames"] = "3";
	m.serverExeSha256 = std::string(64, 'a');
	m.repoCommit = "6044aaf7b29290a866e8e7eb4469cece0f729ca8";
	m.buildConfig = "release";
	m.pins["i0_lock.commit"] = "26234371e";
	m.pins["i0_lock.build"] = "i0ship-26234371-ia";
	m.pins["dll_lock.dll_sha256"] = std::string(64, 'b');
	m.pins["server_sha256"] = std::string(64, 'c');
	m.saveName = "SYNTH \"quoted\" \\ name";
	m.saveSha256 = std::string(64, 'd');
	m.mapW = 2;
	m.mapH = 3;
	m.turn = 9;
	m.loadSeq = 1;
	m.vt = "absent";
	m.install = "C:/nowhere";
	m.machine = "test";
	m.adapterLuid = "00000000-0000abcd";
	m.adapterDescription = "Synthetic Adapter \xc3\xa9";   // UTF-8 e-acute
	m.adapterDriver = "1.2.3";
	m.renderConfig["render_pass"] = "-1";
	m.renderConfig["render_tiles"] = "bake";
	m.versionNotes.push_back("gap: bake-cache tap left out (render_tiles=bake)");
	m.counters["water_refused"] = 0;
	m.counters["record_errors"] = 2;
	return m;
}

struct Full
{
	bool overlay = true;          // the overlay member and frame 1 bound to it
	// ticket 0622 (format version 11): water_frames.bin naming load 3 copy 1 for frames 100-102 (false: no member); with
	// secondCopy the member water/load0001-c2.wtap (copy 2) too; waterRecs replaces the records
	bool waterFrames = true;
	bool secondCopy = false;
	const std::vector<WaterFrameRecord>* waterRecs = nullptr;
};

// The whole bundle: one member of every kind (contract 2.1-2.9).
static bool BuildFull(const std::wstring& dir, const Full& opt, Manifest* outM)
{
	Writer w;
	bool ok = w.open(dir).ok();
	std::string ovlSha;
	if(opt.overlay)
	{
		std::vector<uint8_t> ovl = MakeOverlay();
		ovlSha = Sha256Hex(ovl.data(), ovl.size());
		ok = ok && w.addMember("overlay/" + ovlSha.substr(0, 16) + ".ovl", MemberKind::OverlayTexture, ovl.data(), ovl.size()).ok();
	}
	std::vector<uint8_t> b = MakeHeightfield(true);
	ok = ok && w.addMember("heightfield/load0001.c5hf", MemberKind::Heightfield, b.data(), b.size()).ok();
	b = MakeWaterTap();
	ok = ok && w.addMember("water/load0001.wtap", MemberKind::WaterTap, b.data(), b.size()).ok();
	if(opt.secondCopy)   // ticket 0622
	{
		const std::vector<uint8_t> c2 = MakeWaterTap(2);
		ok = ok && w.addMember("water/load0001-c2.wtap", MemberKind::WaterTap, c2.data(), c2.size()).ok();
	}
	if(opt.waterFrames)   // ticket 0622 (format version 11)
	{
		std::vector<WaterFrameRecord> recs = {WaterRec(100, &b), WaterRec(101, &b), WaterRec(102, &b)};
		const std::vector<uint8_t> wf = WaterFramesBytes(opt.waterRecs ? *opt.waterRecs : recs);
		ok = ok && w.appendStart("water_frames.bin", MemberKind::WaterFrames).ok();
		ok = ok && w.append("water_frames.bin", wf.data() + 20, wf.size() - 20).ok();
	}
	// feed.bin as a stream, record by record (as the recorder appends it)
	{
		std::vector<uint8_t> feed = MakeFeed(nullptr);
		ok = ok && w.appendStart("feed.bin", MemberKind::Feed).ok();
		ok = ok && w.append("feed.bin", feed.data() + 16, feed.size() - 16).ok();
	}
	ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok();
	ok = ok && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
	for(int k = 0; k < 3; ++k)
	{
		FrameRecord r = MakeFrame(k, ovlSha);
		VPShellCamera c = MakeCamera(k);
		ok = ok && w.append("frames.bin", &r, sizeof(r)).ok();
		ok = ok && w.append("cameras.bin", &c, sizeof(c)).ok();
	}
	EncodeWaterClock(MakeClock(), &b);
	ok = ok && w.addMember("water_clock.bin", MemberKind::WaterClock, b.data(), b.size()).ok();
	EncodeFogClock(MakeFogClock(), &b);   // issue 3911
	ok = ok && w.addMember("fog_clock.bin", MemberKind::FogClock, b.data(), b.size()).ok();
	b = MakeBmp();
	ok = ok && w.addMember("dumps/100.bmp", MemberKind::DumpColour, b.data(), b.size()).ok();
	ok = ok && w.addMember("dumps/102.bmp", MemberKind::DumpColour, b.data(), b.size()).ok();
	b = MakeDepth();
	ok = ok && w.addMember("dumps/100.depth.f32", MemberKind::DumpDepth, b.data(), b.size()).ok();
	ok = ok && w.addMember("dumps/102.depth.f32", MemberKind::DumpDepth, b.data(), b.size()).ok();
	b = Str("{\"name\": \"default\", \"pos\": [1.5, -2e3, 0], \"ok\": true, \"n\": null}");
	ok = ok && w.addMember("cameras/default.json", MemberKind::NamedCamera, b.data(), b.size()).ok();
	b = MakeCameras(1);
	ok = ok && w.addMember("cameras/default.vpcam", MemberKind::NamedCameraBlock, b.data(), b.size()).ok();
	b = MakePng();
	ok = ok && w.addMember("refs/default.png", MemberKind::StockShot, b.data(), b.size()).ok();
	b = Str("synthetic capture bytes");
	ok = ok && w.addMember("refs/default.rdc", MemberKind::StockCapture, b.data(), b.size()).ok();
	b = Str("<xml/>");
	ok = ok && w.addMember("refs/default.export/zip.xml", MemberKind::StockExport, b.data(), b.size()).ok();
	Manifest m = BaseManifest();
	m.firstFrameId = 100;
	m.frameCount = 3;
	m.dumpedFrames = {100, 102};
	c5r::Status st = w.finish(m);
	if(!st.ok())
		printf("BuildFull: %s\n", st.str().c_str());
	if(outM)
		*outM = m;
	return ok && st.ok();
}

// A bundle of one member (frames.count 0).
static bool BuildOne(const std::wstring& dir, const std::string& path, MemberKind kind, const std::vector<uint8_t>& b)
{
	Writer w;
	if(!w.open(dir).ok())
		return false;
	c5r::Status st = w.addMember(path, kind, b.data(), b.size());
	if(!st.ok())
	{
		printf("BuildOne %s: %s\n", path.c_str(), st.str().c_str());
		return false;
	}
	Manifest m = BaseManifest();
	return w.finish(m).ok();
}

static std::wstring Dir(const char* name)
{
	std::wstring d = g_work + L"/" + W(name);
	RemoveTree(d);
	return d;
}

// The negative pattern of contract 10: exit code, output, nothing written; the in-process refusal too.
static void ExpectRefused(const char* name, const std::wstring& dir, Refusal want, const std::string& member)
{
	Snap before = SnapDir(dir);
	int code = -1;
	std::string out;
	bool ran = RunCheckChild(dir, &code, &out);
	Snap after = SnapDir(dir);
	std::string line = std::string("scenebundle: REFUSED ") + RefusalName(want) + " " + (member.empty() ? "scene.json" : member) + ":";
	CHECK(ran, "%s: the check child did not start", name);
	CHECK(code == RefusalExitCode(want), "%s: exit %d, expected %d (%s); output: %s", name, code, RefusalExitCode(want),
	      RefusalName(want), out.c_str());
	CHECK(out.find(line) != std::string::npos, "%s: output lacks \"%s\": %s", name, line.c_str(), out.c_str());
	CHECK(before == after && !before.empty(), "%s: the bundle's files changed during the check (%u -> %u files)", name,
	      (unsigned)before.size(), (unsigned)after.size());
	CheckResult res;
	c5r::Status st = CheckBundle(dir, &res);
	CHECK(!st.ok() && res.refusal == want, "%s: in process %s (%s), expected %s", name, RefusalName(res.refusal),
	      res.why.c_str(), RefusalName(want));
	printf("ok   negative %-36s exit %d  %s\n", name, code, res.line().c_str());
}

// ---- the tests ---------------------------------------------------------------------------------------------------------------
// Format version 2 (epic 0301, issue 3911): fog_clock.bin, feed.bin's PLOTS caller 3, frames.bin's flag bit 3. Each
// negative through the child (exit code, line, nothing written) and in process.
static void TestFogMembers()
{
	CHECK(kFormatVersion == 11 && std::string(MemberKindName(MemberKind::FogClock)) == "fog_clock", "format version 8 (0495), kind fog_clock");
	// the feed: the fog's reads (caller 3, full then a diff) parse and resolve as any other table
	std::vector<uint8_t> feed, rec;
	EncodeStreamHead(MemberKind::Feed, &feed);
	FeedEncoder enc;
	std::vector<SrvFeed::PlotRecord> t0 = MakeTable(4, 3, 0), t1 = t0;
	t1[2].fow = 1;
	t1[7].fow = 3;
	enc.addPlots(0, 3, 1, 4, 3, t0, &rec);
	feed.insert(feed.end(), rec.begin(), rec.end());
	enc.addDrain(4, 100, MakeBatch(), &rec);
	feed.insert(feed.end(), rec.begin(), rec.end());
	enc.addPlots(4, 3, 1, 4, 3, t1, &rec);
	feed.insert(feed.end(), rec.begin(), rec.end());
	std::vector<FeedRecord> recs;
	CheckResult res;
	CHECK(ParseFeed(feed, &recs, &res).ok() && recs.size() == 3 && recs[0].caller == 3 && recs[0].full && recs[2].caller == 3 &&
	          !recs[2].full && recs[2].plots.size() == 12 && memcmp(recs[2].plots.data(), t1.data(), 12 * 20) == 0,
	      "feed with the fog's PLOTS records (caller 3): %s", res.line().c_str());
	{
		std::wstring d = Dir("fog_feed_ok");
		CHECK(BuildOne(d, "feed.bin", MemberKind::Feed, feed), "fog feed not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a feed with caller 3 refused by the checker: %s", r.line().c_str());
	}
	{
		std::vector<uint8_t> bad = feed;
		uint32_t five = 5;   // ticket 0495: caller 4 is the forest's (format version 8), 5 is unknown
		memcpy(&bad[16 + 8 + 8], &five, 4);   // the first record's caller (header 16, type 4, bytes 4, loopIter 8)
		std::wstring d = Dir("neg_fog_caller5");
		BuildOne(d, "feed.bin", MemberKind::Feed, bad);
		ExpectRefused("feed PLOTS caller 5", d, Refusal::BadRecord, "feed.bin");
	}
	{   // ticket 0495: caller 4 (the forest's reads) parses and is accepted by the checker
		std::vector<uint8_t> four = feed;
		uint32_t c4 = 4;
		memcpy(&four[16 + 8 + 8], &c4, 4);
		std::vector<FeedRecord> r4;
		CheckResult r;
		CHECK(ParseFeed(four, &r4, &r).ok() && !r4.empty() && r4[0].caller == 4, "feed PLOTS caller 4: %s", r.line().c_str());
		std::wstring d = Dir("forest_caller4_ok");
		CHECK(BuildOne(d, "feed.bin", MemberKind::Feed, four), "caller-4 feed not written");
		CHECK(CheckBundle(d, &r).ok(), "a feed with caller 4 refused by the checker: %s", r.line().c_str());
	}
	// frames.bin: bit 3 is the last known flag
	{
		std::vector<uint8_t> frames;
		EncodeStreamHead(MemberKind::Frames, &frames);
		FrameRecord fr = MakeFrame(1, "");
		fr.flags = kFrameFog | 16;
		frames.insert(frames.end(), (const uint8_t*)&fr, (const uint8_t*)&fr + sizeof(fr));
		std::wstring d = Dir("neg_frame_flag16");
		BuildOne(d, "frames.bin", MemberKind::Frames, frames);
		ExpectRefused("frame flag bit 4", d, Refusal::BadRecord, "frames.bin");
	}
	// fog_clock.bin: the form and its refusals
	std::vector<uint8_t> fc;
	EncodeFogClock(MakeFogClock(), &fc);
	CHECK(fc.size() == 32, "fog_clock.bin is %u bytes, 32 expected", (unsigned)fc.size());
	FogClockFile back;
	CHECK(ParseFogClock(fc, &back, &res).ok() && back.clockS == 41.765625 && back.clockLastMs == 123456.789, "fog clock parse");
	{
		FogClockFile none;   // no fog frame drawn yet: T 0, last -1
		std::vector<uint8_t> b;
		EncodeFogClock(none, &b);
		CHECK(ParseFogClock(b, &back, &res).ok() && back.clockS == 0 && back.clockLastMs == -1, "fog clock before any frame: %s", res.line().c_str());
	}
	struct T
	{
		const char* name;
		std::vector<uint8_t> bytes;
		Refusal want;
	};
	std::vector<T> ts;
	{
		std::vector<uint8_t> b = fc;
		b.resize(24);
		ts.push_back({"fog clock truncated", b, Refusal::Truncated});
	}
	{
		std::vector<uint8_t> b = fc;
		b.push_back(0);
		ts.push_back({"fog clock with a byte after", b, Refusal::WrongSize});
	}
	{
		FogClockFile c = MakeFogClock();
		c.clockS = -0.5;
		std::vector<uint8_t> b;
		EncodeFogClock(c, &b);
		ts.push_back({"fog clock T below 0", b, Refusal::BadRecord});
	}
	{
		FogClockFile c = MakeFogClock();
		c.clockLastMs = std::numeric_limits<double>::quiet_NaN();
		std::vector<uint8_t> b;
		EncodeFogClock(c, &b);
		ts.push_back({"fog clock last frame NaN", b, Refusal::BadRecord});
	}
	{
		std::vector<uint8_t> b = fc;
		uint32_t v = 2;
		memcpy(&b[4], &v, 4);
		ts.push_back({"fog clock form version 2", b, Refusal::BadVersion});
	}
	{
		std::vector<uint8_t> b = fc;
		b[3] = 'X';
		ts.push_back({"fog clock bad magic", b, Refusal::BadMagic});
	}
	int k = 0;
	for(T& t : ts)
	{
		char name[64];
		sprintf_s(name, "neg_fogclock_%02d", k++);
		std::wstring d = Dir(name);
		CHECK(BuildOne(d, "fog_clock.bin", MemberKind::FogClock, t.bytes), "%s: not written", t.name);
		ExpectRefused(t.name, d, t.want, "fog_clock.bin");
	}
	// the fog clock at another path is not a fog clock
	{
		Writer w;
		std::wstring d = Dir("neg_fogclock_path");
		CHECK(w.open(d).ok(), "open");
		CHECK(!w.addMember("fog/clock.bin", MemberKind::FogClock, fc.data(), fc.size()).ok(), "the writer took fog/clock.bin as a fog clock");
	}
	printf("ok   format version 2: the fog's members (%u checks so far)\n", (unsigned)g_pass);
}

// Format version 3 (issue 0364): decals/load<NNNN>.dtap (kind decal_tap) and its cross-checks against render_decals. Each
// negative through the child (exit code, line, nothing written) and in process.
static bool BuildDecal(const std::wstring& dir, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& members, const char* renderDecals,
                       const char* note = nullptr)
{
	Writer w;
	if(!w.open(dir).ok())
		return false;
	for(const auto& kv : members)
	{
		c5r::Status st = w.addMember(kv.first, MemberKind::DecalTap, kv.second.data(), kv.second.size());
		if(!st.ok())
		{
			printf("BuildDecal %s: %s\n", kv.first.c_str(), st.str().c_str());
			return false;
		}
	}
	Manifest m = BaseManifest();
	if(renderDecals)
		m.renderConfig["render_decals"] = renderDecals;
	if(note)
		m.versionNotes.push_back(note);
	return w.finish(m).ok();
}


// ---- format version 9 (ticket 4875, epic 0561): modeltap/frame<NNNNNN>.mtap (kind model_tap) -----------------------------
// An empty frame image as the compositor writes one (shell_model_tap.h): the header alone, every count 0, the offsets at
// the image's end, the hash of the empty tail.
static std::vector<uint8_t> MakeModelTap(uint64_t frameSeq)
{
	std::vector<uint8_t> b(sizeof(VPModelTapFrame), 0);
	VPModelTapFrame h;
	memset(&h, 0, sizeof(h));
	h.magic = VPSHELL_MODELTAP_MAGIC;
	h.version = VPSHELL_MODELTAP_VERSION;
	h.headerBytes = sizeof(VPModelTapFrame);
	h.drawBytes = sizeof(VPModelTapDraw);
	h.frameSeq = frameSeq;
	h.presentCount = 500 + frameSeq;
	h.drawOffset = h.rowOffset = h.constOffset = h.targetOffset = sizeof(VPModelTapFrame);
	h.imageBytes = sizeof(VPModelTapFrame);
	h.setGeneration = 1;
	h.latchedBits = VPMODE_CITIES;
	h.wantBits = VPMODE_CITIES;
	h.fnv1a64 = VPModelHash64(b.data(), 0);
	memcpy(b.data(), &h, sizeof(h));
	return b;
}

// a bundle of three frames (frames.bin, cameras.bin) with the given model tap members and render_config keys
static bool BuildModel(const std::wstring& dir, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& members,
                       const char* cities, const char* landmarks, const char* note = nullptr)
{
	Writer w;
	bool ok = w.open(dir).ok();
	ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok();
	ok = ok && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
	for(int k = 0; k < 3; ++k)
	{
		FrameRecord r = MakeFrame(k, std::string());
		r.flags = 0;
		VPShellCamera c = MakeCamera(k);
		ok = ok && w.append("frames.bin", &r, sizeof(r)).ok();
		ok = ok && w.append("cameras.bin", &c, sizeof(c)).ok();
	}
	for(const auto& kv : members)
	{
		// ticket 4888: modelgeom/ paths are the kind model_geom
		const MemberKind kind = kv.first.compare(0, 10, "modelgeom/") == 0 ? MemberKind::ModelGeom : MemberKind::ModelTap;
		c5r::Status st = w.addMember(kv.first, kind, kv.second.data(), kv.second.size());
		if(!st.ok())
		{
			printf("BuildModel %s: %s\n", kv.first.c_str(), st.str().c_str());
			return false;
		}
	}
	Manifest m = BaseManifest();
	m.firstFrameId = 100;
	m.frameCount = 3;
	if(cities)
		m.renderConfig["render_cities"] = cities;
	if(landmarks)
		m.renderConfig["render_landmarks"] = landmarks;
	if(note)
		m.versionNotes.push_back(note);
	const c5r::Status st = w.finish(m);
	if(!st.ok())
		printf("BuildModel: %s\n", st.str().c_str());
	return ok && st.ok();
}

// ---- format version 4 (epic 0375, issue 4457): feed.bin's form 2, the records 3-6 of the unit feed -------------------------
static SrvFeed::UnitRecord MakeUnit(int owner, int id, int plot, uint32_t seq)
{
	SrvFeed::UnitRecord u;
	memset(&u, 0, sizeof(u));
	u.owner = (int8_t)owner;
	u.domain = 1;
	u.facing = 3;
	u.flags = SrvFeed::kUnitFlagVisible;
	u.id = id;
	u.unitType = 5;
	u.plot = plot;
	u.maxHP = 100;
	u.artStyle = 2;
	u.era = 1;
	u.seq = seq;
	return u;
}
static SrvFeed::PlayerRecord MakePlayer(int player)
{
	SrvFeed::PlayerRecord p;
	memset(&p, 0, sizeof(p));
	p.player = (int8_t)player;
	p.team = (int8_t)player;
	p.alive = 1;
	p.colour = 3 + player;
	p.civilization = 7 + player;
	p.era = 1;
	p.artStyle = 2;
	p.changeGen = 1;
	p.state = 1;
	return p;
}
// A DRAIN with unit events and the three side tables; the expected batch is returned too
static SrvFeed::Batch MakeUnitBatch()
{
	SrvFeed::Batch b = MakeBatch();
	b.unitTableGen = 2;
	SrvFeed::Event e = {};
	e.seq = 20;
	e.kind = SrvFeed::kUnitMoved;
	e.plot = 6;
	e.a[0] = 0;
	e.a[1] = 1001;
	e.a[2] = 0;
	e.a[3] = 3;
	b.events.push_back(e);
	b.pathPlots = {4, 5, 6};
	b.paths = {{20, 0, 3}};
	SrvFeed::CombatRecord c;
	memset(&c, 0, sizeof(c));
	c.seq = 21;
	c.kind = SrvFeed::kUnitCombat;
	c.flags = SrvFeed::kCombatVisualize;
	c.plot = 6;
	c.attackerOwner = 0;
	c.attackerUnit = 1001;
	c.attackerCity = -1;
	c.defenderOwner = 1;
	c.defenderUnit = 1002;
	c.defenderCity = -1;
	c.nMembers = 1;
	b.combats = {c};
	SrvFeed::CombatMember m;
	memset(&m, 0, sizeof(m));
	m.player = 1;
	m.id = 1002;
	m.damage = 30;
	m.maxHP = 100;
	b.combatMembers = {m};
	b.players = {MakePlayer(1)};
	return b;
}
// DRAIN (unit batch, drainMs 1234.5) + PATHS + COMBATS + PLAYERS(2), PLOTS full (the residency's read comes before the
// unit scene's copies), PLAYERS(1), UNITS full, DRAIN (empty), UNITS diff
static std::vector<uint8_t> MakeUnitFeed()
{
	std::vector<uint8_t> out, rec;
	EncodeStreamHead(MemberKind::Feed, &out);
	FeedEncoder enc;
	CHECK(enc.addDrain(3, 50, MakeUnitBatch(), &rec, 1234.5).ok(), "addDrain of a unit batch");
	out.insert(out.end(), rec.begin(), rec.end());
	enc.addPlots(3, 1, 1, 4, 3, MakeTable(4, 3, 0), &rec);
	out.insert(out.end(), rec.begin(), rec.end());
	CHECK(enc.addPlayers(3, {MakePlayer(0), MakePlayer(1)}, &rec).ok(), "addPlayers");
	out.insert(out.end(), rec.begin(), rec.end());
	CHECK(enc.addUnits(3, srvunits::rec::kUnitsFirstCopy, 2, {MakeUnit(1, 1002, 7, 19), MakeUnit(0, 1001, 6, 20)}, &rec).ok(), "addUnits");
	out.insert(out.end(), rec.begin(), rec.end());
	SrvFeed::Batch empty;
	empty.unitTableGen = 2;
	CHECK(enc.addDrain(4, -1, empty, &rec).ok(), "addDrain empty");
	out.insert(out.end(), rec.begin(), rec.end());
	CHECK(enc.addUnits(4, srvunits::rec::kUnitsLaterCopy, 2, {MakeUnit(0, 1001, 8, 22)}, &rec).ok(), "addUnits diff");
	out.insert(out.end(), rec.begin(), rec.end());
	return out;
}
static void PutRec(std::vector<uint8_t>& b, uint32_t type, const std::vector<uint8_t>& payload)
{
	const uint32_t n = (uint32_t)payload.size();
	b.insert(b.end(), (const uint8_t*)&type, (const uint8_t*)&type + 4);
	b.insert(b.end(), (const uint8_t*)&n, (const uint8_t*)&n + 4);
	b.insert(b.end(), payload.begin(), payload.end());
}
template<class T> static void PutV(std::vector<uint8_t>& b, const T& v) { b.insert(b.end(), (const uint8_t*)&v, (const uint8_t*)&v + sizeof(T)); }
static std::vector<uint8_t> PathsPayload(uint64_t loop)
{
	std::vector<uint8_t> p;
	PutV(p, loop);
	PutV(p, (uint32_t)1);
	PutV(p, (uint32_t)30);
	PutV(p, (uint32_t)1);
	PutV(p, (int32_t)9);
	return p;
}
static std::vector<uint8_t> DrainOnly(uint64_t loop)
{
	std::vector<uint8_t> rec;
	FeedEncoder enc;
	enc.addDrain(loop, -1, SrvFeed::Batch(), &rec);
	return rec;
}

static void TestUnitRecords()
{
	// round trip through the parser: records 4, 5, 6-of-caller-2 folded into their DRAIN
	std::vector<uint8_t> feed = MakeUnitFeed();
	std::vector<FeedRecord> recs;
	CheckResult res;
	CHECK(ParseFeed(feed, &recs, &res).ok() && recs.size() == 3, "unit feed: %s (%u records)", res.line().c_str(), (unsigned)recs.size());
	if(recs.size() == 3)
	{
		const SrvFeed::Batch want = MakeUnitBatch();
		const SrvFeed::Batch& got = recs[0].batch;
		CHECK(recs[0].type == kFeedDrain && recs[0].drainMs == 1234.5 && recs[0].nextFrameId == 50 && got.unitTableGen == 2 &&
		          got.events.size() == 4 && memcmp(got.events.data(), want.events.data(), 4 * sizeof(SrvFeed::Event)) == 0,
		      "DRAIN 0: drainMs %g, gen %u, %u events", recs[0].drainMs, got.unitTableGen, (unsigned)got.events.size());
		CHECK(got.pathPlots == want.pathPlots && got.paths.size() == 1 && got.paths[0].seq == 20 && got.paths[0].first == 0 &&
		          got.paths[0].length == 3, "DRAIN 0: its PATHS folded in");
		CHECK(got.combats.size() == 1 && memcmp(&got.combats[0], &want.combats[0], 64) == 0 && got.combatMembers.size() == 1 &&
		          memcmp(&got.combatMembers[0], &want.combatMembers[0], 28) == 0, "DRAIN 0: its COMBATS folded in");
		CHECK(got.players.size() == 1 && memcmp(&got.players[0], &want.players[0], 32) == 0, "DRAIN 0: its PLAYERS (caller 2) folded in");
		CHECK(recs[0].playerCopies.size() == 1 && recs[0].playerCopies[0].size() == 2 && recs[0].playerCopies[0][1].player == 1,
		      "DRAIN 0: its PLAYERS of caller 1 (a copy) folded in");
		CHECK(recs[0].unitCopies.size() == 1 && recs[0].unitCopies[0].caller == 1 && recs[0].unitCopies[0].full &&
		          recs[0].unitCopies[0].tableGen == 2 && recs[0].unitCopies[0].units.size() == 2 && recs[0].unitCopies[0].units[0].id == 1001 &&
		          recs[0].unitCopies[0].units[1].id == 1002, "DRAIN 0: its UNITS (full, sorted) folded in");
		CHECK(recs[1].type == kFeedPlots && recs[2].type == kFeedDrain && recs[2].drainMs == -1 && recs[2].batch.paths.empty(), "records 1-2");
		CHECK(recs[2].unitCopies.size() == 1 && recs[2].unitCopies[0].caller == 2 && recs[2].unitCopies[0].units.size() == 1 &&
		          recs[2].unitCopies[0].units[0].plot == 8, "DRAIN 2: its UNITS diff applied");
	}
	{
		std::wstring d = Dir("units_feed_ok");
		CHECK(BuildOne(d, "feed.bin", MemberKind::Feed, feed), "unit feed not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a feed with records 3-6 refused by the checker: %s", r.line().c_str());
	}
	// the file feed yields them in call order; the recording feed hands the scene's copies to its sink
	{
		std::string why;
		srvrender::FileFeedControl* ctl = nullptr;
		Manifest m;
		std::wstring d = Dir("units_feed_file");
		BuildOne(d, "feed.bin", MemberKind::Feed, feed);
		CheckResult r;
		ReadManifest(d, &m, &r);
		std::unique_ptr<srvrender::FeedSource> f = srvrender::MakeFileFeed(d, m, &ctl, &why);
		CHECK(f && ctl, "file feed of the unit feed: %s", why.c_str());
		if(f && ctl)
		{
			SrvFeed::Batch b;
			std::vector<SrvFeed::PlayerRecord> pl;
			std::vector<SrvFeed::UnitRecord> un;
			uint32_t gen = 0;
			CHECK(f->drain(b) && b.paths.size() == 1 && b.combats.size() == 1 && ctl->lastDrainMs() == 1234.5 && ctl->unitCopiesLeft() == 2,
			      "file feed drain 0");
			std::vector<SrvFeed::PlotRecord> t;
			uint32_t pg = 0, w = 0, h = 0;
			f->copyPlots(t, pg, w, h, 1);
			f->copyPlayers(pl);
			f->copyUnits(un, gen);
			CHECK(pl.size() == 2 && un.size() == 2 && gen == 2 && !ctl->failed() && ctl->unitCopiesLeft() == 0, "file feed copies: %s",
			      ctl->why().c_str());
			// a copy the recording did not take: the feed fails
			f->copyUnits(un, gen);
			CHECK(ctl->failed() && un.empty() && gen == 0 &&
			          ctl->why().find("out of order: copyUnits called, no recorded copy left after the DRAIN of loop iteration 3") != std::string::npos,
			      "file feed out of order: %s", ctl->why().c_str());
		}
		struct Sink : srvrender::FeedRecordSink
		{
			size_t drains = 0, units = 0, players = 0;
			void onDrain(const SrvFeed::Batch&) override { drains++; }
			void onPlots(uint32_t, const std::vector<SrvFeed::PlotRecord>&, uint32_t, uint32_t, uint32_t) override {}
			void onUnits(const std::vector<SrvFeed::UnitRecord>& t, uint32_t) override { units += t.size(); }
			void onPlayers(const std::vector<SrvFeed::PlayerRecord>& r) override { players += r.size(); }
		} sink;
		srvrender::FileFeedControl* ctl2 = nullptr;
		std::unique_ptr<srvrender::FeedSource> f2 = srvrender::MakeFileFeed(d, m, &ctl2, &why);
		if(f2)
		{
			std::unique_ptr<srvrender::FeedSource> rec = srvrender::MakeRecordingFeed(*f2, sink);
			SrvFeed::Batch b;
			std::vector<SrvFeed::PlayerRecord> pl;
			std::vector<SrvFeed::UnitRecord> un;
			uint32_t gen = 0;
			rec->drain(b);
			std::vector<SrvFeed::PlotRecord> t;
			uint32_t pg = 0, w = 0, h = 0;
			rec->copyPlots(t, pg, w, h, 1);
			rec->copyPlayers(pl);
			rec->copyUnits(un, gen);
			CHECK(sink.drains == 1 && sink.units == 2 && sink.players == 2, "recording feed: %u drains, %u units, %u players",
			      (unsigned)sink.drains, (unsigned)sink.units, (unsigned)sink.players);
		}
	}
	// the encoder refuses a batch the codec refuses and writes nothing
	{
		SrvFeed::Batch b = MakeUnitBatch();
		b.paths[0].length = 9;   // past the batch's 3 plots
		std::vector<uint8_t> rec;
		FeedEncoder enc;
		c5r::Status st = enc.addDrain(1, -1, b, &rec);
		CHECK(!st.ok() && rec.empty() && st.str().find("outside the batch's 3 plots") != std::string::npos, "addDrain of a bad batch: %s",
		      st.str().c_str());
		b = MakeUnitBatch();
		b.combats[0].kind = 2;
		st = enc.addDrain(1, -1, b, &rec);
		CHECK(!st.ok() && rec.empty() && st.str().find("kind 2 is not 25 or 28") != std::string::npos, "addDrain bad combat: %s", st.str().c_str());
		st = enc.addUnits(1, 7, 1, {}, &rec);
		CHECK(!st.ok() && rec.empty(), "addUnits caller 7: %s", st.str().c_str());
	}
	// negatives through the child (exit code, output line, nothing written)
	struct Neg
	{
		const char* name;
		std::vector<uint8_t> bytes;
		Refusal want;
		const char* text;
	};
	std::vector<Neg> negs;
	{
		std::vector<uint8_t> b = feed;
		uint32_t v1 = 1;
		memcpy(&b[4], &v1, 4);
		negs.push_back({"feed.bin of file form 1", b, Refusal::BadVersion, "file form version 1, this reader reads 2"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5), pl;
		b.insert(b.end(), d.begin(), d.end());
		std::vector<uint8_t> plots;
		FeedEncoder enc;
		enc.addPlots(5, 1, 1, 4, 3, MakeTable(4, 3, 0), &plots);
		b.insert(b.end(), plots.begin(), plots.end());
		PutRec(b, kFeedPaths, PathsPayload(5));
		negs.push_back({"PATHS not right after its DRAIN", b, Refusal::BadRecord, "record 2 (PATHS): not right after its DRAIN"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		b.insert(b.end(), d.begin(), d.end());
		PutRec(b, kFeedPaths, PathsPayload(6));
		negs.push_back({"PATHS of another loop iteration", b, Refusal::BadRecord, "not right after its DRAIN (loop iteration 6)"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		PutRec(b, kFeedPaths, PathsPayload(5));
		negs.push_back({"PATHS before any DRAIN", b, Refusal::BadRecord, "record 0 (PATHS): not right after its DRAIN"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		b.insert(b.end(), d.begin(), d.end());
		PutRec(b, kFeedPaths, PathsPayload(5));
		PutRec(b, kFeedPaths, PathsPayload(5));
		negs.push_back({"two PATHS for one DRAIN", b, Refusal::BadRecord, "record 2 (PATHS): a second one for the DRAIN"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(4);
		b.insert(b.end(), d.begin(), d.end());
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)0);
		PutRec(b, kFeedPlayers, p);
		negs.push_back({"PLAYERS copy without a DRAIN of its loop iteration", b, Refusal::BadRecord,
		                "record 1 (PLAYERS of caller 1): no DRAIN of loop iteration 5 before it"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		b.insert(b.end(), d.begin(), d.end());
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)0);
		PutRec(b, kFeedPlayers, p);
		PutRec(b, kFeedPaths, PathsPayload(5));
		negs.push_back({"PATHS after the scene's copies", b, Refusal::BadRecord, "record 2 (PATHS): not right after its DRAIN"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		b.insert(b.end(), d.begin(), d.end());
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutV(p, (uint32_t)2);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)0);
		PutV(p, (uint32_t)0);
		PutV(p, (uint32_t)0);
		PutRec(b, kFeedUnits, p);
		negs.push_back({"UNITS diff before a full table", b, Refusal::BadRecord, "record 1 (UNITS): UnitsDiffBeforeFull: UNITS: a diff before any full table"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)1);
		PutV(p, (uint32_t)1);   // one unit announced, none present
		PutRec(b, kFeedUnits, p);
		negs.push_back({"UNITS shorter than its count", b, Refusal::WrongSize, "record 0 (UNITS): Truncated: UNITS: count 1 needs 32 bytes, 0 left"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutV(p, (uint32_t)9);
		PutV(p, (uint32_t)0);
		PutRec(b, kFeedPlayers, p);
		negs.push_back({"PLAYERS of caller 9", b, Refusal::BadRecord, "record 0 (PLAYERS): BadRecord: PLAYERS: caller 9"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		const double nan = std::numeric_limits<double>::quiet_NaN();
		memcpy(&d[8 + 8 + 8 + 12 + 8 + 4], &nan, 8);   // type, bytes, loopIter, nextFrameId, 3 x u32, dropped, unitTableGen
		b.insert(b.end(), d.begin(), d.end());
		negs.push_back({"DRAIN drainMs not finite", b, Refusal::BadRecord, "record 0 (DRAIN): drainMs is not finite"});
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<uint8_t> d = DrainOnly(5);
		b.insert(b.end(), d.begin(), d.end());
		PutRec(b, kFeedPaths, PathsPayload(5));
		std::vector<uint8_t> p;
		PutV(p, (uint64_t)5);
		PutRec(b, 7, p);
		negs.push_back({"feed record type 7", b, Refusal::BadRecord, "unknown type 7"});
	}
	for(Neg& n : negs)
	{
		std::wstring d = g_work + L"/neg_units_" + std::to_wstring(&n - negs.data());
		RemoveTree(d);
		BuildOne(d, "feed.bin", MemberKind::Feed, n.bytes);
		ExpectRefused(n.name, d, n.want, "feed.bin");
		CheckResult r;
		CheckBundle(d, &r);
		CHECK(r.why.find(n.text) != std::string::npos, "%s: why lacks \"%s\": %s", n.name, n.text, r.why.c_str());
	}
}

static void TestDecalMember()
{
	CHECK(std::string(MemberKindName(MemberKind::DecalTap)) == "decal_tap", "kind name decal_tap");
	MemberKind k;
	CHECK(MemberKindFromName("decal_tap", &k) && k == MemberKind::DecalTap, "kind from name");
	CHECK(DecalTapPath(1) == "decals/load0001.dtap" && DecalTapPath(9999) == "decals/load9999.dtap" && DecalTapPath(10000).empty(),
	      "DecalTapPath: %s", DecalTapPath(1).c_str());
	const std::vector<uint8_t> img = decalfixture::MakeImage(5, 1);
	// written and read back: the parser, the checker (render_decals 1), the bytes on disk equal
	DecalTapFile f;
	CheckResult res;
	CHECK(ParseDecalTap(img, &f, &res).ok() && f.generation == 5 && f.loadSeq == 1 && f.decalCount == 3 && f.nameCount == 3 &&
	          f.imageBytes == img.size(),
	      "ParseDecalTap: %s", res.line().c_str());
	{
		std::wstring d = Dir("decal_ok");
		CHECK(BuildDecal(d, {{"decals/load0001.dtap", img}}, "1"), "decal bundle not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a bundle with the decal member refused: %s", r.line().c_str());
		Manifest m;
		CHECK(ReadManifest(d, &m, &r).ok() && m.members.count("decals/load0001.dtap") &&
		          m.members["decals/load0001.dtap"].kind == MemberKind::DecalTap && m.members["decals/load0001.dtap"].bytes == img.size() &&
		          m.members["decals/load0001.dtap"].sha256 == Sha256Hex(img.data(), img.size()),
		      "manifest entry kind decal_tap with bytes and sha256: %s", r.line().c_str());
		std::vector<uint8_t> back;
		CHECK(ReadMember(d, m, "decals/load0001.dtap", &back, &r).ok() && back == img, "the member reads back byte-equal: %s", r.line().c_str());
		std::string js;
		{
			std::vector<uint8_t> j;
			ReadBytes(d + L"/scene.json", &j);
			js.assign(j.begin(), j.end());
		}
		CHECK(js.find("\"format_version\": 11") != std::string::npos && js.find("\"kind\": \"decal_tap\"") != std::string::npos,
		      "scene.json: format_version 4 and kind decal_tap");
	}
	{   // render_decals 1 without a member, with the recorder's note: accepted (the live span drew no decals)
		std::wstring d = Dir("decal_noset_note");
		std::string note = std::string(kDecalNoSetNote) + " (frame 7, the reader has no set yet): the span is drawn without decals";
		CHECK(BuildDecal(d, {}, "1", note.c_str()), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_decals 1 with the no-set note refused: %s", r.line().c_str());
	}
	{   // render_decals 0 and no member: accepted
		std::wstring d = Dir("decal_off");
		CHECK(BuildDecal(d, {}, "0"), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_decals 0 without a member refused: %s", r.line().c_str());
	}
	// the file form's refusals
	struct T
	{
		const char* name;
		std::vector<uint8_t> bytes;
		Refusal want;
	};
	std::vector<T> ts;
	auto set32 = [](std::vector<uint8_t> b, size_t o, uint32_t v) {
		memcpy(&b[o], &v, 4);
		return b;
	};
	{
		std::vector<uint8_t> b = img;
		b.resize(img.size() - 16);
		ts.push_back({"decal image truncated", b, Refusal::Truncated});
	}
	{
		std::vector<uint8_t> b = img;
		b.resize(100);
		ts.push_back({"decal image shorter than its header", b, Refusal::Truncated});
	}
	{
		std::vector<uint8_t> b = img;
		b.push_back(0);
		ts.push_back({"decal image with a byte after", b, Refusal::WrongSize});
	}
	ts.push_back({"decal image wrong magic", set32(img, 0, 0x54443544u), Refusal::BadMagic});
	ts.push_back({"decal image version 2", set32(img, 4, 2), Refusal::BadVersion});
	ts.push_back({"decal image header 116", set32(img, 8, 116), Refusal::WrongSize});
	ts.push_back({"decal image record 52", set32(img, 12, 52), Refusal::WrongSize});
	ts.push_back({"decal image texture index 9 (the reader's check)", set32(img, 112 + 24, 9), Refusal::BadRecord});
	ts.push_back({"decal image 0 triangles (the reader's check)", set32(img, 112 + 40, 0), Refusal::BadRecord});
	int n = 0;
	for(T& t : ts)
	{
		char name[64];
		sprintf_s(name, "neg_decal_%02d", n++);
		std::wstring d = Dir(name);
		CHECK(BuildDecal(d, {{"decals/load0001.dtap", t.bytes}}, "1"), "%s: not written", t.name);
		ExpectRefused(t.name, d, t.want, "decals/load0001.dtap");
		CheckResult r;
		CHECK(!ParseDecalTap(t.bytes, nullptr, &r).ok() && r.refusal == t.want, "%s: ParseDecalTap %s", t.name, r.line().c_str());
	}
	{   // the name's NNNN is not the image's loadSeq
		std::wstring d = Dir("neg_decal_name");
		CHECK(BuildDecal(d, {{"decals/load0002.dtap", img}}, "1"), "not written");
		ExpectRefused("decal member of load 1 named load0002", d, Refusal::BadRecord, "decals/load0002.dtap");
	}
	{   // missing while the manifest names it
		std::wstring d = Dir("neg_decal_missing");
		CHECK(BuildDecal(d, {{"decals/load0001.dtap", img}}, "1"), "not written");
		CHECK(DeleteFileW((d + L"/decals/load0001.dtap").c_str()) != 0, "delete");
		ExpectRefused("decal member listed, file missing", d, Refusal::MissingMember, "decals/load0001.dtap");
	}
	{   // render_decals 1, no member, no note
		std::wstring d = Dir("neg_decal_absent");
		CHECK(BuildDecal(d, {}, "1"), "not written");
		ExpectRefused("render_decals 1 without the member or the note", d, Refusal::MissingMember, "decals/load<NNNN>.dtap");
	}
	{   // a member while render_decals is 0
		std::wstring d = Dir("neg_decal_off");
		CHECK(BuildDecal(d, {{"decals/load0001.dtap", img}}, "0"), "not written");
		ExpectRefused("a decal member with render_decals 0", d, Refusal::BadValue, "decals/load0001.dtap");
	}
	{   // two members
		std::wstring d = Dir("neg_decal_two");
		CHECK(BuildDecal(d, {{"decals/load0001.dtap", img}, {"decals/load0002.dtap", decalfixture::MakeImage(6, 2)}}, "1"), "not written");
		ExpectRefused("two decal members", d, Refusal::BadValue, "decals/load0002.dtap");
	}
	{   // paths of another form
		Writer w;
		std::wstring d = Dir("neg_decal_path");
		CHECK(w.open(d).ok(), "open");
		CHECK(!w.addMember("decals/load1.dtap", MemberKind::DecalTap, img.data(), img.size()).ok(), "the writer took decals/load1.dtap");
		CHECK(!w.addMember("decals/load00001.dtap", MemberKind::DecalTap, img.data(), img.size()).ok(), "the writer took 5 digits");
		CHECK(!w.addMember("decal/load0001.dtap", MemberKind::DecalTap, img.data(), img.size()).ok(), "the writer took decal/");
		CHECK(!w.addMember("decals/load000a.dtap", MemberKind::DecalTap, img.data(), img.size()).ok(), "the writer took a hex digit");
	}
	printf("ok   format version 3: the decal member (%u checks so far)\n", (unsigned)g_pass);
}

// ---- format version 5 (issue 0440): units/gen<NNNNNNNNNN>.db and units_history.bin ----------------------------------------
static std::vector<uint8_t> MakeUnitsDb(uint8_t salt)
{
	std::vector<uint8_t> b(4096, salt);
	memcpy(b.data(), "SQLite format 3", 16);   // the 15 characters and the terminating zero
	return b;
}

// n records from frame 50, loop iterations 1.., the first drawn with ready() off, every later one with it on
static UnitsHistoryFile MakeHistory(size_t n, uint32_t gen)
{
	UnitsHistoryFile f;
	f.dbGeneration = gen;
	f.flags = kUnitsHistReady;
	for(size_t k = 0; k < n; ++k)
	{
		UnitsHistoryRecord r;
		memset(&r, 0, sizeof(r));
		r.frameId = 50 + (int64_t)k;
		r.loopIter = 1 + (uint64_t)k;
		r.nowMs = 1000.0 + 16.25 * (double)k;
		r.flags = k ? kUnitsHistReady : 0;
		r.cam = MakeCamera((int)k);
		f.records.push_back(r);
	}
	return f;
}

static std::vector<uint8_t> HistBytes(const UnitsHistoryFile& f)
{
	std::vector<uint8_t> b;
	EncodeUnitsHistory(f, &b);
	return b;
}

typedef std::vector<std::pair<std::string, std::pair<MemberKind, std::vector<uint8_t>>>> MemberList;

// A bundle with the span of three frames (loop iterations 6-8, nothing dumped) and the given members; renderUnits null:
// no render_units key.
static bool BuildUnits(const std::wstring& dir, const MemberList& members, const char* renderUnits, const std::vector<std::string>& notes)
{
	Writer w;
	bool ok = w.open(dir).ok();
	for(const auto& kv : members)
	{
		const c5r::Status st = w.addMember(kv.first, kv.second.first, kv.second.second.data(), kv.second.second.size());
		if(!st.ok())
		{
			printf("BuildUnits %s: %s\n", kv.first.c_str(), st.str().c_str());
			ok = false;
		}
	}
	ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok();
	ok = ok && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
	for(int k = 0; k < 3; ++k)
	{
		FrameRecord r = MakeFrame(k, "");
		r.flags &= ~kFrameDumped;
		VPShellCamera c = MakeCamera(k);
		ok = ok && w.append("frames.bin", &r, sizeof(r)).ok();
		ok = ok && w.append("cameras.bin", &c, sizeof(c)).ok();
	}
	Manifest m = BaseManifest();
	m.firstFrameId = 100;
	m.frameCount = 3;
	if(renderUnits)
		m.renderConfig["render_units"] = renderUnits;
	for(const std::string& n : notes)
		m.versionNotes.push_back(n);
	return w.finish(m).ok() && ok;
}

// Ticket 4875 (format version 9): the model tap member, its file form's refusals and its cross-checks against
// render_cities / render_landmarks and frames.bin (srv_scene_bundle.h, the block at ModelTapFile).
static void TestModelTapMember()
{
	CHECK(kFormatVersion == 11 && std::string(MemberKindName(MemberKind::ModelTap)) == "model_tap", "format 9, kind model_tap");
	MemberKind k;
	CHECK(MemberKindFromName("model_tap", &k) && k == MemberKind::ModelTap, "kind from name");
	uint32_t idx = 7;
	CHECK(ModelTapPath(0) == "modeltap/frame000000.mtap" && ModelTapPath(999999) == "modeltap/frame999999.mtap" &&
	          ModelTapPath(1000000).empty() && ModelTapIndex("modeltap/frame000042.mtap", &idx) && idx == 42 &&
	          !ModelTapIndex("modeltap/frame42.mtap", nullptr) && !ModelTapIndex("modeltap/frame00004a.mtap", nullptr) &&
	          !ModelTapIndex("modeltap/frame000042.mtapx", nullptr),
	      "ModelTapPath / ModelTapIndex");
	const std::vector<uint8_t> img = MakeModelTap(11), img2 = MakeModelTap(12);
	ModelTapFile f;
	CheckResult res;
	CHECK(ParseModelTap(img, &f, &res).ok() && f.frameSeq == 11 && f.drawCount == 0 && f.imageBytes == 128 &&
	          f.latchedBits == VPMODE_CITIES,
	      "ParseModelTap: %s", res.line().c_str());
	{   // two members (frame 0 and frame 2), render_cities 1: accepted, scene.json names the kind and version 9
		std::wstring d = Dir("model_ok");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modeltap/frame000002.mtap", img2}}, "1", "0"), "model bundle not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a bundle with model tap members refused: %s", r.line().c_str());
		Manifest m;
		CHECK(ReadManifest(d, &m, &r).ok() && m.members.count("modeltap/frame000002.mtap") &&
		          m.members["modeltap/frame000002.mtap"].kind == MemberKind::ModelTap,
		      "manifest entry kind model_tap: %s", r.line().c_str());
		std::vector<uint8_t> j;
		ReadBytes(d + L"/scene.json", &j);
		const std::string js(j.begin(), j.end());
		CHECK(js.find("\"format_version\": 11") != std::string::npos && js.find("\"kind\": \"model_tap\"") != std::string::npos,
		      "scene.json: format_version 9 and kind model_tap");
		// the player's loader: sorted by frame index, the member in force per frame
		ModelTapMembers t;
		std::string note, why;
		CHECK(LoadModelTapMembers(d, m, &t, &note, &why) && t.members.size() == 2 && t.members[0].first == 0 && t.members[1].first == 2,
		      "LoadModelTapMembers: %s", why.c_str());
	}
	{   // landmarks only: accepted
		std::wstring d = Dir("model_landmarks");
		CHECK(BuildModel(d, {{"modeltap/frame000001.mtap", img}}, nullptr, "1"), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_landmarks 1 with a member refused: %s", r.line().c_str());
	}
	{   // on without a member, with the recorder's note: accepted; the loader says so
		std::wstring d = Dir("model_note");
		const std::string note = std::string(kModelNoFrameNote) + " (frame 100, the module has no frame yet)";
		CHECK(BuildModel(d, {}, "1", "1", note.c_str()), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "on with the no-frame note refused: %s", r.line().c_str());
		Manifest m;
		ReadManifest(d, &m, &r);
		ModelTapMembers t;
		std::string ln, why;
		CHECK(LoadModelTapMembers(d, m, &t, &ln, &why) && t.members.empty() && ln.find("no model tap member") == 0, "loader: %s", ln.c_str());
	}
	{   // both off (or absent) and no member: accepted, the bundle replays as before
		std::wstring d = Dir("model_off");
		CHECK(BuildModel(d, {}, "0", nullptr), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "models off without a member refused: %s", r.line().c_str());
		std::wstring d2 = Dir("model_absent_keys");
		CHECK(BuildModel(d2, {}, nullptr, nullptr), "not written");
		CHECK(CheckBundle(d2, &r).ok(), "no model keys and no member refused: %s", r.line().c_str());
	}
	// the file form's refusals
	struct T
	{
		const char* name;
		std::vector<uint8_t> bytes;
		Refusal want;
	};
	std::vector<T> ts;
	auto set32 = [](std::vector<uint8_t> b, size_t o, uint32_t v) {
		memcpy(&b[o], &v, 4);
		return b;
	};
	{
		std::vector<uint8_t> b = img;
		b.resize(100);
		ts.push_back({"model frame shorter than its header", b, Refusal::Truncated});
	}
	ts.push_back({"model frame imageBytes past the file", set32(img, offsetof(VPModelTapFrame, imageBytes), 256), Refusal::Truncated});
	{
		std::vector<uint8_t> b = img;
		b.push_back(0);
		ts.push_back({"model frame with a byte after", b, Refusal::WrongSize});
	}
	ts.push_back({"model frame wrong magic", set32(img, 0, 0x54443544u), Refusal::BadMagic});
	ts.push_back({"model frame version 2", set32(img, 4, 2), Refusal::BadVersion});
	ts.push_back({"model frame header 132", set32(img, 8, 132), Refusal::WrongSize});
	ts.push_back({"model frame draw record 96", set32(img, 12, 96), Refusal::WrongSize});
	{
		std::vector<uint8_t> b = img;
		b[offsetof(VPModelTapFrame, fnv1a64)] ^= 1;
		ts.push_back({"model frame hash", b, Refusal::BadRecord});
	}
	int n = 0;
	for(T& t : ts)
	{
		char name[64];
		sprintf_s(name, "neg_model_%02d", n++);
		std::wstring d = Dir(name);
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", t.bytes}}, "1", "0"), "%s: not written", t.name);
		ExpectRefused(t.name, d, t.want, "modeltap/frame000000.mtap");
		CheckResult r;
		CHECK(!ParseModelTap(t.bytes, nullptr, &r).ok() && r.refusal == t.want, "%s: ParseModelTap %s", t.name, r.line().c_str());
	}
	{   // NNNNNN not below the frame count
		std::wstring d = Dir("neg_model_index");
		CHECK(BuildModel(d, {{"modeltap/frame000003.mtap", img}}, "1", "0"), "not written");
		ExpectRefused("model member at frame index 3 of 3 frames", d, Refusal::BadRecord, "modeltap/frame000003.mtap");
	}
	{   // on, no member, no note
		std::wstring d = Dir("neg_model_absent");
		CHECK(BuildModel(d, {}, "0", "1"), "not written");
		ExpectRefused("render_landmarks 1 without a member or the note", d, Refusal::MissingMember, "modeltap/frame<NNNNNN>.mtap");
	}
	{   // a member while both are off
		std::wstring d = Dir("neg_model_off");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}}, "0", "0"), "not written");
		ExpectRefused("a model member with render_cities and render_landmarks 0", d, Refusal::BadValue, "modeltap/frame000000.mtap");
		Manifest m;
		CheckResult r;
		ReadManifest(d, &m, &r);
		ModelTapMembers t;
		std::string note, why;
		CHECK(!LoadModelTapMembers(d, m, &t, &note, &why) && why.find("not 1") != std::string::npos, "loader refuses: %s", why.c_str());
	}
	{   // a member file missing while the manifest names it
		std::wstring d = Dir("neg_model_missing");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}}, "1", "0"), "not written");
		CHECK(DeleteFileW((d + L"/modeltap/frame000000.mtap").c_str()) != 0, "delete");
		ExpectRefused("model member listed, file missing", d, Refusal::MissingMember, "modeltap/frame000000.mtap");
	}
}

// ---- format version 9, optional (ticket 4888, epic 0561): modelgeom/frame<NNNNNN>.mgeo (kind model_geom) -------------------
// The CPU-built meshes' buffers: an R32 index buffer of 270 indices and a 20-byte vertex buffer of 160 vertices (the road
// bridge's shape, city-renderer.md 13.1), made up here (nothing of the game).
static std::vector<ModelGeomBuffer> MakeGeomBuffers()
{
	ModelGeomBuffer ib, vb;
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
	return {vb, ib};   // unsorted on purpose: EncodeModelGeom sorts
}

static void TestModelGeomMember()
{
	CHECK(kFormatVersion == 11 && std::string(MemberKindName(MemberKind::ModelGeom)) == "model_geom", "format 9, kind model_geom");
	MemberKind k;
	CHECK(MemberKindFromName("model_geom", &k) && k == MemberKind::ModelGeom && MemberKindVersion(MemberKind::ModelGeom) == 1,
	      "kind from name, form version 1");
	uint32_t idx = 7;
	CHECK(ModelGeomPath(0) == "modelgeom/frame000000.mgeo" && ModelGeomPath(999999) == "modelgeom/frame999999.mgeo" &&
	          ModelGeomPath(1000000).empty() && ModelGeomIndex("modelgeom/frame000042.mgeo", &idx) && idx == 42 &&
	          !ModelGeomIndex("modelgeom/frame42.mgeo", nullptr) && !ModelGeomIndex("modelgeom/frame00004a.mgeo", nullptr) &&
	          !ModelGeomIndex("modeltap/frame000042.mtap", nullptr) && !ModelGeomIndex("modelgeom/frame000042.mgeox", nullptr),
	      "ModelGeomPath / ModelGeomIndex");
	const std::vector<ModelGeomBuffer> bufs = MakeGeomBuffers();
	std::vector<uint8_t> geo, empty;
	EncodeModelGeom(bufs, &geo);
	EncodeModelGeom({}, &empty);
	{   // the round trip: sorted by (kind, hash), the bytes and strides as given
		ModelGeomFile f;
		CheckResult res;
		CHECK(ParseModelGeom(geo, &f, &res).ok() && f.buffers.size() == 2 && f.dataBytes == 4 * 270 + 20 * 160,
		      "ParseModelGeom: %s", res.line().c_str());
		CHECK(f.buffers.size() == 2 && f.buffers[0].kind == VPMGEOM_KIND_INDEX32 && f.buffers[0].stride == 4 &&
		          f.buffers[0].hash == bufs[1].hash && f.buffers[0].bytes == bufs[1].bytes && f.buffers[1].kind == VPMGEOM_KIND_VERTEX &&
		          f.buffers[1].stride == 20 && f.buffers[1].bytes == bufs[0].bytes,
		      "the buffers back in (kind, hash) order");
		CHECK(geo.size() == 32 + 2 * 32 + 4 * 270 + 20 * 160, "file size %u", (unsigned)geo.size());
		ModelGeomFile e;
		CHECK(ParseModelGeom(empty, &e, &res).ok() && e.buffers.empty() && empty.size() == 32, "the empty list: %s", res.line().c_str());
	}
	const std::vector<uint8_t> img = MakeModelTap(11);
	{   // a tap at 0, the geometry at 0 and none from 2: accepted; the player's loader lists both kinds
		std::wstring d = Dir("geom_ok");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modelgeom/frame000000.mgeo", geo}, {"modelgeom/frame000002.mgeo", empty}},
		                 "1", "1"),
		      "geometry bundle not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a bundle with model_geom members refused: %s", r.line().c_str());
		Manifest m;
		CHECK(ReadManifest(d, &m, &r).ok() && m.members.count("modelgeom/frame000000.mgeo") &&
		          m.members["modelgeom/frame000000.mgeo"].kind == MemberKind::ModelGeom && m.formatVersion == kFormatVersion,
		      "manifest entry kind model_geom, format 9: %s", r.line().c_str());
		std::vector<uint8_t> j;
		ReadBytes(d + L"/scene.json", &j);
		const std::string js(j.begin(), j.end());
		CHECK(js.find("\"kind\": \"model_geom\"") != std::string::npos, "scene.json: kind model_geom");
		ModelTapMembers t;
		std::string note, why;
		CHECK(LoadModelTapMembers(d, m, &t, &note, &why) && t.members.size() == 1 && t.geom.size() == 2 && t.geom[0].first == 0 &&
		          t.geom[1].first == 2 && note.find("2 CPU-built geometry members") != std::string::npos,
		      "LoadModelTapMembers: %s / %s", note.c_str(), why.c_str());
		// the player hands the geometry over before the tap: a scene without a model module is refused naming the geometry
		srvrender::Scene scene;
		why.clear();
		CHECK(!ApplyModelTapFrame(scene, d, m, t, 0, &why) && why.find("modelgeom/frame000000.mgeo") == 0 &&
		          why.find("no model module") != std::string::npos,
		      "ApplyModelTapFrame without a module: %s", why.c_str());
	}
	{   // a bundle without the member (the six Tiny-save bundles): accepted, no geometry listed
		std::wstring d = Dir("geom_absent");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}}, "1", "1"), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a model bundle without model_geom refused: %s", r.line().c_str());
		Manifest m;
		ReadManifest(d, &m, &r);
		ModelTapMembers t;
		std::string note, why;
		CHECK(LoadModelTapMembers(d, m, &t, &note, &why) && t.geom.empty() && note.find("CPU-built") == std::string::npos,
		      "loader without geometry: %s", note.c_str());
	}
	// the file form's refusals (each through the checker child and in process)
	struct T
	{
		const char* name;
		std::vector<uint8_t> bytes;
		Refusal want;
	};
	std::vector<T> ts;
	auto set32 = [](std::vector<uint8_t> b, size_t o, uint32_t v) {
		memcpy(&b[o], &v, 4);
		return b;
	};
	auto set64 = [](std::vector<uint8_t> b, size_t o, uint64_t v) {
		memcpy(&b[o], &v, 8);
		return b;
	};
	const size_t e0 = 32, e1 = 64;   // the two entries (index buffer first): hash +0, kind +8, stride +12, offset +16, bytes +24
	{
		std::vector<uint8_t> b = geo;
		b.resize(24);
		ts.push_back({"geometry shorter than its fixed fields", b, Refusal::Truncated});
	}
	ts.push_back({"geometry wrong magic", set32(geo, 0, 0x47544D43u), Refusal::BadMagic});
	ts.push_back({"geometry version 2", set32(geo, 4, 2), Refusal::BadVersion});
	ts.push_back({"geometry entry record 24", set32(geo, 20, 24), Refusal::WrongSize});
	ts.push_back({"geometry 513 entries", set32(geo, 16, 513), Refusal::WrongSize});
	{
		std::vector<uint8_t> b = geo;
		b.pop_back();
		ts.push_back({"geometry data cut short", b, Refusal::Truncated});
		b = geo;
		b.push_back(0);
		ts.push_back({"geometry with a byte after", b, Refusal::WrongSize});
	}
	ts.push_back({"geometry dataBytes larger than the entries", set64(geo, 24, geo.size() - 96 + 4), Refusal::Truncated});
	ts.push_back({"geometry kind 3", set32(geo, e0 + 8, 3), Refusal::BadRecord});
	ts.push_back({"geometry vertex stride 24", set32(geo, e1 + 12, 24), Refusal::BadRecord});
	ts.push_back({"geometry index stride 2", set32(geo, e0 + 12, 2), Refusal::BadRecord});
	ts.push_back({"geometry bytes over 8 MB", set64(geo, e1 + 24, (8ull << 20) + 20), Refusal::BadRecord});
	ts.push_back({"geometry bytes 0", set64(geo, e0 + 24, 0), Refusal::BadRecord});
	ts.push_back({"geometry offset not back to back", set64(geo, e1 + 16, 4), Refusal::BadRecord});
	{
		std::vector<uint8_t> b = geo;
		b[b.size() - 1] ^= 0xFF;   // a vertex byte after the hash was taken
		ts.push_back({"geometry content differs from its hash", b, Refusal::BadRecord});
	}
	{   // the same buffer twice (duplicate) and an index buffer of one element
		std::vector<ModelGeomBuffer> dup = {bufs[0], bufs[0]};
		std::vector<uint8_t> b;
		EncodeModelGeom(dup, &b);
		ts.push_back({"geometry duplicate entry", b, Refusal::BadRecord});
		ModelGeomBuffer one;
		one.kind = VPMGEOM_KIND_INDEX32;
		one.stride = 4;
		one.bytes.assign(4, 1);
		one.hash = 1;
		EncodeModelGeom({one}, &b);
		ts.push_back({"geometry index buffer of one element", b, Refusal::BadRecord});
	}
	int n = 0;
	for(T& t : ts)
	{
		char name[64];
		sprintf_s(name, "neg_geom_%02d", n++);
		std::wstring d = Dir(name);
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modelgeom/frame000000.mgeo", t.bytes}}, "1", "0"), "%s: not written",
		      t.name);
		ExpectRefused(t.name, d, t.want, "modelgeom/frame000000.mgeo");
		CheckResult r;
		CHECK(!ParseModelGeom(t.bytes, nullptr, &r).ok() && r.refusal == t.want, "%s: ParseModelGeom %s", t.name, r.line().c_str());
	}
	{   // NNNNNN not below the frame count
		std::wstring d = Dir("neg_geom_index");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modelgeom/frame000003.mgeo", geo}}, "1", "0"), "not written");
		ExpectRefused("geometry member at frame index 3 of 3 frames", d, Refusal::BadRecord, "modelgeom/frame000003.mgeo");
	}
	{   // a geometry member while render_cities is not 1 (landmarks only): refused by the checker and the player's loader
		std::wstring d = Dir("neg_geom_cities_off");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modelgeom/frame000000.mgeo", geo}}, "0", "1"), "not written");
		ExpectRefused("a geometry member with render_cities 0", d, Refusal::BadValue, "modelgeom/frame000000.mgeo");
		Manifest m;
		CheckResult r;
		ReadManifest(d, &m, &r);
		ModelTapMembers t;
		std::string note, why;
		CHECK(!LoadModelTapMembers(d, m, &t, &note, &why) && why.find("render_cities is not 1") != std::string::npos,
		      "loader refuses: %s", why.c_str());
	}
	{   // a member file missing while the manifest names it
		std::wstring d = Dir("neg_geom_missing");
		CHECK(BuildModel(d, {{"modeltap/frame000000.mtap", img}, {"modelgeom/frame000000.mgeo", geo}}, "1", "0"), "not written");
		CHECK(DeleteFileW((d + L"/modelgeom/frame000000.mgeo").c_str()) != 0, "delete");
		ExpectRefused("geometry member listed, file missing", d, Refusal::MissingMember, "modelgeom/frame000000.mgeo");
	}
}

static void TestUnitsMembers()
{
	CHECK(std::string(MemberKindName(MemberKind::UnitsDb)) == "units_db" && std::string(MemberKindName(MemberKind::UnitsHistory)) == "units_history",
	      "kind names units_db, units_history");
	MemberKind k;
	CHECK(MemberKindFromName("units_db", &k) && k == MemberKind::UnitsDb && MemberKindFromName("units_history", &k) && k == MemberKind::UnitsHistory,
	      "kinds from names");
	CHECK(UnitsDbPath(1) == "units/gen0000000001.db" && UnitsDbPath(4294967295u) == "units/gen4294967295.db", "UnitsDbPath: %s",
	      UnitsDbPath(1).c_str());
	CHECK(MemberKindVersion(MemberKind::UnitsDb) == 1 && MemberKindVersion(MemberKind::UnitsHistory) == 1, "form versions 1");
	// the history's form: encoded and parsed back equal
	{
		const UnitsHistoryFile f = MakeHistory(4, 3);
		const std::vector<uint8_t> b = HistBytes(f);
		CHECK(b.size() == 32 + 4 * sizeof(UnitsHistoryRecord), "history bytes %u", (unsigned)b.size());
		UnitsHistoryFile g;
		CheckResult r;
		CHECK(ParseUnitsHistory(b, &g, &r).ok() && g.dbGeneration == 3 && g.flags == kUnitsHistReady && g.records.size() == 4 &&
		          memcmp(g.records.data(), f.records.data(), 4 * sizeof(UnitsHistoryRecord)) == 0,
		      "history round trip: %s", r.line().c_str());
		// one record, the span's ready state carries the controller's start
		UnitsHistoryFile one = MakeHistory(1, 3);
		CHECK(ParseUnitsHistory(HistBytes(one), nullptr, &r).ok(), "a history of one record (the controller started right before the span): %s",
		      r.line().c_str());
		CHECK(ParseUnitsDb(MakeUnitsDb(7), &r).ok(), "ParseUnitsDb: %s", r.line().c_str());
	}
	// the history's refusals, in process
	{
		struct Case
		{
			const char* name;
			std::vector<uint8_t> b;
			Refusal want;
		};
		std::vector<Case> cases;
		UnitsHistoryFile f = MakeHistory(3, 1);
		std::vector<uint8_t> b = HistBytes(f);
		std::vector<uint8_t> x = b;
		x[16] = 9;   // recordBytes
		cases.push_back({"record size 9", x, Refusal::WrongSize});
		x = b;
		x.pop_back();
		cases.push_back({"a partial record", x, Refusal::Truncated});
		x.assign(b.begin(), b.begin() + 24);
		cases.push_back({"a short head", x, Refusal::Truncated});
		x.assign(b.begin(), b.begin() + 32);
		cases.push_back({"no record", x, Refusal::BadRecord});
		x = b;
		x[28] = 1;   // head reserved
		cases.push_back({"head reserved not 0", x, Refusal::BadRecord});
		x = b;
		x[24] = 2;   // head flags bit 1
		cases.push_back({"head flags bit 1", x, Refusal::BadRecord});
		x = b;
		x[0] = 'X';
		cases.push_back({"bad magic", x, Refusal::BadMagic});
		UnitsHistoryFile g = f;
		g.records[0].flags = kUnitsHistReady;
		cases.push_back({"the first record ready", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[1].flags = 0;
		cases.push_back({"the second record not ready", HistBytes(g), Refusal::BadRecord});
		g = MakeHistory(1, 1);
		g.flags = 0;
		cases.push_back({"one record, not ready at the span", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[2].loopIter = 1;
		cases.push_back({"a loop iteration going back", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[2].nowMs = 1.0;
		cases.push_back({"a clock going back", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[1].nowMs = std::nan("");
		cases.push_back({"a clock not finite", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[1].reserved = 1;
		cases.push_back({"record reserved not 0", HistBytes(g), Refusal::BadRecord});
		g = f;
		g.records[1].flags = 3;
		cases.push_back({"record flags bit 1", HistBytes(g), Refusal::BadRecord});
		for(const Case& c : cases)
		{
			CheckResult r;
			CHECK(!ParseUnitsHistory(c.b, nullptr, &r).ok() && r.refusal == c.want, "history %s: %s, expected %s", c.name, r.line().c_str(),
			      RefusalName(c.want));
		}
		CheckResult r;
		std::vector<uint8_t> db = MakeUnitsDb(1);
		db.resize(99);
		CHECK(!ParseUnitsDb(db, &r).ok() && r.refusal == Refusal::Truncated, "a database of 99 bytes: %s", r.line().c_str());
		db = MakeUnitsDb(1);
		db[15] = 'x';
		CHECK(!ParseUnitsDb(db, &r).ok() && r.refusal == Refusal::BadMagic, "a database without the SQLite header: %s", r.line().c_str());
	}
	const std::vector<uint8_t> db1 = MakeUnitsDb(1);
	const std::vector<uint8_t> hist = HistBytes(MakeHistory(4, 1));
	const MemberList both = {{"units/gen0000000001.db", {MemberKind::UnitsDb, db1}}, {"units_history.bin", {MemberKind::UnitsHistory, hist}}};
	// positives: both members with render_units 1; the two gap notes without them; units off without them
	{
		std::wstring d = Dir("units_ok");
		CHECK(BuildUnits(d, both, "1", {}), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a bundle with units_db and units_history refused: %s", r.line().c_str());
		Manifest m;
		CHECK(ReadManifest(d, &m, &r).ok() && m.members.count("units/gen0000000001.db") && m.members["units/gen0000000001.db"].kind == MemberKind::UnitsDb &&
		          m.members.count("units_history.bin") && m.members["units_history.bin"].kind == MemberKind::UnitsHistory,
		      "manifest entries: %s", r.line().c_str());
		std::vector<uint8_t> back;
		CHECK(ReadMember(d, m, "units/gen0000000001.db", &back, &r).ok() && back == db1, "the database reads back byte-equal");
		std::vector<uint8_t> j;
		ReadBytes(d + L"/scene.json", &j);
		const std::string js(j.begin(), j.end());
		CHECK(js.find("\"format_version\": 11") != std::string::npos && js.find("\"kind\": \"units_history\"") != std::string::npos,
		      "scene.json: format_version 5 and kind units_history");
	}
	{
		std::wstring d = Dir("units_gap_notes");
		CHECK(BuildUnits(d, {}, "1", {std::string(kUnitsNoDbNote) + ": the unit scene had no database at the span's first frame",
		                              std::string(kUnitsNoHistoryNote) + ": the unit scene was not ready at the span's first frame"}),
		      "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_units 1 with both gap notes refused: %s", r.line().c_str());
	}
	{
		std::wstring d = Dir("units_off");
		CHECK(BuildUnits(d, {}, "0", {}), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_units 0 without the members refused: %s", r.line().c_str());
	}
	// negatives through the child (exit code, line, nothing written) and in process
	{
		std::wstring d = Dir("neg_units_absent");
		CHECK(BuildUnits(d, {}, "1", {}), "not written");
		ExpectRefused("render_units 1 without the database or its note", d, Refusal::MissingMember, "units/gen<NNNNNNNNNN>.db");
	}
	{
		std::wstring d = Dir("neg_units_no_history");
		CHECK(BuildUnits(d, {both[0]}, "1", {}), "not written");
		ExpectRefused("render_units 1 without the history or its note", d, Refusal::MissingMember, "units_history.bin");
	}
	{
		std::wstring d = Dir("neg_units_off");
		CHECK(BuildUnits(d, both, "0", {}), "not written");
		ExpectRefused("units members with render_units 0", d, Refusal::BadValue, "units/gen0000000001.db");
	}
	{
		std::wstring d = Dir("neg_units_two_db");
		MemberList two = both;
		two.push_back({"units/gen0000000002.db", {MemberKind::UnitsDb, MakeUnitsDb(2)}});
		CHECK(BuildUnits(d, two, "1", {}), "not written");
		ExpectRefused("two units_db members", d, Refusal::BadValue, "units/gen0000000002.db");
	}
	{
		std::wstring d = Dir("neg_units_gen");
		const MemberList other = {{"units/gen0000000002.db", {MemberKind::UnitsDb, db1}}, both[1]};
		CHECK(BuildUnits(d, other, "1", {}), "not written");
		ExpectRefused("the database's generation is not the history's", d, Refusal::BadValue, "units_history.bin");
	}
	{
		std::wstring d = Dir("neg_units_after_span");
		const MemberList late = {both[0], {"units_history.bin", {MemberKind::UnitsHistory, HistBytes(MakeHistory(8, 1))}}};
		CHECK(BuildUnits(d, late, "1", {}), "not written");
		ExpectRefused("a history frame after the span's first", d, Refusal::BadRecord, "units_history.bin");
	}
	{
		std::wstring d = Dir("neg_units_first_ready");
		UnitsHistoryFile g = MakeHistory(3, 1);
		g.records[0].flags = kUnitsHistReady;
		const MemberList bad = {both[0], {"units_history.bin", {MemberKind::UnitsHistory, HistBytes(g)}}};
		CHECK(BuildUnits(d, bad, "1", {}), "not written");
		ExpectRefused("a history whose first frame was ready", d, Refusal::BadRecord, "units_history.bin");
	}
	{
		std::wstring d = Dir("neg_units_db_magic");
		std::vector<uint8_t> x = db1;
		x[0] = 's';
		const MemberList bad = {{"units/gen0000000001.db", {MemberKind::UnitsDb, x}}, both[1]};
		CHECK(BuildUnits(d, bad, "1", {}), "not written");
		ExpectRefused("a database that is not SQLite", d, Refusal::BadMagic, "units/gen0000000001.db");
	}
	{   // paths of another form
		Writer w;
		std::wstring d = Dir("neg_units_path");
		CHECK(w.open(d).ok(), "open");
		CHECK(!w.addMember("units/gen1.db", MemberKind::UnitsDb, db1.data(), db1.size()).ok(), "the writer took units/gen1.db");
		CHECK(!w.addMember("units/gen000000000a.db", MemberKind::UnitsDb, db1.data(), db1.size()).ok(), "the writer took a hex digit");
		CHECK(!w.addMember("units/history.bin", MemberKind::UnitsHistory, hist.data(), hist.size()).ok(), "the writer took units/history.bin");
	}
	printf("ok   format version 5: the units' database and history (%u checks so far)\n", (unsigned)g_pass);
}

// ---- format version 6 (ticket 0403): particles.bin ----------------------------------------------------------------------
// A real c5r::fow::ParticleSim state (a 4 x 4 flow field, a 2048 x 1024 world, seed 5, three steps).
static std::vector<uint8_t> MakeParticleState()
{
	c5r::fow::FlowField flow;
	flow.width = flow.height = 4;
	for(int i = 0; i < 32; ++i)
		flow.xy.push_back(0.1f * (float)((i % 7) - 3));
	c5r::fow::ParticleSim sim;
	if(!sim.reset(2048.0f, 1024.0f, c5r::fow::ParticleSettings(), flow, 5).ok())
		return {};
	const float lo[2] = {-400.0f, -300.0f}, hi[2] = {400.0f, 300.0f};
	for(int k = 0; k < 3; ++k)
		sim.step(lo, hi);
	return sim.saveState();
}

static std::vector<uint8_t> PartBytes(uint32_t flags, const std::vector<uint8_t>& state, float w = 2048.0f, float h = 1024.0f, uint32_t seed = 5)
{
	FogParticlesFile f;
	f.flags = flags;
	f.seed = seed;
	f.worldW = w;
	f.worldH = h;
	f.state = state;
	std::vector<uint8_t> b;
	EncodeFogParticles(f, &b);
	return b;
}

static bool BuildParticles(const std::wstring& dir, const MemberList& members, const char* renderFowParticles, const std::vector<std::string>& notes)
{
	Writer w;
	bool ok = w.open(dir).ok();
	for(const auto& kv : members)
	{
		const c5r::Status st = w.addMember(kv.first, kv.second.first, kv.second.second.data(), kv.second.second.size());
		if(!st.ok())
		{
			printf("BuildParticles %s: %s\n", kv.first.c_str(), st.str().c_str());
			ok = false;
		}
	}
	ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok();
	ok = ok && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
	for(int k = 0; k < 3; ++k)
	{
		FrameRecord r = MakeFrame(k, "");
		r.flags &= ~kFrameDumped;
		VPShellCamera c = MakeCamera(k);
		ok = ok && w.append("frames.bin", &r, sizeof(r)).ok();
		ok = ok && w.append("cameras.bin", &c, sizeof(c)).ok();
	}
	Manifest m = BaseManifest();
	m.firstFrameId = 100;
	m.frameCount = 3;
	if(renderFowParticles)
		m.renderConfig["render_fow_particles"] = renderFowParticles;
	for(const std::string& n : notes)
		m.versionNotes.push_back(n);
	return w.finish(m).ok() && ok;
}

// ---- format version 8 (ticket 0495): forest.bin and near.bin ---------------------------------------------------------
static ForestStateFile MakeForestState(bool cut)
{
	ForestStateFile f;
	f.flags = (cut ? kForestHaveCut : 0u) | kForestIdsResolved;
	f.sessionSeed = 0x1234;
	f.layoutSeed = 0x5678;
	f.loads = 1;
	f.loadSeq = 3;
	f.forestId = 5;
	f.jungleId = 1;
	f.streamState = 99;
	f.tableGen = 7;
	f.tableW = 10;
	f.tableH = 8;
	ForestStateRecord a, b;
	a.x = 2, a.y = 1, a.kind = 1, a.layout = 4;
	b.x = 1, b.y = 3, b.kind = 4, b.layout = 19;
	f.records = {a, b};
	f.built = {{1, 3}, {2, 1}};
	f.marks = {{1, 0}, {0, 1}, {2, 1}};
	if(cut)
	{
		f.cutW = 16;
		f.cutH = 16;
		f.cutOx = 1.5f;
		f.cutOy = -2.0f;
		f.cutGeneration = 12;
		f.cutCells.assign(256, 0);
		f.cutCells[17] = 3;
	}
	return f;
}

static std::vector<uint8_t> MakeNearBytes(uint32_t tx, uint32_t ty)
{
	// a NearCaches state's head as the parser reads it ("C5NC", version 1, tile count) and some body
	std::vector<uint8_t> s = {'C', '5', 'N', 'C', 1, 0, 0, 0};
	const uint32_t tiles = tx * ty;
	for(int i = 0; i < 4; ++i)
		s.push_back((uint8_t)(tiles >> (8 * i)));
	s.resize(s.size() + 40, 0);
	NearPairsFile f;
	f.tilesX = tx;
	f.tilesY = ty;
	f.state = s;
	std::vector<uint8_t> b;
	EncodeNearPairs(f, &b);
	return b;
}

static void TestForestNearMembers()
{
	MemberKind k;
	CHECK(kFormatVersion == 11 && MemberKindFromName("forest_state", &k) && k == MemberKind::ForestState &&
	          MemberKindFromName("near_pairs", &k) && k == MemberKind::NearPairs && MemberKindVersion(MemberKind::ForestState) == 1,
	      "format version 8, kinds forest_state and near_pairs");
	CHECK(strstr(FormatVersionChange(8), "caller 4") && strstr(FormatVersionChange(8), "forest.bin") && strstr(FormatVersionChange(8), "near.bin"),
	      "version 8's line names its changes");
	std::vector<uint8_t> fb, fbNoCut;
	EncodeForestState(MakeForestState(true), &fb);
	EncodeForestState(MakeForestState(false), &fbNoCut);
	{
		ForestStateFile g;
		CheckResult r;
		const ForestStateFile want = MakeForestState(true);
		CHECK(fb.size() == kForestStateFixedBytes + 2 * 16 + 2 * 8 + 3 * 8 + 256, "forest.bin %u bytes", (unsigned)fb.size());
		CHECK(ParseForestState(fb, &g, &r).ok() && g.records.size() == 2 && g.records[1].layout == 19 && g.built == want.built &&
		          g.marks == want.marks && g.cutCells == want.cutCells && g.cutOx == 1.5f && g.sessionSeed == 0x1234 && g.streamState == 99 &&
		          g.forestId == 5 && g.cutGeneration == 12,
		      "forest.bin round trip: %s", r.line().c_str());
		CHECK(ParseForestState(fbNoCut, &g, &r).ok() && g.cutCells.empty() && g.cutW == 0, "forest.bin without the cut: %s", r.line().c_str());
	}
	{   // negatives of the form
		struct Case
		{
			const char* name;
			std::vector<uint8_t> b;
			Refusal want;
		};
		std::vector<Case> cases;
		auto with = [&](void (*f)(ForestStateFile&)) {
			ForestStateFile x = MakeForestState(true);
			f(x);
			std::vector<uint8_t> b;
			EncodeForestState(x, &b);
			return b;
		};
		auto set32 = [](std::vector<uint8_t> b, size_t at, uint32_t v) {
			memcpy(&b[at], &v, 4);
			return b;
		};
		cases.push_back({"short", std::vector<uint8_t>(fb.begin(), fb.begin() + 60), Refusal::Truncated});
		cases.push_back({"the cut cut short", std::vector<uint8_t>(fb.begin(), fb.end() - 1), Refusal::Truncated});
		std::vector<uint8_t> extra = fb;
		extra.push_back(0);
		cases.push_back({"a byte after the cut", extra, Refusal::WrongSize});
		cases.push_back({"magic", set32(fb, 0, 0x58585858u), Refusal::BadMagic});
		cases.push_back({"form version 2", set32(fb, 4, 2), Refusal::BadVersion});
		cases.push_back({"flags bit 2", set32(fb, 16, 7), Refusal::BadRecord});
		cases.push_back({"reserved", set32(fb, 96, 1), Refusal::BadRecord});
		cases.push_back({"session seed 0", with([](ForestStateFile& x) { x.sessionSeed = 0; }), Refusal::BadRecord});
		cases.push_back({"kind 5", with([](ForestStateFile& x) { x.records[0].kind = 5; }), Refusal::BadRecord});
		cases.push_back({"layout 20", with([](ForestStateFile& x) { x.records[0].layout = 20; }), Refusal::BadRecord});
		cases.push_back({"records out of order", with([](ForestStateFile& x) { std::swap(x.records[0], x.records[1]); }), Refusal::BadRecord});
		cases.push_back({"a built plot that is no record", with([](ForestStateFile& x) { x.built[0] = {9, 9}; }), Refusal::BadRecord});
		cases.push_back({"a built plot twice", with([](ForestStateFile& x) { x.built[1] = x.built[0]; }), Refusal::BadRecord});
		cases.push_back({"a mark at a negative tile", with([](ForestStateFile& x) { x.marks[0].first = -1; }), Refusal::BadRecord});
		cases.push_back({"marks out of order", with([](ForestStateFile& x) { std::swap(x.marks[0], x.marks[1]); }), Refusal::BadRecord});
		cases.push_back({"a cut of 0 cells", with([](ForestStateFile& x) { x.cutW = 0; x.cutCells.clear(); }), Refusal::BadRecord});
		cases.push_back({"a cut origin not finite", with([](ForestStateFile& x) { x.cutOx = std::nanf(""); }), Refusal::BadRecord});
		cases.push_back({"a cut without the flag", set32(fb, 16, kForestIdsResolved), Refusal::BadRecord});
		for(const Case& c : cases)
		{
			CheckResult r;
			CHECK(!ParseForestState(c.b, nullptr, &r).ok() && r.refusal == c.want, "forest.bin %s: %s, expected %s", c.name, r.line().c_str(),
			      RefusalName(c.want));
		}
	}
	const std::vector<uint8_t> nb = MakeNearBytes(4, 3);
	{
		NearPairsFile g;
		CheckResult r;
		CHECK(ParseNearPairs(nb, &g, &r).ok() && g.tilesX == 4 && g.tilesY == 3 && g.state.size() == 52, "near.bin round trip: %s", r.line().c_str());
		auto set32 = [](std::vector<uint8_t> b, size_t at, uint32_t v) {
			memcpy(&b[at], &v, 4);
			return b;
		};
		struct Case
		{
			const char* name;
			std::vector<uint8_t> b;
			Refusal want;
		} cases[] = {
		    {"short", std::vector<uint8_t>(nb.begin(), nb.begin() + 20), Refusal::Truncated},
		    {"flags bit 1", set32(nb, 16, 2), Refusal::BadRecord},
		    {"the images flag without images", set32(nb, 16, kNearHasImages), Refusal::Truncated},
		    {"tiles 0", set32(nb, 20, 0), Refusal::BadRecord},
		    {"stateBytes", set32(nb, 28, 51), Refusal::WrongSize},
		    {"the state's magic", set32(nb, 32, 0x58585858u), Refusal::BadMagic},
		    {"the state's version", set32(nb, 36, 2), Refusal::BadVersion},
		    {"the state's tile count", set32(nb, 40, 13), Refusal::BadRecord},
		};
		for(const Case& c : cases)
			CHECK(!ParseNearPairs(c.b, nullptr, &r).ok() && r.refusal == c.want, "near.bin %s: %s, expected %s", c.name, r.line().c_str(),
			      RefusalName(c.want));
		// ticket 0495: the texels of pairs whose content was behind their tile (kNearHasImages)
		CHECK(NearImageBytes(512) == 1398100 && NearImageBytes(256) == 349524 && NearImageBytes(64) == 21844 && NearImageBytes(100) == 0,
		      "NearImageBytes");
		NearPairsFile wi = g;
		NearPairImage a, b2;
		a.tile = 2, a.size = 64, a.second = 0, a.texels.assign((size_t)NearImageBytes(64), 7);
		b2.tile = 2, b2.size = 64, b2.second = 1, b2.texels.assign((size_t)NearImageBytes(64), 9);
		wi.images = {a, b2};
		std::vector<uint8_t> wb;
		EncodeNearPairs(wi, &wb);
		NearPairsFile back;
		CHECK(ParseNearPairs(wb, &back, &r).ok() && back.images.size() == 2 && back.images[1].second == 1 && back.images[1].texels == b2.texels &&
		          back.state == g.state,
		      "near.bin with images round trip: %s", r.line().c_str());
		auto withImg = [&](void (*f)(NearPairsFile&)) {
			NearPairsFile x = wi;
			f(x);
			std::vector<uint8_t> o;
			EncodeNearPairs(x, &o);
			return o;
		};
		std::vector<uint8_t> extra = wb;
		extra.push_back(0);
		struct Case ic[] = {
		    {"an image's tile out of range", withImg([](NearPairsFile& x) { x.images[0].tile = 12; }), Refusal::BadRecord},
		    {"an image of size 100", withImg([](NearPairsFile& x) { x.images[0].size = 100; }), Refusal::BadRecord},
		    {"an image's texture 2", withImg([](NearPairsFile& x) { x.images[1].second = 2; }), Refusal::BadRecord},
		    {"an image of the wrong length", withImg([](NearPairsFile& x) { x.images[0].texels.pop_back(); }), Refusal::BadRecord},
		    {"images out of order", withImg([](NearPairsFile& x) { std::swap(x.images[0], x.images[1]); }), Refusal::BadRecord},
		    {"an image cut short", std::vector<uint8_t>(wb.begin(), wb.end() - 1), Refusal::Truncated},
		    {"a byte after the last image", extra, Refusal::WrongSize},
		};
		for(const Case& c : ic)
			CHECK(!ParseNearPairs(c.b, nullptr, &r).ok() && r.refusal == c.want, "near.bin %s: %s, expected %s", c.name, r.line().c_str(),
			      RefusalName(c.want));
	}
	// cross-checks through the checker: members against render_forest / render_bake_near, the gap notes
	struct Bundle
	{
		const char* name;
		MemberList members;
		const char* forest;
		const char* nearKey;
		std::vector<std::string> notes;
		Refusal want;
	} bundles[] = {
	    {"forest_near_ok", {{"forest.bin", {MemberKind::ForestState, fb}}, {"near.bin", {MemberKind::NearPairs, nb}}}, "1", "1", {}, Refusal::None},
	    {"forest_off_ok", {}, "0", "0", {}, Refusal::None},
	    {"forest_gap_ok", {}, "1", "1", {"gap: forest: no state", "gap: near pairs: no state"}, Refusal::None},
	    {"neg_forest_missing", {{"near.bin", {MemberKind::NearPairs, nb}}}, "1", "1", {}, Refusal::MissingMember},
	    {"neg_near_missing", {{"forest.bin", {MemberKind::ForestState, fb}}}, "1", "1", {}, Refusal::MissingMember},
	    {"neg_forest_while_off", {{"forest.bin", {MemberKind::ForestState, fb}}}, "0", "0", {}, Refusal::BadValue},
	    {"neg_near_while_off", {{"near.bin", {MemberKind::NearPairs, nb}}}, "0", "0", {}, Refusal::BadValue},
	    {"neg_forest_key", {}, "2", "0", {}, Refusal::BadValue},
	};
	for(const Bundle& b : bundles)
	{
		std::wstring d = Dir(b.name);
		Writer w;
		bool ok = w.open(d).ok();
		for(const auto& kv : b.members)
			ok = ok && w.addMember(kv.first, kv.second.first, kv.second.second.data(), kv.second.second.size()).ok();
		ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok() && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
		for(int f = 0; f < 3; ++f)
		{
			FrameRecord r = MakeFrame(f, "");
			r.flags &= ~kFrameDumped;
			VPShellCamera c = MakeCamera(f);
			ok = ok && w.append("frames.bin", &r, sizeof(r)).ok() && w.append("cameras.bin", &c, sizeof(c)).ok();
		}
		Manifest m = BaseManifest();
		m.firstFrameId = 100;
		m.frameCount = 3;
		m.renderConfig["render_forest"] = b.forest;
		m.renderConfig["render_bake_near"] = b.nearKey;
		m.versionNotes = b.notes;
		CHECK(w.finish(m).ok() && ok, "%s not written", b.name);
		CheckResult r;
		if(b.want == Refusal::None)
			CHECK(CheckBundle(d, &r).ok(), "%s refused: %s", b.name, r.line().c_str());
		else
		{
			CHECK(!CheckBundle(d, &r).ok() && r.refusal == b.want, "%s: %s, expected %s", b.name, r.line().c_str(), RefusalName(b.want));
			int code = -1;
			std::string out;
			CHECK(RunCheckChild(d, &code, &out) && code == RefusalExitCode(b.want), "%s child: exit %d, %s", b.name, code, out.c_str());
		}
	}
	printf("ok   format version 8: forest.bin, near.bin (%u checks so far)\n", (unsigned)g_pass);
}

// Ticket 4852 (epic 0528): the farm producer's input lives in render_config (render_farmfields, render_farmfields_state),
// not in a member: the format stays 8. The checker's cross-checks, the state surviving the manifest's JSON byte for byte,
// and the rule an older reader relies on: a render_config key it does not know is not refused.
static void TestFarmConfig()
{
	CHECK(kFormatVersion == 11, "the farm keys leave the format at 8");
	const std::string state = "fv1 seed=12345 farm=3 wheat=-1 map=128,80 world=0x0p+0,0x0p+0,0x1.fffp+12,0x1.4ffp+12 "
	                          "farms=1.1.2.3.0,2.1.1.4.1,5.2.3.1.0";
	struct Bundle
	{
		const char* name;
		const char* on;           // render_farmfields, nullptr: absent
		const char* st;           // render_farmfields_state, nullptr: absent
		std::vector<std::string> notes;
		const char* extraKey;     // another render_config key (an older reader's view of a newer key), nullptr: none
		Refusal want;
	} bundles[] = {
	    {"farm_ok", "1", state.c_str(), {}, nullptr, Refusal::None},
	    {"farm_off_ok", "0", nullptr, {}, nullptr, Refusal::None},
	    {"farm_absent_ok", nullptr, nullptr, {}, nullptr, Refusal::None},   // every bundle before 4852
	    {"farm_gap_ok", "1", nullptr, {"gap: farm fields: no production in force"}, nullptr, Refusal::None},
	    {"farm_unknown_key_ok", "1", state.c_str(), {}, "render_farmfields_future_key", Refusal::None},
	    {"neg_farm_key", "2", nullptr, {}, nullptr, Refusal::BadValue},
	    {"neg_farm_state_while_off", "0", state.c_str(), {}, nullptr, Refusal::BadValue},
	    {"neg_farm_state_absent_key", nullptr, state.c_str(), {}, nullptr, Refusal::BadValue},
	    {"neg_farm_state_form", "1", "fv2 seed=1", {}, nullptr, Refusal::BadValue},
	    {"neg_farm_state_missing", "1", nullptr, {}, nullptr, Refusal::MissingMember},
	};
	for(const Bundle& b : bundles)
	{
		std::wstring d = Dir(b.name);
		Writer w;
		bool ok = w.open(d).ok();
		ok = ok && w.appendStart("frames.bin", MemberKind::Frames).ok() && w.appendStart("cameras.bin", MemberKind::CameraBlocks).ok();
		for(int f = 0; f < 3; ++f)
		{
			FrameRecord r = MakeFrame(f, "");
			r.flags &= ~kFrameDumped;
			VPShellCamera c = MakeCamera(f);
			ok = ok && w.append("frames.bin", &r, sizeof(r)).ok() && w.append("cameras.bin", &c, sizeof(c)).ok();
		}
		Manifest m = BaseManifest();
		m.firstFrameId = 100;
		m.frameCount = 3;
		if(b.on)
			m.renderConfig["render_farmfields"] = b.on;
		if(b.st)
			m.renderConfig["render_farmfields_state"] = b.st;
		if(b.extraKey)
			m.renderConfig[b.extraKey] = "anything";
		m.versionNotes = b.notes;
		CHECK(w.finish(m).ok() && ok, "%s not written", b.name);
		CheckResult r;
		if(b.want == Refusal::None)
		{
			CHECK(CheckBundle(d, &r).ok(), "%s refused: %s", b.name, r.line().c_str());
			Manifest back;
			CHECK(ReadManifest(d, &back, &r).ok(), "%s: manifest not read back: %s", b.name, r.line().c_str());
			if(b.st)
				CHECK(back.renderConfig["render_farmfields_state"] == b.st, "%s: the state does not survive the JSON byte for byte",
				      b.name);
		}
		else
		{
			CHECK(!CheckBundle(d, &r).ok() && r.refusal == b.want, "%s: %s, expected %s", b.name, r.line().c_str(), RefusalName(b.want));
			int code = -1;
			std::string out;
			CHECK(RunCheckChild(d, &code, &out) && code == RefusalExitCode(b.want), "%s child: exit %d, %s", b.name, code, out.c_str());
		}
	}
	printf("ok   ticket 4852: render_farmfields and its state in render_config (%u checks so far)\n", (unsigned)g_pass);
}

static void TestParticlesMember()
{
	CHECK(kFormatVersion == 11 && std::string(MemberKindName(MemberKind::FogParticles)) == "fog_particles", "format version 8, kind fog_particles");
	MemberKind k;
	CHECK(MemberKindFromName("fog_particles", &k) && k == MemberKind::FogParticles && MemberKindVersion(MemberKind::FogParticles) == 1,
	      "kind from its name, form version 1");
	const std::vector<uint8_t> state = MakeParticleState();
	CHECK(state.size() > 200 && memcmp(state.data(), "C5FP", 4) == 0, "the sim's state (%u bytes)", (unsigned)state.size());
	const std::vector<uint8_t> spawned = PartBytes(kFogParticlesSpawned, state), fresh = PartBytes(0, {}, 0, 0);
	// round trips
	{
		CHECK(spawned.size() == kFogParticlesFixedBytes + state.size() && fresh.size() == kFogParticlesFixedBytes, "sizes %u, %u",
		      (unsigned)spawned.size(), (unsigned)fresh.size());
		FogParticlesFile f;
		CheckResult r;
		CHECK(ParseFogParticles(spawned, &f, &r).ok() && f.flags == kFogParticlesSpawned && f.seed == 5 && f.worldW == 2048.0f &&
		          f.worldH == 1024.0f && f.state == state && f.particles > 0 && f.cellsX > 0 && f.cellsY > 0,
		      "spawned round trip: %s (%u particles, %d x %d)", r.line().c_str(), f.particles, f.cellsX, f.cellsY);
		CHECK(ParseFogParticles(fresh, &f, &r).ok() && f.flags == 0 && f.state.empty() && f.particles == 0, "not spawned round trip: %s",
		      r.line().c_str());
		// the state the parser accepts loads into a sim of the same flow field (the player's own check)
		c5r::fow::FlowField flow;
		flow.width = flow.height = 4;
		for(int i = 0; i < 32; ++i)
			flow.xy.push_back(0.1f * (float)((i % 7) - 3));
		c5r::fow::ParticleSim sim;
		CHECK(sim.reset(2048.0f, 1024.0f, c5r::fow::ParticleSettings(), flow, 1).ok() && sim.loadState(state.data(), state.size()).ok() &&
		          sim.saveState() == state,
		      "the parsed state does not load back byte-equal");
	}
	// refusals in process
	{
		struct Case
		{
			const char* name;
			std::vector<uint8_t> b;
			Refusal want;
		};
		std::vector<Case> cases;
		std::vector<uint8_t> x = spawned;
		x[0] = 'X';
		cases.push_back({"bad magic", x, Refusal::BadMagic});
		x = spawned;
		x[4] = 2;
		cases.push_back({"form version 2", x, Refusal::BadVersion});
		x.assign(spawned.begin(), spawned.begin() + 30);
		cases.push_back({"a short head", x, Refusal::Truncated});
		x = spawned;
		x[16] = 3;   // flags bit 1
		cases.push_back({"flags bit 1", x, Refusal::BadRecord});
		x = spawned;
		x[36] = 1;   // reserved
		cases.push_back({"reserved not 0", x, Refusal::BadRecord});
		cases.push_back({"a world not finite", PartBytes(kFogParticlesSpawned, state, std::nanf(""), 1024.0f), Refusal::BadRecord});
		cases.push_back({"a spawned world of 0", PartBytes(kFogParticlesSpawned, state, 0.0f, 1024.0f), Refusal::BadRecord});
		cases.push_back({"spawned without a state", PartBytes(kFogParticlesSpawned, {}), Refusal::BadRecord});
		cases.push_back({"not spawned with a state", PartBytes(0, state), Refusal::BadRecord});
		x = spawned;
		x.pop_back();
		cases.push_back({"stateBytes past the end", x, Refusal::WrongSize});
		x = spawned;
		x.push_back(0);
		cases.push_back({"a byte after the state", x, Refusal::WrongSize});
		std::vector<uint8_t> s = state;
		s[0] = 'Y';
		cases.push_back({"the state's magic", PartBytes(kFogParticlesSpawned, s), Refusal::BadMagic});
		s = state;
		s[4] = 9;
		cases.push_back({"the state's version", PartBytes(kFogParticlesSpawned, s), Refusal::BadVersion});
		s.assign(state.begin(), state.begin() + 20);
		cases.push_back({"the state's fields cut", PartBytes(kFogParticlesSpawned, s), Refusal::Truncated});
		s = state;
		s.resize(s.size() - 1);
		cases.push_back({"the state's last particle cut", PartBytes(kFogParticlesSpawned, s), Refusal::Truncated});
		s = state;
		s.push_back(7);
		cases.push_back({"a byte after the state's last cell", PartBytes(kFogParticlesSpawned, s), Refusal::WrongSize});
		// the grid size (cx) sits after magic, version, settings, flow w / h / hash, frames
		const size_t cxAt = 8 + sizeof(c5r::fow::ParticleSettings) + 4 + 4 + 8 + 4;
		s = state;
		memset(&s[cxAt], 0, 4);
		cases.push_back({"the state's grid 0 wide", PartBytes(kFogParticlesSpawned, s), Refusal::BadRecord});
		s = state;
		const uint32_t big = 33;   // the first cell's count (after cx, cy, gx, gy, rand, lost, steps)
		memcpy(&s[cxAt + 7 * 4], &big, 4);
		cases.push_back({"a cell of 33 particles", PartBytes(kFogParticlesSpawned, s), Refusal::BadRecord});
		for(const Case& c : cases)
		{
			CheckResult r;
			CHECK(!ParseFogParticles(c.b, nullptr, &r).ok() && r.refusal == c.want, "particles %s: %s, expected %s", c.name, r.line().c_str(),
			      RefusalName(c.want));
		}
	}
	const MemberList one = {{"particles.bin", {MemberKind::FogParticles, spawned}}};
	// positives: the member with render_fow_particles 1; the gap note without it; 0 or no key without it
	{
		std::wstring d = Dir("particles_ok");
		CHECK(BuildParticles(d, one, "1", {}), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a bundle with particles.bin refused: %s", r.line().c_str());
		Manifest m;
		CHECK(ReadManifest(d, &m, &r).ok() && m.members.count("particles.bin") && m.members["particles.bin"].kind == MemberKind::FogParticles,
		      "manifest entry: %s", r.line().c_str());
		std::vector<uint8_t> back;
		CHECK(ReadMember(d, m, "particles.bin", &back, &r).ok() && back == spawned, "particles.bin reads back byte-equal");
		std::vector<uint8_t> j;
		ReadBytes(d + L"/scene.json", &j);
		const std::string js(j.begin(), j.end());
		CHECK(js.find("\"format_version\": 11") != std::string::npos && js.find("\"kind\": \"fog_particles\"") != std::string::npos &&
		          js.find("\"render_fow_particles\": \"1\"") != std::string::npos,
		      "scene.json: format_version 8, kind fog_particles, render_fow_particles");
	}
	{
		std::wstring d = Dir("particles_fresh_ok");
		CHECK(BuildParticles(d, {{"particles.bin", {MemberKind::FogParticles, fresh}}}, "1", {}), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "a not-spawned particles.bin refused: %s", r.line().c_str());
	}
	{
		std::wstring d = Dir("particles_gap_note");
		CHECK(BuildParticles(d, {}, "1", {std::string(kFogParticlesNote) + ": the server drew the FOWLevel-3 particles (3 particle frames)"}),
		      "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_fow_particles 1 with the gap note refused: %s", r.line().c_str());
	}
	for(const char* v : {"0", (const char*)nullptr})
	{
		std::wstring d = Dir(v ? "particles_off" : "particles_no_key");
		CHECK(BuildParticles(d, {}, v, {}), "not written");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "render_fow_particles %s without the member refused: %s", v ? v : "(absent)", r.line().c_str());
	}
	// negatives through the child (exit code, line, nothing written)
	{
		std::wstring d = Dir("neg_particles_absent");
		CHECK(BuildParticles(d, {}, "1", {}), "not written");
		ExpectRefused("render_fow_particles 1 without particles.bin or its note", d, Refusal::MissingMember, "particles.bin");
	}
	{
		std::wstring d = Dir("neg_particles_off");
		CHECK(BuildParticles(d, one, "0", {}), "not written");
		ExpectRefused("particles.bin with render_fow_particles 0", d, Refusal::BadValue, "particles.bin");
	}
	{
		std::wstring d = Dir("neg_particles_no_key");
		CHECK(BuildParticles(d, one, nullptr, {}), "not written");
		ExpectRefused("particles.bin without render_fow_particles", d, Refusal::BadValue, "particles.bin");
	}
	{
		std::wstring d = Dir("neg_particles_value");
		CHECK(BuildParticles(d, {}, "2", {}), "not written");
		ExpectRefused("render_fow_particles 2", d, Refusal::BadValue, "");
	}
	{
		std::wstring d = Dir("neg_particles_magic");
		std::vector<uint8_t> x = spawned;
		x[kFogParticlesFixedBytes] = 'Z';
		CHECK(BuildParticles(d, {{"particles.bin", {MemberKind::FogParticles, x}}}, "1", {}), "not written");
		ExpectRefused("a particle state without C5FP", d, Refusal::BadMagic, "particles.bin");
	}
	{   // another path
		Writer w;
		std::wstring d = Dir("neg_particles_path");
		CHECK(w.open(d).ok(), "open");
		CHECK(!w.addMember("fog/particles.bin", MemberKind::FogParticles, spawned.data(), spawned.size()).ok(), "the writer took fog/particles.bin");
	}
	printf("ok   format version 6: particles.bin (%u checks so far)\n", (unsigned)g_pass);
}

static void TestRoundTrip()
{
	std::wstring dir = Dir("full");
	Manifest wrote;
	CHECK(BuildFull(dir, Full(), &wrote), "the full bundle was not written");
	CheckResult res;
	c5r::Status st = CheckBundle(dir, &res);
	CHECK(st.ok() && res.refusal == Refusal::None, "CheckBundle on the full bundle: %s", res.line().c_str());
	int code = -1;
	std::string out;
	CHECK(RunCheckChild(dir, &code, &out) && code == 0 && out.find("scenebundle: OK") != std::string::npos,
	      "--check on the full bundle: exit %d, %s", code, out.c_str());
	CHECK(!SnapDir(dir).count(".partial/") && !SnapDir(dir).count("scene.json.tmp"), ".partial or scene.json.tmp left over");

	Manifest m;
	st = ReadManifest(dir, &m, &res);
	CHECK(st.ok(), "ReadManifest: %s", res.line().c_str());
	CHECK(ManifestToJson(m) == ManifestToJson(wrote), "the manifest read back differs from the one written");
	CHECK(m.members.size() == 18, "%u members, 18 written (water_frames.bin: ticket 0622)", (unsigned)m.members.size());
	CHECK(m.saveName == wrote.saveName && m.adapterDescription == wrote.adapterDescription && m.pins == wrote.pins &&
	          m.recorderKeys == wrote.recorderKeys && m.renderConfig == wrote.renderConfig && m.counters == wrote.counters &&
	          m.dumpedFrames == wrote.dumpedFrames && m.firstFrameId == 100 && m.frameCount == 3 && m.mapH == 3 &&
	          m.counters.size() == 5 && m.counters["record_errors"] == 2,
	      "manifest fields differ after the round trip");
	{
		std::vector<uint8_t> raw;
		ReadBytes(dir + L"/scene.json", &raw);
		std::string js(raw.begin(), raw.end());
		CHECK(js.find("\"i0_lock\": {") != std::string::npos && js.find("\"commit\": \"26234371e\"") != std::string::npos,
		      "pins are not nested one level in scene.json: %s", js.c_str());
		CHECK(js.find('\r') == std::string::npos && js.find('\\') != std::string::npos, "scene.json line ends or escapes");
	}
	// every member, read and parsed, equals what was written
	std::vector<uint8_t> b;
	CHECK(ReadMember(dir, m, "heightfield/load0001.c5hf", &b, &res).ok(), "%s", res.line().c_str());
	hf::HeightGrid g, want;
	CHECK(ParseHeightfield(b, &g, &res).ok(), "heightfield: %s", res.line().c_str());
	std::vector<uint8_t> src = MakeHeightfield(true);
	CHECK(hf::readTap(src.data(), src.size(), want).ok() && g.h == want.h && g.mat == want.mat && g.mapW == 2 &&
	          g.mapH == 3 && g.wrapX && g.mapSeed == 12345 && b == src,
	      "heightfield round trip (C5HF through readTap)");

	CHECK(ReadMember(dir, m, "water/load0001.wtap", &b, &res).ok(), "%s", res.line().c_str());
	WaterTapFile wt;
	WaterParts wp = MakeWaterParts();
	CHECK(ParseWaterTap(b, &wt, &res).ok(), "water tap: %s", res.line().c_str());
	CHECK(memcmp(&wt.block, &wp.block, sizeof(wt.block)) == 0 && wt.haveCam == 1 && wt.gridSeq == 3 && wt.samplesX == 128 &&
	          wt.samplesY == 192 && wt.imageOffset == kWaterTapFixedBytes && wt.imageBytes == wp.image.size() &&
	          memcmp(b.data() + wt.imageOffset, wp.image.data(), wp.image.size()) == 0,
	      "water tap round trip");

	std::string ovlPath;
	for(const auto& kv : m.members)
		if(kv.second.kind == MemberKind::OverlayTexture)
			ovlPath = kv.first;
	CHECK(!ovlPath.empty() && ReadMember(dir, m, ovlPath, &b, &res).ok(), "overlay member: %s", res.line().c_str());
	OverlayFile of;
	CHECK(ParseOverlay(b, &of, &res).ok(), "overlay: %s", res.line().c_str());
	CHECK(b.size() == kOverlayTableBytes + kOverlayTexelBytes && b.size() == 22369748 && of.mips == 12 &&
	          of.offset[0] == 128 && of.rowPitch[0] == 8192 && of.rowPitch[11] == 4 && of.offset[11] == 22369748 - 4 &&
	          memcmp(b.data() + of.offset[0], OverlayTexels().data(), OverlayTexels().size()) == 0,
	      "overlay round trip (%u bytes)", (unsigned)b.size());

	CHECK(ReadMember(dir, m, "feed.bin", &b, &res).ok(), "%s", res.line().c_str());
	std::vector<FeedRecord> recs;
	std::vector<std::vector<SrvFeed::PlotRecord>> tables;
	std::vector<uint8_t> feedSrc = MakeFeed(&tables);
	CHECK(b == feedSrc, "feed.bin appended differs from the encoded stream");
	CHECK(ParseFeed(b, &recs, &res).ok() && recs.size() == 4, "feed: %s (%u records)", res.line().c_str(), (unsigned)recs.size());
	if(recs.size() == 4)
	{
		SrvFeed::Batch bt = MakeBatch();
		CHECK(recs[0].type == kFeedPlots && recs[0].full && recs[0].caller == 1 && recs[0].loopIter == 5 &&
		          recs[0].plots.size() == 12 && memcmp(recs[0].plots.data(), tables[0].data(), 12 * 20) == 0,
		      "feed record 0 (PLOTS full)");
		CHECK(recs[1].type == kFeedDrain && recs[1].nextFrameId == 100 && recs[1].batch.events.size() == 3 &&
		          memcmp(recs[1].batch.events.data(), bt.events.data(), 3 * 32) == 0 && recs[1].batch.changedPlots == bt.changedPlots &&
		          recs[1].batch.width == 4 && recs[1].batch.height == 3 && recs[1].batch.snapshotGen == 1,
		      "feed record 1 (DRAIN)");
		CHECK(recs[2].type == kFeedPlots && !recs[2].full && recs[2].caller == 2 &&
		          memcmp(recs[2].plots.data(), tables[1].data(), 12 * 20) == 0,
		      "feed record 2 (PLOTS diff applied)");
		CHECK(recs[3].type == kFeedDrain && recs[3].nextFrameId == -1 && recs[3].batch.events.empty(), "feed record 3");
	}

	CHECK(ReadMember(dir, m, "frames.bin", &b, &res).ok(), "%s", res.line().c_str());
	std::vector<FrameRecord> fr;
	CHECK(ParseFrames(b, &fr, &res).ok() && fr.size() == 3, "frames: %s", res.line().c_str());
	std::string ovlSha = m.members[ovlPath].sha256;
	for(int k = 0; k < 3 && fr.size() == 3; ++k)
	{
		FrameRecord e = MakeFrame(k, ovlSha);
		CHECK(memcmp(&fr[k], &e, sizeof(e)) == 0, "frame record %d differs", k);
	}
	CHECK(ReadMember(dir, m, "cameras.bin", &b, &res).ok(), "%s", res.line().c_str());
	std::vector<VPShellCamera> cams;
	CHECK(ParseCameras(b, &cams, &res).ok() && cams.size() == 3, "cameras: %s", res.line().c_str());
	for(int k = 0; k < 3 && cams.size() == 3; ++k)
	{
		VPShellCamera e = MakeCamera(k);
		CHECK(memcmp(&cams[k], &e, sizeof(e)) == 0, "camera block %d differs", k);
	}
	CHECK(ReadMember(dir, m, "cameras/default.vpcam", &b, &res).ok() && ParseCameras(b, &cams, &res).ok() && cams.size() == 1,
	      "named camera block: %s", res.line().c_str());
	CHECK(ReadMember(dir, m, "water_clock.bin", &b, &res).ok(), "%s", res.line().c_str());
	WaterClockFile wc, wce = MakeClock();
	CHECK(ParseWaterClock(b, &wc, &res).ok() && wc.clockStartMs == wce.clockStartMs && wc.waveLastMs == wce.waveLastMs &&
	          wc.lastNowMs == wce.lastNowMs && wc.waves.size() == 5 && memcmp(wc.waves.data(), wce.waves.data(), 5 * 16) == 0,
	      "water clock round trip: %s", res.line().c_str());
	CHECK(ReadMember(dir, m, "fog_clock.bin", &b, &res).ok(), "%s", res.line().c_str());
	FogClockFile fc, fce = MakeFogClock();
	CHECK(b.size() == kFogClockBytes && m.members["fog_clock.bin"].kind == MemberKind::FogClock && ParseFogClock(b, &fc, &res).ok() &&
	          fc.clockS == fce.clockS && fc.clockLastMs == fce.clockLastMs && memcmp(b.data(), "C5FC", 4) == 0,
	      "fog clock round trip: %s", res.line().c_str());
	CHECK(fr.size() == 3 && (fr[1].flags & kFrameFog) && !(fr[0].flags & kFrameFog), "frames.bin's fog flag (bit 3) round trip");
	CHECK(ReadMember(dir, m, "dumps/102.bmp", &b, &res).ok(), "%s", res.line().c_str());
	uint32_t bw = 0, bh = 0;
	CHECK(ParseDumpColour(b, &bw, &bh, &res).ok() && bw == 8 && bh == 4 && b == MakeBmp(), "colour dump round trip");
	CHECK(ReadMember(dir, m, "dumps/100.depth.f32", &b, &res).ok() && b == MakeDepth(), "depth dump round trip");
	CHECK(ReadMember(dir, m, "refs/default.png", &b, &res).ok() && b == MakePng(), "stock shot round trip");
	CHECK(ReadMember(dir, m, "refs/default.export/zip.xml", &b, &res).ok() && b == Str("<xml/>"), "stock export round trip");
	CHECK(!ReadMember(dir, m, "refs/none.png", &b, &res).ok() && res.refusal == Refusal::MissingMember,
	      "ReadMember of an unlisted path: %s", res.line().c_str());
	printf("ok   round trip of every member kind (%u members, %u checks so far)\n", (unsigned)m.members.size(), (unsigned)g_pass);
}

static void TestManifestRules()
{
	// keys sorted, re-read equal; JSON escapes; refusals of ParseManifest in process
	Manifest m = BaseManifest();
	std::string js = ManifestToJson(m);
	Manifest back;
	CheckResult res;
	CHECK(ParseManifest(js, &back, &res).ok() && ManifestToJson(back) == js, "manifest text round trip: %s", res.line().c_str());
	struct Case
	{
		const char* name;
		const char* from;
		const char* to;
		Refusal want;
	} cases[] = {
		{"bad JSON", "{", "[", Refusal::BadJson},
		{"trailing comma", "\"turn\": 9}", "\"turn\": 9,}", Refusal::BadJson},
		{"older version (a version-3 bundle, issue 4457)", "\"format_version\": 11", "\"format_version\": 3", Refusal::OlderVersion},
		{"older version (a version-2 bundle, issue 0364)", "\"format_version\": 11", "\"format_version\": 2", Refusal::OlderVersion},
		{"older version (a version-1 bundle, issue 3911)", "\"format_version\": 11", "\"format_version\": 1", Refusal::OlderVersion},
		{"older version 0", "\"format_version\": 11", "\"format_version\": 0", Refusal::OlderVersion},
		{"newer version", "\"format_version\": 11", "\"format_version\": 12", Refusal::NewerVersion},
		{"older version (a version-8 bundle, ticket 4875: the model tap member)", "\"format_version\": 11", "\"format_version\": 8",
		 Refusal::OlderVersion},
		{"older version (a version-7 bundle, ticket 0495: the trees and the near pairs)", "\"format_version\": 11", "\"format_version\": 7",
		 Refusal::OlderVersion},
		{"older version (a version-6 bundle, ticket 0493: the feed's read order)", "\"format_version\": 11", "\"format_version\": 6",
		 Refusal::OlderVersion},
		{"older version (a version-5 bundle, ticket 0403)", "\"format_version\": 11", "\"format_version\": 5", Refusal::OlderVersion},
		{"older version (a version-4 bundle, issue 0440)", "\"format_version\": 11", "\"format_version\": 4", Refusal::OlderVersion},
		{"unknown format", "\"c5-scene-bundle\"", "\"c5-scene-bundle-x\"", Refusal::UnknownFormat},
		{"missing key", "\"turn\": 9", "\"turnx\": 9", Refusal::MissingKey},
		{"unknown key", "\"bundle_id\"", "\"extra\": 1, \"bundle_id\"", Refusal::UnknownKey},
		{"wrong type", "\"turn\": 9", "\"turn\": \"9\"", Refusal::BadValue},
		{"fraction", "\"turn\": 9", "\"turn\": 9.5", Refusal::BadValue},
		{"negative u32", "\"turn\": 9", "\"turn\": -9", Refusal::BadValue},
		{"duplicate key", "\"turn\": 9", "\"turn\": 9, \"turn\": 9", Refusal::BadJson},
		{"bad vt", "\"vt\": \"absent\"", "\"vt\": \"yes\"", Refusal::BadValue},
		{"renamed key", "\"bundle_id\"", "\"bundle_idx\"", Refusal::MissingKey},
	};
	for(const Case& c : cases)
	{
		std::string t = js;
		size_t at = t.find(c.from);
		CHECK(at != std::string::npos, "%s: pattern not in the manifest", c.name);
		if(at == std::string::npos)
			continue;
		t.replace(at, strlen(c.from), c.to);
		Manifest x;
		CheckResult r;
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == c.want, "%s: %s, expected %s", c.name, r.line().c_str(),
		      RefusalName(c.want));
	}
	// an older bundle missing a key of version 1 is refused as older, not as the key
	{
		std::string t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 1");
		t.replace(t.find("\"bundle_id\""), 11, "\"bundle_idx\"");
		Manifest x;
		CheckResult r;
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion, "older first: %s", r.line().c_str());
	}
	// ticket 0493: the refusal of an older bundle names every change since its version, the feed's read order of version 7
	// among them; every version from 2 to kFormatVersion has its line (a raise without one fails here)
	{
		for(uint32_t v = 2; v <= kFormatVersion; ++v)
			CHECK(strlen(FormatVersionChange(v)) > 10, "FormatVersionChange(%u) is empty: a format raise needs its line", v);
		CHECK(*FormatVersionChange(1) == 0 && *FormatVersionChange(kFormatVersion + 1) == 0 && *FormatVersionChange(0) == 0,
		      "FormatVersionChange outside 2..%u is not empty", kFormatVersion);
		std::string t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 7");
		Manifest x;
		CheckResult r;
		const std::string want = "format_version 7 is older than 9 (refused, not read; changed since: version 8: the trees and the near-camera "
		                         "pairs drawn by default: feed.bin's PLOTS of caller 4";
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion && r.why.find(want) == 0 &&
		          r.why.find("ticket 0495") != std::string::npos && r.why.find("version 7:") == std::string::npos &&
		          r.why.find("; record the bundle again)") != std::string::npos,
		      "version 7 names the trees and the near pairs only: %s", r.line().c_str());
		t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 6");
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion && r.why.find("version 7: the feed's read order") != std::string::npos &&
		          r.why.find("version 8: the trees") != std::string::npos,
		      "version 6 names versions 7 and 8: %s", r.line().c_str());
		// ticket 4875: a version-8 bundle (the library before 4875) is refused naming version 9's model tap member only
		t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 8");
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion &&
		          r.why.find("format_version 8 is older than 9") == 0 && r.why.find("version 9: the cities and landmarks") != std::string::npos &&
		          r.why.find("modeltap/frame<NNNNNN>.mtap") != std::string::npos && r.why.find("version 8:") == std::string::npos,
		      "version 8 names the model tap member only: %s", r.line().c_str());
		t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 1");
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion, "version 1: %s", r.line().c_str());
		size_t at = 0;
		for(uint32_t v = 2; v <= kFormatVersion; ++v)
		{
			char key[32];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "version %u: ", v);
			const size_t p = r.why.find(key);
			CHECK(p != std::string::npos && p >= at, "version 1: change %u missing or out of order: %s", v, r.why.c_str());
			at = p == std::string::npos ? at : p;
		}
		CHECK(r.why.find("version 1:") == std::string::npos, "version 1 lists itself: %s", r.why.c_str());
		t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": 0");
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::OlderVersion && r.why.find("version 2: ") != std::string::npos,
		      "version 0: %s", r.line().c_str());
	}
	// member entries: bad path, unknown kind, bad sha, path of another kind
	const char* bad[][2] = {{"../x.c5hf", "BadPath"}, {"heightfield\\\\a.c5hf", "BadPath"}, {"heightfield/a.png", "BadPath"},
	                        {"feed2.bin", "BadPath"}, {"heightfield//a.c5hf", "BadPath"}, {"scene.json", "BadPath"}};
	for(auto& p : bad)
	{
		std::string t = js;
		std::string entry = std::string("\"members\": {\"") + p[0] + "\": {\"bytes\": 1, \"kind\": \"" +
		                    (std::string(p[0]).find("feed") != std::string::npos ? "feed" : "heightfield") +
		                    "\", \"sha256\": \"" + std::string(64, 'e') + "\", \"version\": 1}}";
		t.replace(t.find("\"members\": {}"), 13, entry);
		Manifest x;
		CheckResult r;
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::BadPath, "member path %s: %s", p[0], r.line().c_str());
	}
	{
		std::string t = js;
		t.replace(t.find("\"members\": {}"), 13, "\"members\": {\"feed.bin\": {\"bytes\": 1, \"kind\": \"feed\", \"sha256\": \"ABC\", \"version\": 1}}");
		Manifest x;
		CheckResult r;
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::BadValue, "bad sha string: %s", r.line().c_str());
		t = js;
		t.replace(t.find("\"members\": {}"), 13, "\"members\": {\"feed.bin\": {\"bytes\": 1, \"kind\": \"feed\", \"sha256\": \"" +
		                                         std::string(64, 'e') + "\", \"version\": 1}}");   // issue 4457: feed.bin's form is 2
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::BadVersion, "feed member version 1: %s", r.line().c_str());
		t = js;
		t.replace(t.find("\"members\": {}"), 13, "\"members\": {\"frames.bin\": {\"bytes\": 1, \"kind\": \"frames\", \"sha256\": \"" +
		                                         std::string(64, 'e') + "\", \"version\": 2}}");
		CHECK(!ParseManifest(t, &x, &r).ok() && r.refusal == Refusal::BadVersion, "frames member version 2: %s", r.line().c_str());
	}
	printf("ok   manifest rules (%u checks so far)\n", (unsigned)g_pass);
}

static void TestNegatives()
{
	// contract 10's list, each through the child process
	{
		std::wstring d = Dir("neg_older_version");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"format_version\": 11", "\"format_version\": 1"), "edit");
		ExpectRefused("older format_version (version 1)", d, Refusal::OlderVersion, "");
	}
	{   // ticket 0493: a version-6 bundle (recorded before the feed's read order of f916762a5) through the child: exit code,
		// the line naming the change, nothing written
		std::wstring d = Dir("neg_older_read_order");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"format_version\": 11", "\"format_version\": 7"), "edit");
		ExpectRefused("older format_version (version 7)", d, Refusal::OlderVersion, "");
		int code = -1;
		std::string out;
		CHECK(RunCheckChild(d, &code, &out) && code == RefusalExitCode(Refusal::OlderVersion) &&
		          out.find("format_version 7 is older than 9 (refused, not read; changed since: version 8: the trees and the near-camera pairs") !=
		              std::string::npos,
		      "version 7 child (ticket 0495): exit %d, %s", code, out.c_str());
	}
	{
		std::wstring d = Dir("neg_unknown_format");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"format\": \"c5-scene-bundle\"", "\"format\": \"c5-scene-bundle2\""), "edit");
		ExpectRefused("unknown format", d, Refusal::UnknownFormat, "");
	}
	{
		std::wstring d = Dir("neg_sha_mismatch");
		BuildFull(d, Full{false}, nullptr);
		std::vector<uint8_t> b;
		ReadBytes(d + L"/water_clock.bin", &b);
		b[40] ^= 1;
		WriteBytes(d + L"/water_clock.bin", b.data(), b.size());
		ExpectRefused("SHA-256 mismatch", d, Refusal::ShaMismatch, "water_clock.bin");
	}
	{
		std::wstring d = Dir("neg_size_mismatch");
		BuildFull(d, Full{false}, nullptr);
		std::vector<uint8_t> b;
		ReadBytes(d + L"/refs/default.rdc", &b);
		b.push_back(0);
		WriteBytes(d + L"/refs/default.rdc", b.data(), b.size());
		ExpectRefused("size differs from the manifest", d, Refusal::ShaMismatch, "refs/default.rdc");
	}
	{
		std::wstring d = Dir("neg_missing_member");
		BuildFull(d, Full{false}, nullptr);
		DeleteFileW((d + L"/cameras/default.json").c_str());
		ExpectRefused("missing member", d, Refusal::MissingMember, "cameras/default.json");
	}
	{
		std::wstring d = Dir("neg_unlisted_file");
		BuildFull(d, Full{false}, nullptr);
		WriteBytes(d + L"/dumps/notes.txt", "x", 1);
		ExpectRefused("unlisted file", d, Refusal::UnlistedFile, "dumps/notes.txt");
	}
	{
		std::wstring d = Dir("neg_no_manifest");
		BuildFull(d, Full{false}, nullptr);
		DeleteFileW((d + L"/scene.json").c_str());
		ExpectRefused("no scene.json", d, Refusal::NoManifest, "");
	}
	{
		std::wstring d = Dir("neg_missing_key");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"load_seq\": 1, ", ""), "edit");
		ExpectRefused("missing key", d, Refusal::MissingKey, "");
	}
	{
		std::wstring d = Dir("neg_unknown_key");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"load_seq\": 1, ", "\"load_seq\": 1, \"seed\": 5, "), "edit");
		ExpectRefused("unknown key", d, Refusal::UnknownKey, "");
	}
	// a truncated file per member kind: the manifest lists the short file's own hash, so the kind's reader must refuse it
	{
		struct T
		{
			const char* path;
			MemberKind kind;
			std::vector<uint8_t> bytes;
			size_t keep;
			Refusal want;
		};
		std::vector<uint8_t> feed = MakeFeed(nullptr), wc, ovl = MakeOverlay(), hfb = MakeHeightfield(true);
		EncodeWaterClock(MakeClock(), &wc);
		std::vector<uint8_t> frames;
		EncodeStreamHead(MemberKind::Frames, &frames);
		FrameRecord fr = MakeFrame(0, "");
		frames.insert(frames.end(), (const uint8_t*)&fr, (const uint8_t*)&fr + sizeof(fr));
		std::string ovlName = "overlay/" + Sha256Hex(ovl.data(), ovl.size() - 1000).substr(0, 16) + ".ovl";
		std::vector<T> ts = {
			{"heightfield/load0001.c5hf", MemberKind::Heightfield, hfb, hfb.size() - 100, Refusal::Truncated},
			{"water/load0001.wtap", MemberKind::WaterTap, MakeWaterTap(), MakeWaterTap().size() - 10, Refusal::Truncated},
			{"water/load0002.wtap", MemberKind::WaterTap, MakeWaterTap(), 100, Refusal::Truncated},
			{ovlName.c_str(), MemberKind::OverlayTexture, ovl, ovl.size() - 1000, Refusal::Truncated},
			{"feed.bin", MemberKind::Feed, feed, feed.size() - 3, Refusal::Truncated},
			{"frames.bin", MemberKind::Frames, frames, frames.size() - 8, Refusal::Truncated},
			{"cameras.bin", MemberKind::CameraBlocks, MakeCameras(2), MakeCameras(2).size() - 1, Refusal::Truncated},
			{"water_clock.bin", MemberKind::WaterClock, wc, wc.size() - 4, Refusal::Truncated},
			{"dumps/100.bmp", MemberKind::DumpColour, MakeBmp(), MakeBmp().size() - 4, Refusal::Truncated},
			{"dumps/100.depth.f32", MemberKind::DumpDepth, MakeDepth(), MakeDepth().size() - 2, Refusal::Truncated},
			{"cameras/a.vpcam", MemberKind::NamedCameraBlock, MakeCameras(1), 300, Refusal::Truncated},
			{"cameras/a.json", MemberKind::NamedCamera, Str("{\"name\": \"a\", \"pos\": [1, 2"), 0, Refusal::BadJson},
			{"refs/a.png", MemberKind::StockShot, MakePng(), 5, Refusal::Truncated},
			{"refs/a.rdc", MemberKind::StockCapture, Str("x"), 0, Refusal::Truncated},
		};
		int k = 0;
		for(T& t : ts)
		{
			if(t.keep)
				t.bytes.resize(t.keep);
			else if(t.kind == MemberKind::StockCapture)
				t.bytes.clear();
			char name[64];
			sprintf_s(name, "neg_trunc_%02d", k++);
			std::wstring d = Dir(name);
			CHECK(BuildOne(d, t.path, t.kind, t.bytes), "%s: not written", t.path);
			ExpectRefused((std::string("truncated ") + t.path).c_str(), d, t.want, t.path);
		}
	}
	// wrong sizes: the water tap block, the camera block, the overlay mip sizes
	{
		std::vector<uint8_t> b = MakeWaterTap();
		uint32_t v = 319;
		memcpy(&b[16], &v, 4);
		std::wstring d = Dir("neg_water_block_size");
		BuildOne(d, "water/load0001.wtap", MemberKind::WaterTap, b);
		ExpectRefused("water tap block size", d, Refusal::WrongSize, "water/load0001.wtap");
	}
	{
		std::vector<uint8_t> b = MakeWaterTap();
		b.push_back(0);
		std::wstring d = Dir("neg_water_extra");
		BuildOne(d, "water/load0001.wtap", MemberKind::WaterTap, b);
		ExpectRefused("water tap with bytes after the image", d, Refusal::WrongSize, "water/load0001.wtap");
	}
	{
		std::vector<uint8_t> b = MakeCameras(2);
		uint32_t v = 708;
		memcpy(&b[16], &v, 4);
		std::wstring d = Dir("neg_camera_block_size");
		BuildOne(d, "cameras.bin", MemberKind::CameraBlocks, b);
		ExpectRefused("camera block size", d, Refusal::WrongSize, "cameras.bin");
	}
	{
		std::vector<uint8_t> b = MakeCameras(2);
		std::wstring d = Dir("neg_vpcam_two");
		BuildOne(d, "cameras/two.vpcam", MemberKind::NamedCameraBlock, b);
		ExpectRefused(".vpcam with two blocks", d, Refusal::WrongSize, "cameras/two.vpcam");
	}
	{
		std::vector<uint8_t> b = MakeOverlay();
		uint32_t v = 2048 * 4 / 8 + 4;   // mip 3's row pitch, not tight
		memcpy(&b[32 + 3 * 4], &v, 4);
		std::string name = "overlay/" + Sha256Hex(b.data(), b.size()).substr(0, 16) + ".ovl";
		std::wstring d = Dir("neg_overlay_mip_size");
		BuildOne(d, name, MemberKind::OverlayTexture, b);
		ExpectRefused("overlay mip sizes", d, Refusal::WrongSize, name);
	}
	{
		std::vector<uint8_t> b = MakeOverlay();
		uint32_t v = 11;
		memcpy(&b[24], &v, 4);
		std::string name = "overlay/" + Sha256Hex(b.data(), b.size()).substr(0, 16) + ".ovl";
		std::wstring d = Dir("neg_overlay_11_mips");
		BuildOne(d, name, MemberKind::OverlayTexture, b);
		ExpectRefused("overlay with 11 mips", d, Refusal::BadMipCount, name);
	}
	{
		std::vector<uint8_t> b = MakeOverlay();
		std::string name = "overlay/0000000000000000.ovl";
		std::wstring d = Dir("neg_overlay_name");
		BuildOne(d, name, MemberKind::OverlayTexture, b);
		ExpectRefused("overlay named by another hash", d, Refusal::ShaMismatch, name);
	}
	{
		std::vector<uint8_t> b;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::wstring d = Dir("neg_empty_feed");
		BuildOne(d, "feed.bin", MemberKind::Feed, b);
		ExpectRefused("empty feed", d, Refusal::EmptyFeed, "feed.bin");
	}
	{
		FeedEncoder enc;
		std::vector<uint8_t> b, rec;
		EncodeStreamHead(MemberKind::Feed, &b);
		std::vector<SrvFeed::PlotRecord> t = MakeTable(4, 3, 0);
		enc.addPlots(1, 1, 1, 4, 3, t, &rec);   // primes the encoder; not written
		t[2].owner = 5;
		enc.addPlots(2, 2, 1, 4, 3, t, &rec);   // a diff
		b.insert(b.end(), rec.begin(), rec.end());
		uint32_t full;
		memcpy(&full, &b[16 + 8 + 8 + 16], 4);
		CHECK(full == 0, "the second PLOTS record is not a diff");
		std::wstring d = Dir("neg_plots_diff_first");
		BuildOne(d, "feed.bin", MemberKind::Feed, b);
		ExpectRefused("PLOTS diff before any full table", d, Refusal::PlotsDiffBeforeFull, "feed.bin");
	}
	{
		std::vector<uint8_t> b = MakeFeed(nullptr);
		uint32_t t = 9;
		memcpy(&b[16], &t, 4);
		std::wstring d = Dir("neg_feed_type");
		BuildOne(d, "feed.bin", MemberKind::Feed, b);
		ExpectRefused("feed record of an unknown type", d, Refusal::BadRecord, "feed.bin");
	}
	{
		std::vector<uint8_t> b = MakeWaterTap();
		memcpy(&b[0], "C5WX", 4);
		std::wstring d = Dir("neg_bad_magic");
		BuildOne(d, "water/load0001.wtap", MemberKind::WaterTap, b);
		ExpectRefused("bad magic", d, Refusal::BadMagic, "water/load0001.wtap");
	}
	{
		std::vector<uint8_t> b;
		EncodeWaterClock(MakeClock(), &b);
		uint32_t v = 2;
		memcpy(&b[4], &v, 4);
		std::wstring d = Dir("neg_bad_form_version");
		BuildOne(d, "water_clock.bin", MemberKind::WaterClock, b);
		ExpectRefused("file form version 2", d, Refusal::BadVersion, "water_clock.bin");
	}
	// cross-checks of the full bundle
	{
		std::wstring d = Dir("neg_dump_missing");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"dumped\": [100, 102]", "\"dumped\": [100, 101, 102]"), "edit");
		ExpectRefused("dumped frame without its dump", d, Refusal::MissingMember, "dumps/101.bmp");
	}
	{
		std::wstring d = Dir("neg_frame_count");
		BuildFull(d, Full{false}, nullptr);
		CHECK(ReplaceInFile(d + L"/scene.json", "\"count\": 3", "\"count\": 4"), "edit");
		ExpectRefused("frames.count differs from frames.bin", d, Refusal::WrongSize, "frames.bin");
	}
	{
		// the overlay a frame binds is not in the bundle: the full bundle without its overlay member
		std::wstring d = Dir("neg_overlay_unbound");
		BuildFull(d, Full(), nullptr);
		Manifest m;
		CheckResult r;
		ReadManifest(d, &m, &r);
		std::string ovl;
		for(const auto& kv : m.members)
			if(kv.second.kind == MemberKind::OverlayTexture)
				ovl = kv.first;
		DeleteFileW((d + L"/" + W(ovl)).c_str());
		m.members.erase(ovl);
		std::string js = ManifestToJson(m);
		WriteBytes(d + L"/scene.json", js.data(), js.size());
		ExpectRefused("frame binds an absent overlay", d, Refusal::MissingMember, ovl);
	}
	printf("ok   negatives (%u checks so far)\n", (unsigned)g_pass);
}

static void TestWriter()
{
	// the folder must be absent or empty; nothing is made otherwise
	{
		std::wstring d = Dir("writer_not_empty");
		MkDir(d);
		WriteBytes(d + L"/keep.txt", "k", 1);
		Snap before = SnapDir(d);
		Writer w;
		CHECK(!w.open(d).ok(), "open on a non-empty folder succeeded");
		CHECK(SnapDir(d) == before, "open on a non-empty folder wrote something");
	}
	{
		std::wstring d = Dir("writer_parent_missing") + L"/sub/bundle";
		Writer w;
		CHECK(!w.open(d).ok() && GetFileAttributesW(d.c_str()) == INVALID_FILE_ATTRIBUTES, "open below a missing parent");
	}
	{
		std::wstring d = Dir("writer_bad_paths");
		Writer w;
		CHECK(w.open(d).ok(), "open");
		uint8_t x = 1;
		CHECK(!w.addMember("../evil.c5hf", MemberKind::Heightfield, &x, 1).ok(), "a path with ..");
		CHECK(!w.addMember("C:/evil.png", MemberKind::StockShot, &x, 1).ok(), "an absolute path");
		CHECK(!w.addMember("heightfield/a.png", MemberKind::Heightfield, &x, 1).ok(), "a path of another kind");
		CHECK(!w.appendStart("refs/a.png", MemberKind::StockShot).ok(), "a stream of a non-stream kind");
		CHECK(!w.append("feed.bin", &x, 1).ok(), "append before appendStart");
		CHECK(w.addMember("refs/a.png", MemberKind::StockShot, MakePng().data(), MakePng().size()).ok(), "a good member");
		CHECK(!w.addMember("refs/a.png", MemberKind::StockShot, &x, 1).ok(), "the same path twice");
		Manifest m = BaseManifest();
		CHECK(w.finish(m).ok() && m.members.size() == 1, "finish");
		CHECK(!w.addMember("refs/b.png", MemberKind::StockShot, &x, 1).ok(), "a member after finish");
		CheckResult r;
		CHECK(CheckBundle(d, &r).ok(), "the bundle after the refused calls: %s", r.line().c_str());
		Snap s = SnapDir(d);
		CHECK(s.size() == 3 && s.count("scene.json") && s.count("refs/a.png"), "files written: %u", (unsigned)s.size());
	}
	// the disk-full stand-in (contract 10, for 3904): the writer fails at byte N, the bundle is refused as incomplete
	{
		std::wstring d = Dir("writer_fail_after");
		Writer w;
		CHECK(w.open(d).ok(), "open");
		w.failAfterBytes(1000);
		std::vector<uint8_t> hfb = MakeHeightfield(false);
		c5r::Status st = w.addMember("heightfield/load0001.c5hf", MemberKind::Heightfield, hfb.data(), hfb.size());
		CHECK(!st.ok() && w.incomplete(), "the seam did not fail the write: %s", st.str().c_str());
		CHECK(!w.appendStart("feed.bin", MemberKind::Feed).ok(), "a write after the failure succeeded");
		Manifest m = BaseManifest();
		st = w.finish(m);
		CHECK(!st.ok(), "finish of an incomplete bundle returned ok");
		CHECK(!m.versionNotes.empty() && m.versionNotes.back().find("incomplete: ") == 0 && m.members.empty(),
		      "the incomplete note is missing");
		ExpectRefused("incomplete bundle (failAfterBytes)", d, Refusal::Incomplete, "");
		std::vector<uint8_t> part;
		CHECK(ReadBytes(d + L"/.partial/heightfield/load0001.c5hf", &part) && part.size() == 1000,
		      "the partial member holds %u bytes, 1000 expected", (unsigned)part.size());
	}
	{
		std::wstring d = Dir("writer_mark_incomplete");
		Writer w;
		CHECK(w.open(d).ok(), "open");
		CHECK(w.addMember("refs/a.png", MemberKind::StockShot, MakePng().data(), MakePng().size()).ok(), "member");
		w.markIncomplete("the recorder stopped (test)");
		Manifest m = BaseManifest();
		CHECK(!w.finish(m).ok(), "finish after markIncomplete returned ok");
		ExpectRefused("incomplete bundle (markIncomplete)", d, Refusal::Incomplete, "");
	}
	{
		// a writer destroyed without finish leaves no scene.json: refused as NoManifest
		std::wstring d = Dir("writer_abandoned");
		{
			Writer w;
			w.open(d);
			w.appendStart("feed.bin", MemberKind::Feed);
		}
		ExpectRefused("abandoned writer", d, Refusal::NoManifest, "");
	}
	printf("ok   writer (%u checks so far)\n", (unsigned)g_pass);
}

// Ticket 4749 (format version 10): shadow.bin's file form 2 carries the slot's unit-mask-dropped flag and ShadowRaw's size;
// a format-9 bundle's form 1 (344-byte records) is still read, with the flag clear and a raw size of 0.
static void TestShadowUnitMask()
{
	CHECK(kFormatVersion == 11 && kOldestReadFormatVersion == 9 && sizeof(ShadowRecord) == 352 && kShadowRecordBytesForm1 == 344 &&
	          kUnitMaskDropped == 8,
	      "format 11, oldest read 9, record 352 / 344, flag 8");
	CHECK(MemberKindVersion(MemberKind::Shadow) == 2 && MemberKindVersionFor(MemberKind::Shadow, 10) == 2 &&
	          MemberKindVersionFor(MemberKind::Shadow, 9) == 1 && MemberKindVersionFor(MemberKind::Feed, 9) == kFeedFormVersion &&
	          MemberKindVersionFor(MemberKind::Frames, 9) == 1,
	      "shadow form 2 in format 10, 1 in format 9; the other kinds unchanged");
	CHECK(strstr(FormatVersionChange(10), "unit") && strstr(FormatVersionChange(10), "4749"), "FormatVersionChange(10): %s",
	      FormatVersionChange(10));
	// three records: bound at quality 3 with the flag, quality 0 with the flag, an empty one (a frame without a triple)
	std::vector<ShadowRecord> recs(3);
	memset(recs.data(), 0, recs.size() * sizeof(ShadowRecord));
	for(size_t k = 0; k < recs.size(); ++k)
		recs[k].frameId = 100 + (int64_t)k;
	recs[0].flags = kShadowBound | kUnitMaskDropped;
	recs[0].quality = 3;
	recs[0].dxgiFormat = 56;
	recs[0].width = recs[0].height = 2048;
	recs[0].numViews = 1;
	recs[0].strength = 1.0f;
	for(int i = 0; i < 4; ++i)
		recs[0].matrices[0][5 * i] = 1.0f;
	memset(recs[0].texelSha256, 0x5a, 32);
	recs[0].rawWidth = 2554;
	recs[0].rawHeight = 1411;
	recs[1].flags = kShadowQ0 | kUnitMaskDropped;
	recs[1].rawWidth = 2554;
	recs[1].rawHeight = 1411;
	std::vector<uint8_t> form2;
	EncodeStreamHead(MemberKind::Shadow, &form2);
	form2.insert(form2.end(), (const uint8_t*)recs.data(), (const uint8_t*)recs.data() + recs.size() * sizeof(ShadowRecord));
	std::vector<ShadowRecord> back;
	CheckResult r;
	CHECK(form2.size() == 20 + 3 * 352 && ParseShadow(form2, &back, &r).ok() && back.size() == 3 &&
	          !memcmp(back.data(), recs.data(), 3 * sizeof(ShadowRecord)),
	      "form 2 round trip: %s", r.line().c_str());
	CHECK(ParseShadow(form2, &back, &r, 10).ok(), "form 2 in format 10: %s", r.line().c_str());
	CHECK(!ParseShadow(form2, nullptr, &r, 9).ok() && r.refusal == Refusal::BadVersion, "form 2 in format 9: %s", r.line().c_str());
	// form 1, as a format-9 recorder wrote it: header version 1, 344-byte records (the same bytes up to iceLoadSeq)
	std::vector<uint8_t> form1(form2.begin(), form2.begin() + 20);
	form1[4] = 1;   // FileHeader.version
	const uint32_t rb1 = kShadowRecordBytesForm1;
	memcpy(form1.data() + 16, &rb1, 4);
	std::vector<ShadowRecord> old = recs;
	for(ShadowRecord& o : old)
		o.flags &= ~kUnitMaskDropped;
	for(const ShadowRecord& o : old)
		form1.insert(form1.end(), (const uint8_t*)&o, (const uint8_t*)&o + kShadowRecordBytesForm1);
	CHECK(ParseShadow(form1, &back, &r, 9).ok() && back.size() == 3 && back[0].rawWidth == 0 && back[0].rawHeight == 0 &&
	          back[0].flags == kShadowBound && back[0].strength == 1.0f && back[0].texelSha256[31] == 0x5a && back[1].flags == kShadowQ0,
	      "form 1 in format 9: %s", r.line().c_str());
	CHECK(!ParseShadow(form1, nullptr, &r).ok() && r.refusal == Refusal::BadVersion, "form 1 in format 10: %s", r.line().c_str());
	{
		std::vector<uint8_t> b = form1;
		uint32_t f = kShadowBound | kUnitMaskDropped;
		memcpy(b.data() + 20 + offsetof(ShadowRecord, flags), &f, 4);
		CHECK(!ParseShadow(b, nullptr, &r, 9).ok() && r.refusal == Refusal::BadRecord && r.why.find("above bit 2") != std::string::npos,
		      "form 1 with the flag: %s", r.line().c_str());
	}
	// form 2 negatives
	auto rec = [&](std::vector<uint8_t>& b, size_t k) { return (ShadowRecord*)(b.data() + 20 + k * sizeof(ShadowRecord)); };
	struct Bad
	{
		const char* what;
		std::vector<uint8_t> b;
		Refusal want;
	};
	std::vector<Bad> bads;
	bads.push_back({"flag on an empty record", form2, Refusal::BadRecord});
	rec(bads.back().b, 2)->flags = kUnitMaskDropped;
	bads.push_back({"flags above bit 3", form2, Refusal::BadRecord});
	rec(bads.back().b, 0)->flags |= 16;
	bads.push_back({"raw size 0 x 1411", form2, Refusal::BadRecord});
	rec(bads.back().b, 0)->rawWidth = 0;
	bads.push_back({"raw size above 16384", form2, Refusal::BadRecord});
	rec(bads.back().b, 1)->rawHeight = 16385;
	bads.push_back({"record size 344 in form 2", form2, Refusal::WrongSize});
	memcpy(bads.back().b.data() + 16, &rb1, 4);
	bads.push_back({"a partial record", form2, Refusal::Truncated});
	bads.back().b.resize(bads.back().b.size() - 8);
	for(const Bad& x : bads)
		CHECK(!ParseShadow(x.b, nullptr, &r).ok() && r.refusal == x.want, "%s: %s, expected %s", x.what, r.line().c_str(),
		      RefusalName(x.want));
	// the manifest: format 9 is read; the shadow member's version follows the format
	Manifest m = BaseManifest();
	const std::string js = ManifestToJson(m);
	const std::string entry = std::string("\"members\": {\"shadow.bin\": {\"bytes\": 1, \"kind\": \"shadow\", \"sha256\": \"") +
	                          std::string(64, 'e') + "\", \"version\": ";
	struct Case
	{
		int format, member;
		bool ok;
	} cases[] = {{10, 2, true}, {9, 1, true}, {10, 1, false}, {9, 2, false}, {8, 1, false}};
	for(const Case& c : cases)
	{
		std::string t = js;
		t.replace(t.find("\"format_version\": 11"), 20, "\"format_version\": " + std::to_string(c.format));
		t.replace(t.find("\"members\": {}"), 13, entry + std::to_string(c.member) + "}}");
		Manifest x;
		const bool ok = ParseManifest(t, &x, &r).ok();
		CHECK(ok == c.ok && (!ok || (x.formatVersion == (uint32_t)c.format && x.members["shadow.bin"].version == (uint32_t)c.member)) &&
		          (ok || r.refusal == (c.format < 9 ? Refusal::OlderVersion : Refusal::BadVersion)),
		      "format %d with shadow form %d: %s", c.format, c.member, r.line().c_str());
	}
	printf("ok   shadow unit mask, format 10 (%u checks so far)\n", (unsigned)g_pass);
}

// Ticket 0622 (format version 11): water_frames.bin, the water copy drawn per frame. The form's refusals in process; the
// checker's cross-checks through the child (exit code, line, nothing written) and in process; a two-copy span passes; a
// format-10 bundle without the member is still read, one with it is refused.
static void TestWaterFrames()
{
	CHECK(kFormatVersion == 11 && kWaterFramesFormatVersion == 11 && kOldestReadFormatVersion == 9 && sizeof(WaterFrameRecord) == 56 &&
	          kWaterCopyInForce == 1 && std::string(MemberKindName(MemberKind::WaterFrames)) == "water_frames" &&
	          MemberKindVersion(MemberKind::WaterFrames) == 1,
	      "format 11, record 56, flag 1, kind water_frames form 1");
	MemberKind k = MemberKind::Heightfield;
	CHECK(MemberKindFromName("water_frames", &k) && k == MemberKind::WaterFrames, "water_frames by name");
	CHECK(strstr(FormatVersionChange(11), "water_frames.bin") && strstr(FormatVersionChange(11), "0622"), "FormatVersionChange(11): %s",
	      FormatVersionChange(11));
	const std::vector<uint8_t> c1 = MakeWaterTap(1), c2 = MakeWaterTap(2);
	CHECK(c1 != c2, "the two copies' bytes differ");
	// the form: a copy-less first frame, copy 1, copy 2 (a Waves_Reload inside the span) round-trip
	const std::vector<WaterFrameRecord> good = {WaterRec(10, nullptr), WaterRec(11, &c1), WaterRec(12, &c2, 2), WaterRec(14, &c2, 2)};
	std::vector<uint8_t> gb = WaterFramesBytes(good);
	CHECK(gb.size() == 20 + 4 * 56 && !memcmp(gb.data(), "C5WF", 4), "stream head and size %zu", gb.size());
	std::vector<WaterFrameRecord> back;
	CheckResult r;
	CHECK(ParseWaterFrames(gb, &back, &r).ok() && back.size() == 4 && !memcmp(back.data(), good.data(), 4 * sizeof(WaterFrameRecord)),
	      "round trip: %s", r.line().c_str());
	CHECK(Sha256HexOf(good[1].wtapSha256) == Sha256Hex(c1.data(), c1.size()), "Sha256HexOf");
	{
		std::vector<uint8_t> empty = WaterFramesBytes({});
		CHECK(ParseWaterFrames(empty, &back, &r).ok() && back.empty(), "no record: %s", r.line().c_str());
	}
	struct Bad
	{
		const char* what;
		std::vector<WaterFrameRecord> recs;
		Refusal want;
	};
	std::vector<Bad> bads;
	bads.push_back({"flags above bit 0", good, Refusal::BadRecord});
	bads.back().recs[1].flags |= 2;
	bads.push_back({"reserved not 0", good, Refusal::BadRecord});
	bads.back().recs[2].reserved = 1;
	bads.push_back({"no copy, but a load", good, Refusal::BadRecord});
	bads.back().recs[0].loadSeq = 3;
	bads.push_back({"no copy, but a hash", good, Refusal::BadRecord});
	bads.back().recs[0].wtapSha256[5] = 1;
	bads.push_back({"a copy without its hash", good, Refusal::BadRecord});
	memset(bads.back().recs[1].wtapSha256, 0, 32);
	bads.push_back({"frame ids not increasing", good, Refusal::BadRecord});
	bads.back().recs[3].frameId = 12;
	bads.push_back({"a copy, then none", good, Refusal::BadRecord});
	bads.back().recs[3] = WaterRec(14, nullptr);
	bads.push_back({"copy 2, then copy 1", good, Refusal::BadRecord});
	bads.back().recs[3] = WaterRec(14, &c1);
	for(const Bad& x : bads)
	{
		const std::vector<uint8_t> b = WaterFramesBytes(x.recs);
		CHECK(!ParseWaterFrames(b, nullptr, &r).ok() && r.refusal == x.want, "%s: %s, expected %s", x.what, r.line().c_str(),
		      RefusalName(x.want));
	}
	{
		std::vector<uint8_t> b = gb;
		const uint32_t rb = 48;
		memcpy(b.data() + 16, &rb, 4);
		CHECK(!ParseWaterFrames(b, nullptr, &r).ok() && r.refusal == Refusal::WrongSize, "record size 48: %s", r.line().c_str());
		b = gb;
		b.resize(b.size() - 8);
		CHECK(!ParseWaterFrames(b, nullptr, &r).ok() && r.refusal == Refusal::Truncated, "a partial record: %s", r.line().c_str());
		b = gb;
		b[0] = 'X';
		CHECK(!ParseWaterFrames(b, nullptr, &r).ok() && r.refusal == Refusal::BadMagic, "magic: %s", r.line().c_str());
		b = gb;
		b[4] = 2;
		CHECK(!ParseWaterFrames(b, nullptr, &r).ok() && r.refusal == Refusal::BadVersion, "file form 2: %s", r.line().c_str());
	}
	// the checker: the full bundle with the member passes; a two-copy span passes
	{
		std::wstring d = Dir("wf_full");
		Full f;
		CHECK(BuildFull(d, f, nullptr) && CheckBundle(d, &r).ok(), "full with water_frames.bin: %s", r.line().c_str());
		d = Dir("wf_two");
		const std::vector<WaterFrameRecord> two = {WaterRec(100, &c1), WaterRec(101, &c2, 2), WaterRec(102, &c2, 2)};
		f.secondCopy = true;
		f.waterRecs = &two;
		CHECK(BuildFull(d, f, nullptr) && CheckBundle(d, &r).ok(), "two copies, the switch at frame 101: %s", r.line().c_str());
		int code = -1;
		std::string out;
		CHECK(RunCheckChild(d, &code, &out) && code == 0 && out.find("scenebundle: OK") != std::string::npos, "two copies, child: %d %s", code,
		      out.c_str());
	}
	// the checker's refusals
	{
		Full f;
		f.waterFrames = false;
		std::wstring d = Dir("wf_missing");
		CHECK(BuildFull(d, f, nullptr), "build");
		ExpectRefused("water_frames.bin missing in format 11", d, Refusal::MissingMember, "water_frames.bin");
		// the same bundle as format 10 (written before 0622): read as before
		CHECK(ReplaceInFile(d + L"/scene.json", "\"format_version\": 11", "\"format_version\": 10"), "edit");
		CHECK(CheckBundle(d, &r).ok(), "format 10 without water_frames.bin is read: %s", r.line().c_str());
		d = Dir("wf_in_v10");
		CHECK(BuildFull(d, Full(), nullptr) && ReplaceInFile(d + L"/scene.json", "\"format_version\": 11", "\"format_version\": 10"), "build");
		ExpectRefused("water_frames.bin in format 10", d, Refusal::BadValue, "water_frames.bin");
		const std::vector<WaterFrameRecord> two = {WaterRec(100, &c1), WaterRec(101, &c1)};
		f = Full();
		f.waterRecs = &two;
		d = Dir("wf_count");
		CHECK(BuildFull(d, f, nullptr), "build");
		ExpectRefused("water_frames.bin: 2 records for 3 frames", d, Refusal::WrongSize, "water_frames.bin");
		const std::vector<WaterFrameRecord> ids = {WaterRec(100, &c1), WaterRec(101, &c1), WaterRec(103, &c1)};
		f.waterRecs = &ids;
		d = Dir("wf_ids");
		CHECK(BuildFull(d, f, nullptr), "build");
		ExpectRefused("water_frames.bin: frame 103 for 102", d, Refusal::BadRecord, "water_frames.bin");
		const std::vector<WaterFrameRecord> absent = {WaterRec(100, &c1), WaterRec(101, &c2, 2), WaterRec(102, &c2, 2)};
		f.waterRecs = &absent;   // copy 2 named, not a member
		d = Dir("wf_absent");
		CHECK(BuildFull(d, f, nullptr), "build");
		ExpectRefused("water_frames.bin: a copy not in the bundle", d, Refusal::MissingMember, "water_frames.bin");
		std::vector<WaterFrameRecord> wrong = {WaterRec(100, &c1), WaterRec(101, &c1), WaterRec(102, &c1)};
		wrong[2].copySeq = 2;   // copy 1's hash, named as copy 2
		f.waterRecs = &wrong;
		d = Dir("wf_wrongcopy");
		CHECK(BuildFull(d, f, nullptr), "build");
		ExpectRefused("water_frames.bin: copy seq not the member's", d, Refusal::BadRecord, "water_frames.bin");
	}
	printf("ok   water frames, format 11 (%u checks so far)\n", (unsigned)g_pass);
}

static void TestHelpers()
{
	CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
	CHECK(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
	for(uint32_t k = 1; k <= (uint32_t)MemberKind::FogClock; ++k)
	{
		MemberKind back;
		CHECK(MemberKindFromName(MemberKindName((MemberKind)k), &back) && back == (MemberKind)k, "kind %u name", k);
	}
	CHECK(RefusalExitCode(Refusal::None) == 0 && RefusalExitCode(Refusal::NoManifest) == 21 &&
	          RefusalExitCode(Refusal::BadValue) == 42 && std::string(RefusalName(Refusal::BadMipCount)) == "BadMipCount",
	      "exit codes and names");
	CHECK(sizeof(FrameRecord) == 344 && kCameraBlockBytes == 712 && kWaterTapFixedBytes == 356, "sizes");
	// the asset-set port: three cases in three folders (the runner re-reads each after this program ended), compared
	// with visualcheck.py by the runner (PYCHECK)
	std::wstring absent = g_work + L"/install_no_vt";
	RemoveTree(absent);
	MkDir(absent);
	MkDir(absent + L"/Assets");
	MkDir(absent + L"/Assets/DLC");
	Manifest m;
	CHECK(ComputeAssetSet(absent, "test", &m).ok() && m.vt == "absent" && m.vtFiles == 0, "absent VT: %s", m.vt.c_str());
	auto py = [](const Manifest& x) {
		std::string vt = x.vt;
		for(char& c : vt)
			if(c == ' ')
				c = '_';
		printf("PYCHECK asset_set %s %s %s %s %u\n", x.install.c_str(), x.machine.c_str(), vt.c_str(),
		       x.vtHash.empty() ? "-" : x.vtHash.c_str(), x.vtFiles);
	};
	py(m);
	std::wstring inst = g_work + L"/install_vt";
	RemoveTree(inst);
	MkDir(inst);
	MkDir(inst + L"/Assets");
	MkDir(inst + L"/Assets/DLC");
	MkDir(inst + L"/Assets/DLC/VT");
	MkDir(inst + L"/Assets/DLC/VT/Sub");
	MkDir(inst + L"/Assets/DLC/VT/Sub/Deeper");
	WriteBytes(inst + L"/Assets/DLC/VT/B.dds", "bbbb", 4);
	WriteBytes(inst + L"/Assets/DLC/VT/a.xml", "<a/>", 4);
	WriteBytes(inst + L"/Assets/DLC/VT/Sub/Tex_01.DDS", "t1", 2);
	WriteBytes(inst + L"/Assets/DLC/VT/Sub/Deeper/z.txt", "", 0);
	WriteBytes(inst + L"/Assets/DLC/VT/Sub.txt", "s", 1);
	CHECK(ComputeAssetSet(inst, "test", &m).ok() && m.vt == "present (other hash)" && m.vtFiles == 5 && m.vtHash.size() == 64,
	      "synthetic VT: %s %u", m.vt.c_str(), m.vtFiles);
	py(m);
	CHECK(ComputeAssetSet(g_work + L"/no_such_install", "test", &m).ok() && m.vt == "unknown" &&
	          m.assetWhy == "install folder not found",
	      "missing install");
	py(m);
	printf("ok   helpers and the asset-set port (%u checks so far)\n", (unsigned)g_pass);
}

int wmain(int argc, wchar_t** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if(argc == 3 && !wcscmp(argv[1], L"--check"))
	{
		CheckResult res;
		CheckBundle(argv[2], &res);
		printf("%s\n", res.line().c_str());
		return RefusalExitCode(res.refusal);
	}
	if(argc != 3 || wcscmp(argv[1], L"--work"))
	{
		printf("usage: bundle_format_test --work DIR | --check DIR\n");
		return 2;
	}
	g_work = argv[2];
	for(wchar_t& c : g_work)
		if(c == L'\\')
			c = L'/';
	MkDir(g_work);
	TestHelpers();
	TestManifestRules();
	TestRoundTrip();
	TestNegatives();
	TestFogMembers();   // issue 3911
	TestDecalMember();  // issue 0364
	TestModelTapMember();   // ticket 4875 (format version 9)
	TestModelGeomMember();  // ticket 4888 (format version 9, optional member)
	TestUnitRecords();  // issue 4457
	TestUnitsMembers(); // issue 0440
	TestParticlesMember();   // ticket 0403
	TestForestNearMembers();   // ticket 0495
	TestFarmConfig();          // ticket 4852
	TestShadowUnitMask();      // ticket 4749 (format version 10)
	TestWaterFrames();         // ticket 0622 (format version 11)
	TestWriter();
	printf("RESULT: %d passed, %d failed, 0 skipped\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
