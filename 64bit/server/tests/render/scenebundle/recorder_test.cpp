// recorder_test.cpp - tests of the scene recorder (srv_scene_recorder.{h,cpp}; epic 0301, task 3904; contract:
// research/06-tooling-and-workflows/offline-scene-harness.md sections 5, 7, 9, 10 and 15).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). The section-free builders follow bundle_format_test.cpp and
// sources_test.cpp of this folder (3902, 3903).
//
// Every byte is synthetic, made by the test (D83). The recorder is driven directly, as the host drives it: the readers'
// hand-off points (field and water sinks, the recording feed over SrvFeed's writer calls, the recording overlay wrapper
// over a fake source with real textures), and the frame hook on a WARP device's targets. Each bundle is checked with
// 3902's CheckBundle; the negatives check the refusal in process and through a child (`--check`: exit code and line).
// The P2 replay (contract 7) draws on a scene with the stand-in baker and needs C5R_EXE (read-only); a missing input FAILS.
//
//   recorder_test.exe --work DIR                         the suite (writes only below DIR); last line RESULT: ...
//   recorder_test.exe --check DIR                        CheckBundle: prints the result line, exits RefusalExitCode
//   recorder_test.exe --stamp DIR KEY=VALUE ...          fills scene.json's fields the server cannot know (section 15),
//                                                        then checks the bundle; exit 0 OK, 2 usage, else the refusal's code
#include "srv_scene_recorder.h"
#include "srv_render_scene.h"
#include "srv_residency_source.h"
#include "feed/srv_feed.h"
#include "c5r/heightfield/heightfield.h"
#include "decal_image_fixture.h"   // issue 0364
#include "c5r/fow/fow.h"            // ticket 0403
#include "srv_unit_shadow.h"        // ticket 4749: UnitShadowWanted, the scene's per-frame merge rule
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace srvrender;
using namespace srvrender::scenebundle;
namespace c5h = c5r::heightfield;

namespace {

int g_pass = 0, g_fail = 0;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_work, g_exe;
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

#define CHECK(cond, ...)                                                                                                       \
	do                                                                                                                         \
	{                                                                                                                          \
		if(cond)                                                                                                               \
			++g_pass;                                                                                                          \
		else                                                                                                                   \
		{                                                                                                                      \
			++g_fail;                                                                                                          \
			printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                                             \
			printf(__VA_ARGS__);                                                                                               \
			printf("\n");                                                                                                      \
			fflush(stdout);                                                                                                    \
		}                                                                                                                      \
	} while(0)

// ---- files ----------------------------------------------------------------------------------------------------------------
void RemoveTree(const std::wstring& dir)
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

std::wstring Dir(const char* name)
{
	std::wstring d = g_work + L"/" + Widen(name);
	RemoveTree(d);
	return d;
}

bool WriteBytes(const std::wstring& path, const void* p, size_t n)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"wb") || !f)
		return false;
	const bool ok = fwrite(p, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

int CountFiles(const std::wstring& dir, const wchar_t* pattern)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"/" + pattern).c_str(), &fd);
	int n = 0;
	if(h != INVALID_HANDLE_VALUE)
	{
		do
			n += (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
		while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	return n;
}

// Runs this program with the arguments; output and exit code.
bool RunChild(const std::wstring& args, int* code, std::string* out)
{
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + args;
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
	WaitForSingleObject(pi.hProcess, 120000);
	DWORD ec = 0;
	GetExitCodeProcess(pi.hProcess, &ec);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	*code = (int)ec;
	return true;
}

void ExpectRefusedChild(const char* name, const std::wstring& dir, Refusal want)
{
	int code = -1;
	std::string out;
	const bool ran = RunChild(L"--check \"" + dir + L"\"", &code, &out);
	const std::string line = std::string("scenebundle: REFUSED ") + RefusalName(want);
	CHECK(ran, "%s: the check child did not start", name);
	CHECK(code == RefusalExitCode(want), "%s: exit %d, expected %d (%s): %s", name, code, RefusalExitCode(want), RefusalName(want),
	      out.c_str());
	CHECK(out.find(line) != std::string::npos, "%s: output lacks \"%s\": %s", name, line.c_str(), out.c_str());
	CheckResult res;
	CHECK(!CheckBundle(dir, &res).ok() && res.refusal == want, "%s: in process %s (%s)", name, RefusalName(res.refusal), res.why.c_str());
	printf("ok   negative %-34s exit %d  %s\n", name, code, res.line().c_str());
}

// ---- synthetic inputs ------------------------------------------------------------------------------------------------------
std::vector<uint8_t> FieldBytes(uint16_t height = (uint16_t)(20 << 8))
{
	c5h::HeightGrid g;
	g.mapW = 4;
	g.mapH = 4;
	g.samplesX = 256;
	g.samplesY = 256;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = 128.0f;
	g.oy = 128.0f * 0.866f;
	g.waterZ = 10.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, height);
	std::vector<uint8_t> img;
	c5h::writeTap(g, img);
	return img;
}

struct WaterParts
{
	VPShellWaterTap block;
	std::vector<uint8_t> image;
};

WaterParts MakeWater(uint32_t load, uint32_t copy)
{
	WaterParts w;
	memset(&w.block, 0, sizeof(w.block));
	VPShellWaterImage img = {};
	img.magic = VPSHELL_WATER_MAGIC;
	img.version = VPSHELL_WATER_IMAGE_VERSION;
	img.headerBytes = sizeof(VPShellWaterImage);
	img.recordBytes = sizeof(VPShellWaterRecord);
	img.recordCount = 1;
	img.loadSeq = load;
	img.exePid = 4242;
	img.mapW = 4;
	img.mapH = 4;
	img.waterZ = 10.0f;
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
		w.image[k] = (uint8_t)(k * 31 + copy);
	w.block.state = 2;
	w.block.loadSeq = load;
	w.block.exePid = 4242;
	w.block.copySeq = copy;
	w.block.recordCount = 1;
	w.block.imageBytes = img.imageBytes;
	w.block.sectionBytes = 65536;
	w.block.fnv1a64 = 0x1234567890abcdefull;
	wcscpy_s(w.block.sectionName, L"Local\\vp_test_water_3904");
	return w;
}

OverlayTexels Texels(uint32_t seed)
{
	OverlayTexels t;
	t.width = t.height = 2048;
	t.mips = 12;
	t.dxgiFormat = 28;
	t.bytes.resize((size_t)OverlayTightBytes(2048, 2048, 12));
	uint32_t x = seed * 2654435761u + 1;
	for(size_t i = 0; i < t.bytes.size(); ++i)
	{
		x = x * 1664525u + 1013904223u;
		t.bytes[i] = (uint8_t)(x >> 24);
	}
	return t;
}

ID3D11ShaderResourceView* TextureOf(const OverlayTexels& t)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = t.width;
	d.Height = t.height;
	d.MipLevels = t.mips;
	d.ArraySize = 1;
	d.Format = (DXGI_FORMAT)t.dxgiFormat;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
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
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, sd, &tex)))
		return nullptr;
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
	vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	vd.Texture2D.MipLevels = t.mips;
	g_dev->CreateShaderResourceView(tex, &vd, &v);
	tex->Release();
	return v;
}

SrvFeed::PlotRecord Rec(int8_t terrain, int16_t improvement)
{
	SrvFeed::PlotRecord r = {};
	r.terrain = terrain;
	r.plotType = 2;
	r.feature = -1;
	r.route = -1;
	r.resource = -1;
	r.improvement = improvement;
	r.owner = -1;
	r.improvementState = -1;
	r.routeState = -1;
	r.known = 3;
	return r;
}

VPShellCamera Camera(int64_t frame)
{
	VPShellCamera c;
	memset(&c, 0, sizeof(c));
	c.status = VPCAM_OK;
	c.presentCount = frame;
	c.frameStamp = frame;
	c.numViews = 1;
	const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -120, 1};
	const float w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 120, 1};
	memcpy(c.view[0], v, sizeof(v));
	memcpy(c.camWorld, w, sizeof(w));
	const float n = 1.0f, f = 1000.0f;
	const float ys = 1.0f / tanf(15.0f * 3.14159265f / 180.0f), xs = ys * 120.0f / 160.0f;
	const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
	memcpy(c.proj, p, sizeof(p));
	c.bbW = 160;
	c.bbH = 120;
	c.gridSeq = 1;
	c.gridValid = 1;
	c.wrapX = 1;
	c.wrapPeriod = 255.0f;
	c.sx = 1.0f;
	c.sy = 0.866f;
	c.heightScale = 0.2f;
	c.ox = 128.0f;
	c.oy = 128.0f * 0.866f;
	c.samplesX = 256;
	c.samplesY = 256;
	return c;
}

// The sink's targets as the ring sink makes them: B8G8R8A8 colour, D32_FLOAT depth (frame_sink.h)
const uint32_t kW = 160, kH = 120;
struct Target
{
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* dt = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	Target(DXGI_FORMAT colour = DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT depth = DXGI_FORMAT_D32_FLOAT)
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = kW;
		d.Height = kH;
		d.MipLevels = d.ArraySize = 1;
		d.Format = colour;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET;
		g_dev->CreateTexture2D(&d, nullptr, &rt);
		if(rt)
			g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
		d.Format = depth;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		g_dev->CreateTexture2D(&d, nullptr, &dt);
		if(dt)
			g_dev->CreateDepthStencilView(dt, nullptr, &dsv);
	}
	~Target()
	{
		SafeRelease(dsv);
		SafeRelease(dt);
		SafeRelease(rtv);
		SafeRelease(rt);
	}
	// a "draw": colour and depth of frame k, so each dump can be told apart
	void fill(int k)
	{
		const float c[4] = {0.1f * (float)(k % 10), 0.4f, 1.0f - 0.1f * (float)(k % 10), 1.0f};
		g_ctx->ClearRenderTargetView(rtv, c);
		g_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.25f + 0.05f * (float)(k % 10), 0);
	}
};

struct FakeOverlay : OverlaySource
{
	OverlayFrame pair;
	bool give = true;
	bool acquire(OverlayFrame& out) override
	{
		out = OverlayFrame();
		if(!give)
			return false;
		out = pair;
		return true;
	}
	OverlayTapReader::State state() const override { return give ? OverlayTapReader::State::Active : OverlayTapReader::State::Waiting; }
	uint32_t failReason() const override { return 0; }
	std::string why() const override { return ""; }
};

// The host's loop around one recorder, without the scene: what server.exe's Loop and DrawFrame do with it.
struct Rig
{
	RecorderKeys keys;
	RecorderContext ctx;
	std::unique_ptr<SceneRecorder> rec;
	std::unique_ptr<FeedSource> feed;
	std::unique_ptr<OverlaySource> ovl;
	FakeOverlay* fake = nullptr;
	WaterScene water;
	Target target;
	int64_t published = 0;
	OverlayFrame bound;   // what the scene's last acquire gave
	bool haveBound = false;
	bool open(const std::wstring& dir, int frames, const char* dump, std::string& why, int after = 1)
	{
		std::map<std::string, std::string> kv;
		kv["render_test_record"] = srvrender::scenebundle::Narrow(dir);
		kv["render_test_record_frames"] = std::to_string(frames);
		kv["render_test_record_after"] = std::to_string(after);
		if(dump)
			kv["render_test_record_dump"] = dump;
		if(!ParseRecorderKeys([&](const char* k) { auto i = kv.find(k); return i == kv.end() ? std::string() : i->second; }, keys, why))
			return false;
		ctx.hashBinaries = false;
		ctx.renderConfig["render_tiles"] = "bake";
		ctx.versionNotes.push_back("gap: bake-cache tap left out (render_tiles=bake)");
		ctx.machine = "test";
		rec = SceneRecorder::Open(keys, ctx, why);
		if(!rec)
			return false;
		rec->setAdapter(0xABCDull, "WARP", "test");
		feed = MakeRecordingFeed(LiveFeed(), *rec);
		fake = new FakeOverlay();
		ovl = MakeRecordingOverlaySource(std::unique_ptr<OverlaySource>(fake), *rec);
		return true;
	}
	// one loop iteration: drain, overlay poll, a draw of frame `frame` (when > 0), the step
	std::vector<double> drainClocks;   // issue 4456: SceneRecorder::lastDrainMs() after each drain that returned true
	std::vector<double> preSpanClocks; // issue 0440: the clock beforeDraw gave each frame outside the span
	// shadow (ticket 4749): the frame's bound shadow triple, handed to afterDrawSeamG as the host does (null: no call)
	void iterate(int64_t frame, uint32_t flags = 0, const ShadowFrame* shadow = nullptr)
	{
		rec->beginIteration();
		SrvFeed::Batch b;
		if(feed->drain(b))
			drainClocks.push_back(rec->lastDrainMs());   // issue 4456: the value the host hands the unit scene
		OverlayFrame f;
		haveBound = ovl->acquire(f) && f.srv;
		if(haveBound)
			bound = f;
		if(frame > 0)
		{
			SceneFrame fr;
			fr.cam = Camera(frame);
			fr.frameId = frame;
			double now = -1;
			const bool in = rec->beforeDraw(frame, published, water, &now, &fr);   // issue 0440: the frame block too
			if(!in)
				preSpanClocks.push_back(now);   // issue 0440: >= 0 only while the unit scene is recorded
			target.fill((int)frame);
			if(in)
			{
				c5r::terrain::Overlay o;
				o.srv = haveBound ? bound.srv : nullptr;
				rec->afterDraw(g_ctx, fr, now, kW, kH, target.rtv, target.dsv, o, flags);
				if(shadow)   // ticket 4749: the host's order (DrawFrame: afterDraw, then afterDrawSeamG)
					rec->afterDrawSeamG(g_ctx, shadow, nullptr, 0, 0);
			}
			published++;
			if(in)
				rec->afterEnd(g_ctx);
		}
		rec->step(RecorderCounters());
	}
	void end()
	{
		ovl.reset();   // the wrapper maps what is pending (the host: the scene's releaseDevice)
		rec->finish(RecorderCounters(), "test end");
	}
};

// ==== the cases ===================================================================================================================
void TestKeys()
{
	auto parse = [](std::map<std::string, std::string> kv, RecorderKeys& k, std::string& why) {
		return ParseRecorderKeys([&](const char* key) { auto i = kv.find(key); return i == kv.end() ? std::string() : i->second; }, k, why);
	};
	RecorderKeys k;
	std::string why;
	CHECK(parse({}, k, why) && !k.on, "no keys: %s", why.c_str());
	CHECK(parse({{"render_test_record", "C:/x/bundle/"}}, k, why) && k.on && k.dir == L"C:/x/bundle" && k.frames == 8 && k.after == 1 &&
	          k.dumpEvery == 0 && k.dumpCount() == 2 && k.dumpsFrame(0) && k.dumpsFrame(7) && !k.dumpsFrame(3),
	      "defaults: %s", why.c_str());
	CHECK(parse({{"render_test_record", "\\\\host\\share\\b"}, {"render_test_record_dump", "all"}, {"render_test_record_frames", "3"}}, k, why) &&
	          k.dumpCount() == 3,
	      "UNC, all: %s", why.c_str());
	CHECK(parse({{"render_test_record", "F:\\b"}, {"render_test_record_dump", "every:4"}, {"render_test_record_frames", "9"}}, k, why) &&
	          k.dumpCount() == 3 && k.dumpsFrame(4) && k.dumpsFrame(8) && !k.dumpsFrame(5),
	      "every:4: %s", why.c_str());
	struct Neg
	{
		std::map<std::string, std::string> kv;
		const char* want;
	};
	const std::string longDir = "C:/" + std::string(200, 'a');
	const Neg negs[] = {
	    {{{"render_test_record", "bundles/x"}}, "render_test_record=bundles/x: an absolute folder"},
	    {{{"render_test_record", "C:bundle"}}, "render_test_record=C:bundle: an absolute folder"},
	    {{{"render_test_record", longDir}}, "at most 200 characters"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_frames", "0"}}, "render_test_record_frames=0: an integer 1..100000"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_frames", "100001"}}, "render_test_record_frames=100001: an integer"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_frames", "8x"}}, "render_test_record_frames=8x: an integer"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_after", "0"}}, "render_test_record_after=0: an integer 1..1000000"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_dump", "first"}}, "render_test_record_dump=first: first,last, all or every:N"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_dump", "every:0"}}, "render_test_record_dump=every:0:"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_dump", "every:8"}}, "render_test_record_dump=every:8: 1 dumped frame(s)"},
	    {{{"render_test_record", "C:/b"}, {"render_test_record_frames", "1"}}, "render_test_record_frames=1: 1 dumped frame(s) of a span of 1"},
	    {{{"render_test_record_frames", "4"}}, "render_test_record_frames=4: needs render_test_record"},
	    {{{"render_test_record_dump", "all"}}, "render_test_record_dump=all: needs render_test_record"},
	    {{{"render_test_record_after", "3"}}, "render_test_record_after=3: needs render_test_record"},
	};
	for(const Neg& n : negs)
	{
		RecorderKeys out;
		out.frames = 77;
		std::string w;
		const bool ok = parse(n.kv, out, w);
		CHECK(!ok && w.find(n.want) != std::string::npos && out.frames == 77, "refusal '%s': ok %d, why '%s'", n.want, (int)ok, w.c_str());
	}
	printf("ok   keys: %zu refusals\n", sizeof(negs) / sizeof(negs[0]));
}

// The whole path: field, water, feed, overlay (two generations of equal texels: one file; a third of other texels), a span
// of 4 frames with first,last dumps; the bundle passes the checker and holds what was handed in.
void TestBundle()
{
	const std::wstring dir = Dir("bundle_ok");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 4, nullptr, why, 3), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1));
	SrvFeed::EndSnapshot();
	// the load: field, water, the baseline table (as ResidencySource::takeBaseline reads it)
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	const WaterParts wp = MakeWater(1, 1);
	SceneRecorder::WaterSink(r.rec.get(), wp.block, wp.image.data(), wp.image.size(), true, 1, 256, 256);
	std::vector<SrvFeed::PlotRecord> t;
	uint32_t gen = 0, w = 0, h = 0;
	r.feed->copyPlots(t, gen, w, h, kPlotsBaseline);
	// overlay: generation 1 and 2 with the same texels, generation 3 with others
	const OverlayTexels ta = Texels(5), tb = Texels(6);
	ID3D11ShaderResourceView* va = TextureOf(ta);
	ID3D11ShaderResourceView* va2 = TextureOf(ta);
	ID3D11ShaderResourceView* vb = TextureOf(tb);
	CHECK(va && va2 && vb, "textures");
	r.fake->pair.srv = va;
	r.fake->pair.numViews = 1;
	r.fake->pair.generation = 1;
	r.fake->pair.presentCount = 50;
	r.fake->pair.slot = 0;
	for(int i = 0; i < 16; ++i)
		r.fake->pair.overlayProj[0][i] = (float)i;
	r.iterate(10);   // published 0 -> not in the span (after = 3)
	SrvFeed::SetPlot(5, Rec(1, 7));
	r.iterate(11);   // published 1
	CHECK(!r.rec->spanStarted(), "span started before published frame 3");
	r.iterate(12);   // published 2: the span's first frame (dump)
	r.fake->pair.srv = va2;
	r.fake->pair.generation = 2;
	r.fake->pair.slot = 1;
	r.iterate(13, kFrameWaterModule);
	r.fake->pair.srv = vb;
	r.fake->pair.generation = 3;
	r.fake->pair.slot = 2;
	SrvFeed::SetPlot(9, Rec(2, -1));
	r.iterate(14);
	r.fake->give = false;
	r.iterate(15);   // the last frame (dump), no overlay
	CHECK(r.rec->spanDone() && r.rec->framesRecorded() == 4, "span %d frames %u", (int)r.rec->spanDone(), r.rec->framesRecorded());
	for(int i = 0; i < 4 && !r.rec->finished(); ++i)
		r.iterate(0);
	CHECK(r.rec->finished() && r.rec->complete(), "finished %d complete %d: %s", (int)r.rec->finished(), (int)r.rec->complete(),
	      r.rec->finishWhy().c_str());
	r.end();
	SrvFeed::SetEnabled(false);
	CheckResult res;
	const c5r::Status st = CheckBundle(dir, &res);
	CHECK(st.ok(), "checker: %s", res.line().c_str());
	int code = -1;
	std::string out;
	CHECK(RunChild(L"--check \"" + dir + L"\"", &code, &out) && code == 0 && out.find("scenebundle: OK") != std::string::npos,
	      "child check exit %d: %s", code, out.c_str());
	Manifest m;
	CHECK(ReadManifest(dir, &m, &res).ok(), "manifest: %s", res.line().c_str());
	CHECK(m.frameCount == 4 && m.firstFrameId == 12 && m.dumpedFrames == std::vector<int64_t>({12, 15}), "frames %u first %lld dumped %zu",
	      m.frameCount, (long long)m.firstFrameId, m.dumpedFrames.size());
	CHECK(m.mapW == 4 && m.mapH == 4 && m.loadSeq == 1 && m.saveName == "bundle_ok" && m.machine == "test" && m.adapterLuid == "000000000000ABCD",
	      "save %s map %ux%u load %u machine %s luid %s", m.saveName.c_str(), m.mapW, m.mapH, m.loadSeq, m.machine.c_str(), m.adapterLuid.c_str());
	CHECK(m.recorderKeys.size() == 3 && m.recorderKeys["render_test_record_frames"] == "4" && m.counters["record_errors"] == 0,
	      "keys %zu, record errors %lld", m.recorderKeys.size(), (long long)m.counters["record_errors"]);
	CHECK(m.members.count("heightfield/load0001.c5hf") && m.members.count("water/load0001.wtap") && m.members.count("dumps/12.bmp") &&
	          m.members.count("dumps/15.depth.f32") && !m.members.count("dumps/13.bmp"),
	      "members");
	// the overlay dedupe: generations 1 and 2 (equal texels) are one file, generation 3 another
	CHECK(CountFiles(dir + L"/overlay", L"*.ovl") == 2, "overlay files %d (want 2)", CountFiles(dir + L"/overlay", L"*.ovl"));
	std::vector<uint8_t> b;
	std::vector<FrameRecord> fr;
	CHECK(ReadMember(dir, m, "frames.bin", &b, &res).ok() && ParseFrames(b, &fr, &res).ok() && fr.size() == 4, "frames.bin");
	if(fr.size() == 4)
	{
		CHECK(fr[0].overlayBound && fr[1].overlayBound && fr[2].overlayBound && !fr[3].overlayBound, "bound flags");
		CHECK(!memcmp(fr[0].overlaySha256, fr[1].overlaySha256, 32) && memcmp(fr[1].overlaySha256, fr[2].overlaySha256, 32) &&
		          fr[0].overlayGeneration == 1 && fr[1].overlayGeneration == 2 && fr[2].overlayGeneration == 3,
		      "overlay shas and generations");
		CHECK((fr[0].flags & kFrameDumped) && !(fr[1].flags & kFrameDumped) && (fr[1].flags & kFrameWaterModule) && (fr[3].flags & kFrameDumped),
		      "flags %u %u %u %u", fr[0].flags, fr[1].flags, fr[2].flags, fr[3].flags);
		CHECK(fr[0].nowMs > 0 && fr[1].nowMs >= fr[0].nowMs && fr[0].width == kW && fr[0].height == kH && fr[0].loopIter == 3, "clock / size / iter");
		CHECK(fr[0].overlayProj[0][5] == 5.0f && fr[0].overlayNumViews == 1 && fr[0].overlayPresentCount == 50, "pair fields");
	}
	// the overlay file holds the texels handed in
	std::string sha;
	for(const auto& kv : m.members)
		if(kv.second.kind == MemberKind::OverlayTexture)
		{
			std::vector<uint8_t> ob, want;
			EncodeOverlay(ta.bytes.data(), ta.bytes.size(), &want);
			ReadMember(dir, m, kv.first, &ob, &res);
			if(ob == want)
				sha = kv.second.sha256;
		}
	CHECK(!sha.empty(), "no overlay file holds generation 1's texels byte for byte");
	// the dumps hold the targets of their frames: colour (12 % 10 = 2: 0.2, 0.4, 0.8 as BGRA bytes) and depth
	CHECK(ReadMember(dir, m, "dumps/12.bmp", &b, &res).ok() && b.size() == 54 + kW * kH * 4, "bmp size %zu", b.size());
	if(b.size() == 54 + kW * kH * 4)
		CHECK(b[54] == 204 && b[55] == 102 && b[56] == 51 && b[57] == 255, "bmp pixel %u %u %u %u", b[54], b[55], b[56], b[57]);
	CHECK(ReadMember(dir, m, "dumps/15.depth.f32", &b, &res).ok() && b.size() == kW * kH * 4, "depth size %zu", b.size());
	if(b.size() == kW * kH * 4)
	{
		float d0 = 0;
		memcpy(&d0, b.data(), 4);
		CHECK(fabsf(d0 - 0.5f) < 1e-6f, "depth %f (want 0.5)", d0);
	}
	// the feed: the baseline table, the drains with the next draw's frame id
	std::vector<FeedRecord> feed;
	CHECK(ReadMember(dir, m, "feed.bin", &b, &res).ok() && ParseFeed(b, &feed, &res).ok(), "feed.bin: %s", res.line().c_str());
	size_t drains = 0, plots = 0;
	bool ids = true;
	for(const FeedRecord& f : feed)
	{
		(f.type == kFeedDrain ? drains : plots)++;
		if(f.type == kFeedDrain && f.nextFrameId != (int64_t)(10 + f.loopIter - 1))
			ids = false;
	}
	CHECK(plots == 1 && drains >= 3 && feed[0].type == kFeedPlots && feed[0].caller == kPlotsBaseline && feed[0].plots.size() == 16,
	      "feed: %zu tables %zu drains", plots, drains);
	CHECK(ids, "a DRAIN's nextFrameId is not the frame drawn in its loop iteration");
	// issue 4456: SceneRecorder::lastDrainMs() is exactly the drainMs feed.bin holds for each DRAIN, in order; -1 for a drain
	// the finished recorder does not record (the host then lets the unit scene read the clock)
	{
		std::vector<double> rec;
		for(const FeedRecord& f : feed)
			if(f.type == kFeedDrain)
				rec.push_back(f.drainMs);
		size_t recorded = 0;
		bool same = true;
		for(size_t i = 0; i < r.drainClocks.size(); ++i)
		{
			if(r.drainClocks[i] < 0)
				continue;
			same = same && recorded < rec.size() && rec[recorded] == r.drainClocks[i] && rec[recorded] > 0;
			recorded++;
		}
		CHECK(same && recorded == rec.size() && recorded >= 3, "lastDrainMs: %zu recorded drains, feed.bin %zu, equal %d", recorded,
		      rec.size(), (int)same);
		// a drain after the recorder finished (the recording feed hands every drain that returned true to onDrain)
		CHECK(r.rec->finished() && r.rec->lastDrainMs() > 0, "finished %d, lastDrainMs %f", (int)r.rec->finished(), r.rec->lastDrainMs());
		SrvFeed::Batch extra;
		r.rec->onDrain(extra);
		CHECK(r.rec->lastDrainMs() == -1, "lastDrainMs after the recorder finished: %f", r.rec->lastDrainMs());
	}
	std::vector<VPShellCamera> cams;
	CHECK(ReadMember(dir, m, "cameras.bin", &b, &res).ok() && ParseCameras(b, &cams, &res).ok() && cams.size() == 4 && cams[0].frameStamp == 12,
	      "cameras.bin");
	// --stamp fills the fields the server cannot know, and the bundle still passes
	CHECK(RunChild(L"--stamp \"" + dir + L"\" save.sha256=" + std::wstring(64, L'e') +
	                   L" save.turn=9 save.name=C5H-TEST repo_commit=abc build_config=release pins.dll_lock.commit=04b1211b8",
	               &code, &out) &&
	          code == 0,
	      "stamp exit %d: %s", code, out.c_str());
	CHECK(ReadManifest(dir, &m, &res).ok() && m.saveSha256 == std::string(64, 'e') && m.turn == 9 && m.saveName == "C5H-TEST" &&
	          m.repoCommit == "abc" && m.pins["dll_lock.commit"] == "04b1211b8",
	      "stamped manifest");
	CHECK(CheckBundle(dir, &res).ok(), "after the stamp: %s", res.line().c_str());
	CHECK(RunChild(L"--stamp \"" + dir + L"\" save.colour=red", &code, &out) && code == 2 && out.find("unknown key") != std::string::npos,
	      "stamp of an unknown key: exit %d: %s", code, out.c_str());
	SafeRelease(va);
	SafeRelease(va2);
	SafeRelease(vb);
	printf("ok   bundle: %zu members, %zu feed records\n", m.members.size(), feed.size());
}

// Issue 3911 (format version 2): the fog's inputs as the host hands them to the recorder. The fog scene (stand-in modules:
// no GPU program needed here) reads its table through the recording feed (StartRecorder's FowScene::setTable before its
// start): feed.bin holds that read as PLOTS caller 3 in loop iteration 0, and a second one after the drain of a fog edit;
// fog_clock.bin holds the fog's clock as it was right before the span's first frame. A fog that is OFF writes no
// fog_clock.bin.
struct StubFowModules : FowModules
{
	c5r::Status initMask(ID3D11Device*, const std::wstring&, uint32_t) override { return c5r::Status::Ok(); }
	c5r::Status initClouds(ID3D11Device*, const std::wstring&, const std::wstring&) override { return c5r::Status::Ok(); }
	c5r::Status buildMask(ID3D11DeviceContext*, const c5r::fow::MaskInput&, c5r::fow::MaskStats*) override { return c5r::Status::Ok(); }
	c5r::Status drawClouds(ID3D11DeviceContext*, const c5r::fow::CloudInput&, c5r::fow::CloudStats*) override { return c5r::Status::Ok(); }
	ID3D11ShaderResourceView* mask() const override { return nullptr; }
	ID3D11ShaderResourceView* cloudBase() const override { return nullptr; }
	void release() override {}
};

void TestFog(bool on)
{
	const std::wstring dir = Dir(on ? "fog_on" : "fog_off");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 3, "all", why), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
	{
		SrvFeed::PlotRecord p = Rec(1, -1);
		p.fow = i < 8 ? 3 : 0;
		SrvFeed::SetPlot(i, p);
	}
	SrvFeed::EndSnapshot();
	FowScene fow(std::unique_ptr<FowModules>(new StubFowModules()));
	fow.setTable(MakeFeedSourcePlotTable(*r.feed));   // the host's StartRecorder
	r.rec->setFog(&fow);
	FowSceneOptions fo;
	fo.on = on;
	fo.level = 2;          // not the ini of this machine
	fow.init(g_dev, fo);   // the fog's start: its first table read (caller 3, loop iteration 0)
	r.rec->setRenderConfig("render_fow", on ? "1" : "0");
	r.rec->setRenderConfig("render_fow_level", on ? std::to_string(fow.level()) : "-1");
	FowClockState seed;
	seed.clockS = 3.25;
	seed.clockLastMs = 900.5;
	CHECK(!on || fow.setClockState(seed), "the fog refused its clock");
	FowClockState badSeed;
	badSeed.clockS = -1;
	CHECK(!fow.setClockState(badSeed) && (!on || fow.clockState().clockS == 3.25), "the fog took a T below 0");
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	r.fake->give = false;
	r.iterate(1);
	SrvFeed::PlotRecord p = Rec(1, -1);
	p.fow = 3;
	SrvFeed::SetPlot(12, p);
	{   // the host's drain with the fog's step (Scene::fowFeed); Rig::iterate drains too, so this is the iteration's drain
		r.rec->beginIteration();
		SrvFeed::Batch b;
		CHECK(r.feed->drain(b) && b.changedPlots.size() == 1, "the fog edit's drain");
		fow.onBatch(b);
		r.rec->step(RecorderCounters());
	}
	r.iterate(2, on ? kFrameFog : 0u);   // the host's RecorderAfterDraw: the fog's mask bound in this draw
	if(on)
		fow.particleFrames += 2;   // issue 0440 (MC #3003): the FOWLevel-3 particles drawn in the span (a stub fog draws none)
	r.iterate(3);
	for(int i = 0; i < 4 && !r.rec->finished(); ++i)
		r.iterate(0);
	r.end();
	SrvFeed::SetEnabled(false);
	CheckResult res;
	CHECK(CheckBundle(dir, &res).ok(), "[fog %d] checker: %s", (int)on, res.line().c_str());
	Manifest m;
	CHECK(ReadManifest(dir, &m, &res).ok(), "manifest: %s", res.line().c_str());
	CHECK(m.formatVersion == kFormatVersion && m.renderConfig["render_fow"] == (on ? "1" : "0") &&
	          m.renderConfig["render_fow_level"] == (on ? "2" : "-1"),
	      "[fog %d] format %u, render_fow %s, level %s", (int)on, m.formatVersion, m.renderConfig["render_fow"].c_str(),
	      m.renderConfig["render_fow_level"].c_str());
	for(const std::string& n : m.versionNotes)
		CHECK(n.find("0168") == std::string::npos, "[fog %d] a version note still names 0168's fog: %s", (int)on, n.c_str());
	{   // issue 0440 (MC #3003): the particles drawn in the span are a gap: line; none drawn, no line
		std::string pn;
		for(const std::string& n : m.versionNotes)
			if(n.compare(0, strlen(kFogParticlesNote), kFogParticlesNote) == 0)
				pn = n;
		CHECK(on ? pn.find("(2 particle frames from the span's start") != std::string::npos : pn.empty(), "[fog %d] the particles' note: '%s'",
		      (int)on, pn.c_str());
	}
	std::vector<uint8_t> b;
	std::vector<FeedRecord> feed;
	CHECK(ReadMember(dir, m, "feed.bin", &b, &res).ok() && ParseFeed(b, &feed, &res).ok(), "feed.bin: %s", res.line().c_str());
	std::vector<FrameRecord> frs;
	CHECK(ReadMember(dir, m, "frames.bin", &b, &res).ok() && ParseFrames(b, &frs, &res).ok() && frs.size() == 3 &&
	          ((frs[1].flags & kFrameFog) != 0) == on && !(frs[0].flags & kFrameFog) && !(frs[2].flags & kFrameFog),
	      "[fog %d] frames.bin's fog flag (bit 3) not kept as the host set it: %s", (int)on, res.line().c_str());
	size_t fogReads = 0;
	for(const FeedRecord& f : feed)
		if(f.type == kFeedPlots && f.caller == kPlotsFog)
			fogReads++;
	if(on)
	{
		CHECK(!feed.empty() && feed[0].type == kFeedPlots && feed[0].caller == kPlotsFog && feed[0].loopIter == 0 && feed[0].plots.size() == 16 &&
		          feed[0].plots[0].fow == 3 && feed[0].plots[12].fow == 0,
		      "[fog 1] feed record 0 is not the fog's start read (caller 3, loop iteration 0, 16 plots)");
		CHECK(fogReads == 2, "[fog 1] %zu fog table reads (want 2: the start and the edit)", fogReads);
		FogClockFile fc;
		CHECK(m.members.count("fog_clock.bin") && ReadMember(dir, m, "fog_clock.bin", &b, &res).ok() && ParseFogClock(b, &fc, &res).ok() &&
		          fc.clockS == 3.25 && fc.clockLastMs == 900.5,
		      "[fog 1] fog_clock.bin: %s (T %g, last %g)", res.line().c_str(), fc.clockS, fc.clockLastMs);
	}
	else
	{
		CHECK(fogReads == 0 && !m.members.count("fog_clock.bin"), "[fog 0] %zu fog reads, fog_clock.bin %d (want none)", fogReads,
		      (int)m.members.count("fog_clock.bin"));
	}
	printf("ok   fog %s: %zu members, %zu fog table reads\n", on ? "on" : "off", m.members.size(), fogReads);
}

// Format version 6 (ticket 0403): the FOWLevel-3 particles' state at the span's first frame. Stand-in modules with a real
// c5r::fow::ParticleSim (no GPU program: the spawn and the state are the simulation's). Variants: 0 spawned (the state set
// through FowScene::setParticleState before the span, as a live spawn and steps would leave it) -> particles.bin with that
// state byte-equal, render_fow_particles 1, no gap note; 1 on but not spawned yet -> particles.bin not spawned with the
// seed 1; 2 the switch C5_RENDER_FOW_PARTICLES=0 -> render_fow_particles 0, no member, no note; 3 a module without a
// simulation -> render_fow_particles 1, no member, the gap note naming why; 4 FOWLevel 2 -> 0, no member, no note.
struct PartStubFowModules : StubFowModules
{
	bool withSim = true;
	c5r::fow::ParticleSim sim;
	static c5r::fow::FlowField Flow()
	{
		c5r::fow::FlowField flow;
		flow.width = flow.height = 4;
		for(int i = 0; i < 32; ++i)
			flow.xy.push_back(0.1f * (float)((i % 7) - 3));
		return flow;
	}
	c5r::Status initParticles(ID3D11Device*, const std::wstring&, const std::wstring&) override { return c5r::Status::Ok(); }
	c5r::Status resetParticles(float w, float h, uint32_t seed) override
	{
		return sim.reset(w, h, c5r::fow::ParticleSettings(), Flow(), seed);
	}
	c5r::Status drawParticles(ID3D11DeviceContext*, const c5r::fow::ParticleInput&, c5r::fow::ParticleStats*) override
	{
		return c5r::Status::Ok();
	}
	c5r::fow::ParticleSim* particleSim() override { return withSim ? &sim : nullptr; }
};

void TestFogParticles(int variant)
{
	const std::wstring dir = Dir(("fog_particles_" + std::to_string(variant)).c_str());
	Rig r;
	std::string why;
	CHECK(r.open(dir, 3, "all", why), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
	{
		SrvFeed::PlotRecord p = Rec(1, -1);
		p.fow = i < 8 ? 3 : 0;
		SrvFeed::SetPlot(i, p);
	}
	SrvFeed::EndSnapshot();
	PartStubFowModules* mods = new PartStubFowModules();
	mods->withSim = variant != 3;
	FowScene fow{std::unique_ptr<FowModules>(mods)};
	fow.setTable(MakeFeedSourcePlotTable(*r.feed));
	r.rec->setFog(&fow);
	FowSceneOptions fo;
	fo.on = true;
	fo.level = variant == 4 ? 2 : 3;
	if(variant == 2)
		SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLES", L"0");
	fow.init(g_dev, fo);
	SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLES", nullptr);
	r.rec->setRenderConfig("render_fow", "1");
	r.rec->setRenderConfig("render_fow_level", std::to_string(fow.level()));
	CHECK(fow.particlesOn() == (variant == 0 || variant == 1 || variant == 3), "[particles %d] on %d after init", variant, (int)fow.particlesOn());
	std::vector<uint8_t> want;
	if(variant == 0)
	{
		// the state a live spawn and three steps leave (seed 9, a 2048 x 1024 world), set as the player sets it
		c5r::fow::ParticleSim s;
		const float lo[2] = {-300.0f, -200.0f}, hi[2] = {300.0f, 200.0f};
		CHECK(s.reset(2048.0f, 1024.0f, c5r::fow::ParticleSettings(), PartStubFowModules::Flow(), 9).ok() && s.step(lo, hi).ok() &&
		          s.step(lo, hi).ok() && s.step(lo, hi).ok(),
		      "[particles 0] the reference sim");
		want = s.saveState();
		FowParticleState ps;
		ps.spawned = true;
		ps.seed = 9;
		ps.worldW = 2048.0f;
		ps.worldH = 1024.0f;
		ps.sim = want;
		std::string pw;
		CHECK(fow.setParticleState(ps, pw), "[particles 0] setParticleState: %s", pw.c_str());
	}
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	r.fake->give = false;
	r.iterate(1);
	r.iterate(2, kFrameFog);
	if(fow.particlesOn())
		fow.particleFrames += 2;   // the stand-in draws nothing: the counter as two drawn particle frames would leave it
	r.iterate(3, kFrameFog);
	for(int i = 0; i < 4 && !r.rec->finished(); ++i)
		r.iterate(0);
	r.end();
	SrvFeed::SetEnabled(false);
	CheckResult res;
	CHECK(CheckBundle(dir, &res).ok(), "[particles %d] checker: %s", variant, res.line().c_str());
	Manifest m;
	CHECK(ReadManifest(dir, &m, &res).ok(), "manifest: %s", res.line().c_str());
	std::string pn;
	for(const std::string& n : m.versionNotes)
		if(n.compare(0, strlen(kFogParticlesNote), kFogParticlesNote) == 0)
			pn = n;
	const bool on = variant == 0 || variant == 1 || variant == 3;
	CHECK(m.formatVersion == kFormatVersion && m.renderConfig["render_fow_particles"] == (on ? "1" : "0"), "[particles %d] format %u, render_fow_particles '%s'",
	      variant, m.formatVersion, m.renderConfig["render_fow_particles"].c_str());
	CHECK(m.members.count("particles.bin") == (variant <= 1 ? 1u : 0u), "[particles %d] particles.bin %d", variant, (int)m.members.count("particles.bin"));
	if(variant <= 1)
	{
		std::vector<uint8_t> b;
		FogParticlesFile pf;
		CHECK(ReadMember(dir, m, "particles.bin", &b, &res).ok() && ParseFogParticles(b, &pf, &res).ok(), "[particles %d] particles.bin: %s", variant,
		      res.line().c_str());
		if(variant == 0)
			CHECK(pf.flags == kFogParticlesSpawned && pf.seed == 9 && pf.worldW == 2048.0f && pf.worldH == 1024.0f && pf.state == want,
			      "[particles 0] the recorded state is not the one in force (flags %u, seed %u, world %g x %g, %zu of %zu bytes)", pf.flags, pf.seed,
			      pf.worldW, pf.worldH, pf.state.size(), want.size());
		else
			CHECK(pf.flags == 0 && pf.seed == 1 && pf.state.empty(), "[particles 1] not spawned: flags %u, seed %u, %zu state bytes", pf.flags,
			      pf.seed, pf.state.size());
		CHECK(pn.empty(), "[particles %d] a gap note beside particles.bin: %s", variant, pn.c_str());
	}
	else if(variant == 3)
		CHECK(pn.find("(2 particle frames from the span's start, on at its first frame 1)") != std::string::npos &&
		          pn.find("no simulation") != std::string::npos,
		      "[particles 3] the gap note: '%s'", pn.c_str());
	else
		CHECK(pn.empty(), "[particles %d] a gap note with the particles off: %s", variant, pn.c_str());
	printf("ok   fog particles variant %d: render_fow_particles %s, particles.bin %d\n", variant, m.renderConfig["render_fow_particles"].c_str(),
	       (int)m.members.count("particles.bin"));
}

// Format version 3 (issue 0364): the decal reader's set in force at the span's first frame is written as
// decals/load<NNNN>.dtap with the reader's own writer (byte-equal to the image the reader accepted); a generation applied
// inside the span is noted in version_notes; no set (or no reader) at the first frame writes the note kDecalNoSetNote and
// no member. Each bundle passes the checker with render_decals 1.
void TestDecals(int mode)   // 0: a set, changed inside the span; 1: a reader without a set; 2: no reader at all
{
	const char* names[] = {"decals_set", "decals_noset", "decals_noreader"};
	const std::wstring dir = Dir(names[mode]);
	Rig r;
	std::string why;
	CHECK(r.open(dir, 3, "all", why), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1));
	SrvFeed::EndSnapshot();
	DecalTapReader reader(0);
	const std::vector<uint8_t> img5 = decalfixture::MakeImage(5, 1), img6 = decalfixture::MakeImage(6, 1);
	if(mode == 0)
		CHECK(reader.loadImage(img5.data(), img5.size()).ok() && reader.generation() == 5, "the reader refused the test image: %s",
		      reader.lastWhy().c_str());
	const DecalTapReader* rp = mode == 2 ? nullptr : &reader;
	r.rec->setDecalReader([rp]() { return rp; });
	r.rec->setRenderConfig("render_decals", "1");
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	r.fake->give = false;
	r.iterate(1);
	r.iterate(2);
	if(mode == 0)   // a new generation between frames 2 and 3 (the scene's decalPoll in the loop)
		CHECK(reader.loadImage(img6.data(), img6.size()).ok() && reader.generation() == 6, "generation 6 refused");
	r.iterate(3);
	for(int i = 0; i < 4 && !r.rec->finished(); ++i)
		r.iterate(0);
	r.end();
	SrvFeed::SetEnabled(false);
	CheckResult res;
	CHECK(CheckBundle(dir, &res).ok(), "[decals %d] checker: %s", mode, res.line().c_str());
	Manifest m;
	CHECK(ReadManifest(dir, &m, &res).ok(), "manifest: %s", res.line().c_str());
	CHECK(m.formatVersion == kFormatVersion && m.renderConfig["render_decals"] == "1", "[decals %d] format %u, render_decals '%s'", mode, m.formatVersion,
	      m.renderConfig["render_decals"].c_str());
	bool noSet = false, changed = false;
	for(const std::string& n : m.versionNotes)
	{
		noSet = noSet || n.compare(0, strlen(kDecalNoSetNote), kDecalNoSetNote) == 0;
		changed = changed || n.find("decal generation changed inside the span: generation 5 at the first frame, 6 before frame 3") != std::string::npos;
	}
	size_t decalMembers = 0;
	for(const auto& kv : m.members)
		decalMembers += kv.second.kind == MemberKind::DecalTap;
	if(mode == 0)
	{
		std::vector<uint8_t> b;
		CHECK(decalMembers == 1 && ReadMember(dir, m, "decals/load0001.dtap", &b, &res).ok() && b == img5,
		      "[decals 0] decals/load0001.dtap is not the first frame's image (generation 5): %s", res.line().c_str());
		CHECK(changed && !noSet, "[decals 0] the generation change inside the span not noted (or a no-set note)");
	}
	else
		CHECK(decalMembers == 0 && noSet && !changed, "[decals %d] %zu decal members, no-set note %d, change note %d", mode, decalMembers, (int)noSet,
		      (int)changed);
	// no temporary image left in %TEMP% by the recorder
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	WIN32_FIND_DATAW fd;
	const std::wstring pat = std::wstring(tmp) + L"c5_scene_decal_" + std::to_wstring(GetCurrentProcessId()) + L"_*";
	HANDLE h = FindFirstFileW(pat.c_str(), &fd);
	CHECK(h == INVALID_HANDLE_VALUE, "[decals %d] the recorder left a temporary image in %ls", mode, tmp);
	if(h != INVALID_HANDLE_VALUE)
		FindClose(h);
	printf("ok   decals mode %d: %zu members, %zu decal member(s), notes %zu\n", mode, m.members.size(), decalMembers, m.versionNotes.size());
}

// Open refuses an unusable folder (the host logs it LOUD and runs without the recorder): a non-empty folder, a path under
// a file. Nothing is written into either.
void TestUnwritable()
{
	const std::wstring full = Dir("not_empty");
	CreateDirectoryW(full.c_str(), NULL);
	WriteBytes(full + L"/other.txt", "x", 1);
	Rig a;
	std::string why;
	CHECK(!a.open(full, 2, "all", why) && why.find("not empty") != std::string::npos, "non-empty folder: %s", why.c_str());
	CHECK(CountFiles(full, L"*") == 1, "something was written into the non-empty folder");
	const std::wstring file = Dir("a_file");
	WriteBytes(file, "y", 1);
	Rig b;
	CHECK(!b.open(file + L"/sub", 2, "all", why) && why.find("render_test_record=") == 0, "under a file: %s", why.c_str());
	DeleteFileW(file.c_str());
	printf("ok   unwritable: %s\n", why.c_str());
}

// The disk-full stand-in: the writer fails at byte N; the recorder goes on (record errors), the bundle says incomplete
// and the checker refuses it (exit 20 + Incomplete).
void TestDiskFull()
{
	const std::wstring dir = Dir("disk_full");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 2, "all", why), "open: %s", why.c_str());
	if(!r.rec)
		return;
	r.rec->writer().failAfterBytes(100000);
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());   // 131,200 bytes: fails here
	r.iterate(1);
	r.iterate(2);
	for(int i = 0; i < 3; ++i)
		r.iterate(0);
	r.end();
	CHECK(r.rec->finished() && !r.rec->complete() && r.rec->recordErrors() > 0, "complete %d errors %lld why %s", (int)r.rec->complete(),
	      (long long)r.rec->recordErrors(), r.rec->finishWhy().c_str());
	CHECK(r.rec->finishWhy().find("failAfterBytes") != std::string::npos, "why: %s", r.rec->finishWhy().c_str());
	CHECK(CountLog("LOUD: recorder:") >= 1, "no LOUD line");
	ExpectRefusedChild("disk_full", dir, Refusal::Incomplete);
}

// A span that never starts, a frame id that goes back (first,last: one dump: incomplete; all: complete with a note), an
// overlay whose readback failed.
void TestShortSpans()
{
	{
		const std::wstring dir = Dir("never_started");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 2, nullptr, why, 50), "open: %s", why.c_str());
		if(!r.rec)
			return;
		r.iterate(1);
		r.end();
		CHECK(!r.rec->complete() && r.rec->finishWhy().find("never started") != std::string::npos, "why: %s", r.rec->finishWhy().c_str());
		ExpectRefusedChild("never_started", dir, Refusal::Incomplete);
	}
	{
		const std::wstring dir = Dir("frame_back_firstlast");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 4, nullptr, why), "open: %s", why.c_str());
		if(!r.rec)
			return;
		r.iterate(5);
		r.iterate(6);
		r.iterate(6);   // a redraw with the same frame id: the span ends after 2 frames, one dumped
		for(int i = 0; i < 3; ++i)
			r.iterate(0);
		r.end();
		CHECK(!r.rec->complete() && r.rec->finishWhy().find("1 dumped frame") != std::string::npos, "why: %s", r.rec->finishWhy().c_str());
		ExpectRefusedChild("frame_back_firstlast", dir, Refusal::Incomplete);
	}
	{
		const std::wstring dir = Dir("frame_back_all");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 4, "all", why), "open: %s", why.c_str());
		if(!r.rec)
			return;
		r.rec->onPlots(kPlotsBaseline, {Rec(1, -1), Rec(2, -1)}, 1, 2, 1);   // a feed record (an empty feed.bin is refused)
		r.iterate(5);
		r.iterate(6);
		r.iterate(3);   // a new composite: frame ids restart
		for(int i = 0; i < 3; ++i)
			r.iterate(0);
		r.end();
		CheckResult res;
		Manifest m;
		CHECK(r.rec->complete() && CheckBundle(dir, &res).ok(), "complete %d: %s / %s", (int)r.rec->complete(), r.rec->finishWhy().c_str(),
		      res.line().c_str());
		CHECK(ReadManifest(dir, &m, &res).ok() && m.frameCount == 2 && m.dumpedFrames.size() == 2, "short span manifest");
		bool note = false;
		for(const auto& n : m.versionNotes)
			note = note || n.find("short span: 2 of 4 frames") != std::string::npos;
		CHECK(note, "no short-span note");
	}
	{
		const std::wstring dir = Dir("empty_feed");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 2, "all", why), "open: %s", why.c_str());
		if(!r.rec)
			return;
		r.iterate(1);
		r.iterate(2);
		r.end();
		CHECK(!r.rec->complete() && r.rec->finishWhy().find("the feed recorded nothing") != std::string::npos, "why: %s",
		      r.rec->finishWhy().c_str());
		ExpectRefusedChild("empty_feed", dir, Refusal::Incomplete);
	}
	{
		const std::wstring dir = Dir("overlay_failed");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 2, nullptr, why), "open: %s", why.c_str());
		if(!r.rec)
			return;
		OverlayFrame p;
		p.srv = (ID3D11ShaderResourceView*)(uintptr_t)0x1000;   // compared only, never used
		p.generation = 9;
		p.numViews = 1;
		r.rec->onPlots(kPlotsBaseline, {Rec(1, -1), Rec(2, -1)}, 1, 2, 1);
		r.rec->onOverlayPair(p);
		r.rec->onOverlayReadbackError(p, "synthetic Map failure");
		r.rec->beginIteration();
		double now = 0;
		SceneFrame fr;
		fr.cam = Camera(1);
		fr.frameId = 1;
		CHECK(r.rec->beforeDraw(1, 0, r.water, &now), "frame 1 not in the span");
		c5r::terrain::Overlay o;
		o.srv = p.srv;
		r.target.fill(1);
		r.rec->afterDraw(g_ctx, fr, now, kW, kH, r.target.rtv, r.target.dsv, o, 0);
		r.rec->afterEnd(g_ctx);
		fr.cam = Camera(2);
		fr.frameId = 2;
		CHECK(r.rec->beforeDraw(2, 1, r.water, &now), "frame 2 not in the span");
		r.rec->afterDraw(g_ctx, fr, now, kW, kH, r.target.rtv, r.target.dsv, o, 0);
		r.rec->afterEnd(g_ctx);
		r.rec->step(RecorderCounters());
		r.end();
		CHECK(!r.rec->complete() && r.rec->finishWhy().find("synthetic Map failure") != std::string::npos, "why: %s", r.rec->finishWhy().c_str());
		ExpectRefusedChild("overlay_failed", dir, Refusal::Incomplete);
	}
	{
		// a target the recorder cannot read as float depth: no dump, a record error, and so an incomplete bundle
		const std::wstring dir = Dir("depth_d24");
		Rig r;
		std::string why;
		CHECK(r.open(dir, 2, nullptr, why), "open: %s", why.c_str());
		if(!r.rec)
			return;
		Target d24(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
		for(int64_t f = 1; f <= 2; ++f)
		{
			r.rec->beginIteration();
			double now = 0;
			SceneFrame fr;
			fr.cam = Camera(f);
			fr.frameId = f;
			CHECK(r.rec->beforeDraw(f, f - 1, r.water, &now), "frame not in the span");
			r.rec->afterDraw(g_ctx, fr, now, kW, kH, d24.rtv, d24.dsv, c5r::terrain::Overlay(), 0);
			r.rec->afterEnd(g_ctx);
		}
		r.rec->step(RecorderCounters());
		r.end();
		CHECK(!r.rec->complete() && r.rec->recordErrors() == 2 && CountLog("not readable as 160x120 float32") >= 1, "errors %lld: %s",
		      (long long)r.rec->recordErrors(), r.rec->finishWhy().c_str());
		ExpectRefusedChild("depth_d24", dir, Refusal::Incomplete);
	}
}

// ---- issue 0440 (format version 5): the unit scene's history and database -------------------------------------------------------
// A unit scene that only answers ready() as the test sets it (the recorder reads nothing else of it).
struct FakeUnits : srvunits::SceneUnits
{
	bool isReady = false;
	void setDevice(ID3D11Device*) override {}
	bool feed(const SrvFeed::Batch&, double) override { return false; }
	void useFeedSource(srvrender::FeedSource*) override {}
	void poll() override {}
	void setPlotStates(const uint8_t*, size_t, uint32_t, uint32_t, uint32_t) override {}
	void beginFrame(const srvunits::UnitsCamera&, const c5r::heightfield::HeightGrid*) override {}
	void drawReflection(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, const float*) override {}
	void drawScene(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, uint32_t, const srvunits::UnitsCamera&,
	               const c5r::heightfield::HeightGrid*) override {}
	bool ready() const override { return isReady; }
	bool failed(std::string&) const override { return false; }
	bool animating() const override { return false; }
	void secondLine(char* out, size_t n) override { if(n) out[0] = 0; }
	void totalsLine(char* out, size_t n) override { if(n) out[0] = 0; }
};

// One recording of frames 1..7 (the span 6..7: render_test_record_after 6, 2 frames) with the unit scene ready from
// `readyFrom` (0: never) and a database (gen 3) unless `noDb`; unitsOn false: no setUnits (render_units 0).
struct UnitsRun
{
	Rig r;
	FakeUnits units;
	std::wstring db;
	bool ok = false;
	void run(const std::wstring& dir, bool unitsOn, int readyFrom, bool noDb)
	{
		std::string why;
		CHECK(r.open(dir, 2, nullptr, why, 6), "open: %s", why.c_str());
		if(!r.rec)
			return;
		db = g_work + L"/units_test_" + dir.substr(dir.rfind(L'/') + 1) + L".db";
		std::vector<uint8_t> b(8192, 0x5A);
		memcpy(b.data(), "SQLite format 3", 16);
		CHECK(WriteBytes(db, b.data(), b.size()), "the test database");
		if(unitsOn)
		{
			r.rec->setRenderConfig("render_units", "1");
			const std::wstring path = db;
			r.rec->setUnits({&units, [path, noDb](std::wstring& p, uint32_t& g) {
				                 if(noDb)
					                 return false;
				                 p = path;
				                 g = 3;
				                 return true;
			                 }});
		}
		r.rec->onPlots(kPlotsBaseline, {Rec(1, -1), Rec(2, -1)}, 1, 2, 1);   // the feed is not empty
		for(int64_t f = 1; f <= 7; ++f)
		{
			units.isReady = readyFrom > 0 && f >= readyFrom;
			r.iterate(f);
		}
		r.end();
		ok = true;
	}
};

void TestUnits()
{
	{   // ready from frame 4: the history is frames 3 (the controller's first), 4 and 5; the database copied whole
		const std::wstring dir = Dir("units_ok");
		UnitsRun u;
		u.run(dir, true, 4, false);
		if(!u.ok)
			return;
		CHECK(u.r.rec->complete(), "units_ok incomplete: %s", u.r.rec->finishWhy().c_str());
		CheckResult res;
		CHECK(CheckBundle(dir, &res).ok(), "units_ok refused: %s", res.line().c_str());
		CHECK(u.r.preSpanClocks.size() == 5, "%zu frames outside the span", u.r.preSpanClocks.size());
		for(size_t i = 0; i < u.r.preSpanClocks.size(); ++i)
			CHECK(u.r.preSpanClocks[i] >= 0 && (!i || u.r.preSpanClocks[i] >= u.r.preSpanClocks[i - 1]), "frame %zu outside the span: clock %.3f",
			      i + 1, u.r.preSpanClocks[i]);
		Manifest m;
		CHECK(ReadManifest(dir, &m, &res).ok(), "manifest: %s", res.line().c_str());
		std::vector<uint8_t> b, want;
		CHECK(ReadMember(dir, m, "units/gen0000000003.db", &b, &res).ok() && b.size() == 8192 && !memcmp(b.data(), "SQLite format 3", 16),
		      "units/gen0000000003.db: %s", res.line().c_str());
		UnitsHistoryFile h;
		CHECK(ReadMember(dir, m, "units_history.bin", &b, &res).ok() && ParseUnitsHistory(b, &h, &res).ok(), "units_history.bin: %s",
		      res.line().c_str());
		CHECK(h.dbGeneration == 3 && h.flags == kUnitsHistReady && h.records.size() == 3, "history head: gen %u, flags %u, %zu records",
		      h.dbGeneration, h.flags, h.records.size());
		for(size_t i = 0; i < h.records.size() && h.records.size() == 3; ++i)
		{
			const UnitsHistoryRecord& hr = h.records[i];
			const VPShellCamera c = Camera(3 + (int64_t)i);
			CHECK(hr.frameId == 3 + (int64_t)i && hr.nowMs == u.r.preSpanClocks[2 + i] && hr.flags == (i ? kUnitsHistReady : 0u) &&
			          !memcmp(&hr.cam, &c, sizeof(c)),
			      "history record %zu: frame %lld, clock %.3f (beforeDraw gave %.3f), flags %u, camera %s", i, (long long)hr.frameId, hr.nowMs,
			      u.r.preSpanClocks[2 + i], hr.flags, memcmp(&hr.cam, &c, sizeof(c)) ? "differs" : "equal");
		}
		bool gap = false;
		for(const std::string& n : m.versionNotes)
			gap = gap || n.compare(0, 11, "gap: units:") == 0;
		CHECK(!gap && m.renderConfig["render_units"] == "1", "no units gap note expected");
		printf("ok   units: history of %zu frames, the database member\n", h.records.size());
	}
	{   // ready only at the span's first frame: the history is the frame right before it
		const std::wstring dir = Dir("units_ready_at_span");
		UnitsRun u;
		u.run(dir, true, 6, false);
		if(!u.ok)
			return;
		CheckResult res;
		Manifest m;
		std::vector<uint8_t> b;
		UnitsHistoryFile h;
		CHECK(u.r.rec->complete() && CheckBundle(dir, &res).ok() && ReadManifest(dir, &m, &res).ok() &&
		          ReadMember(dir, m, "units_history.bin", &b, &res).ok() && ParseUnitsHistory(b, &h, &res).ok() && h.records.size() == 1 &&
		          h.records[0].frameId == 5 && h.flags == kUnitsHistReady,
		      "ready at the span: %s (%zu records)", res.line().c_str(), h.records.size());
	}
	{   // never ready: the database, and the gap note for the controller's state; the checker accepts it
		const std::wstring dir = Dir("units_never_ready");
		UnitsRun u;
		u.run(dir, true, 0, false);
		if(!u.ok)
			return;
		CheckResult res;
		Manifest m;
		CHECK(u.r.rec->complete() && CheckBundle(dir, &res).ok() && ReadManifest(dir, &m, &res).ok(), "never ready: %s", res.line().c_str());
		std::string note;
		for(const std::string& n : m.versionNotes)
			if(n.compare(0, strlen(kUnitsNoHistoryNote), kUnitsNoHistoryNote) == 0)
				note = n;
		CHECK(!m.members.count("units_history.bin") && m.members.count("units/gen0000000003.db") && note.find("was not ready") != std::string::npos,
		      "never ready: note '%s'", note.c_str());
	}
	{   // no database: its gap note, the history as usual
		const std::wstring dir = Dir("units_no_db");
		UnitsRun u;
		u.run(dir, true, 4, true);
		if(!u.ok)
			return;
		CheckResult res;
		Manifest m;
		CHECK(u.r.rec->complete() && CheckBundle(dir, &res).ok() && ReadManifest(dir, &m, &res).ok(), "no database: %s", res.line().c_str());
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || n.compare(0, strlen(kUnitsNoDbNote), kUnitsNoDbNote) == 0;
		CHECK(note && m.members.count("units_history.bin") && !m.members.count("units/gen0000000003.db"), "no database: note %d", note ? 1 : 0);
	}
	{   // units not recorded (render_units 0): no clock outside the span (the frames as before 0440), no member, no note
		const std::wstring dir = Dir("units_off");
		UnitsRun u;
		u.run(dir, false, 4, false);
		if(!u.ok)
			return;
		CheckResult res;
		Manifest m;
		CHECK(u.r.rec->complete() && CheckBundle(dir, &res).ok() && ReadManifest(dir, &m, &res).ok(), "units off: %s", res.line().c_str());
		bool clockGiven = false;
		for(double c : u.r.preSpanClocks)
			clockGiven = clockGiven || c != -1;
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || n.compare(0, 11, "gap: units:") == 0;
		CHECK(!clockGiven && u.r.preSpanClocks.size() == 5 && !note && !m.members.count("units_history.bin") && m.members.size() > 0,
		      "units off: a clock outside the span %d, a note %d", clockGiven ? 1 : 0, note ? 1 : 0);
		for(const auto& kv : m.members)
			CHECK(kv.second.kind != MemberKind::UnitsDb, "units off: a units_db member %s", kv.first.c_str());
	}
	printf("ok   units (format version 5)\n");
}

// P2 (contract 7): the recorded feed.bin, read back through 3902's parser and replayed with 3903's file feed into a second
// scene, gives the residency the same filter decisions and counters as live, across a snapshot (a load), plot edits in
// the span and a forced overflow.
// Ticket 4749 (4785's rule A gap, offline-scene-harness.md 38 and 40): a recording whose bound shadow triple's slot said the
// EXE's unit mask draws were dropped (live with ServerUnits = 1 and render_units_shadow = 1: the server merges its own units'
// mask) must hand the player that flag and ShadowRaw's size per frame, so that the replayed frame passes the scene's merge
// rule (UnitShadowWanted, Scene::unitShadowStep) as the live one did. Frame 1 carries the flag (raw 160 x 120), frame 2 does
// not. Written against the API both formats share (ShadowFrame, LoadSeamGMembers, UnitShadowWanted): with format version 9
// it compiles and FAILS (the record has no field: the replayed flag is clear, the frame is drawn without unit shadows).
void TestUnitShadowMask()
{
	const std::wstring dir = Dir("unit_shadow_mask");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 2, "all", why), "open: %s", why.c_str());
	if(!r.rec)
		return;
	r.rec->onPlots(kPlotsBaseline, {Rec(1, -1), Rec(2, -1)}, 1, 2, 1);   // a feed record (an empty feed.bin is refused)
	// the tapped map: R16_UNORM 4 x 4 as at quality 3 (the slot's ShadowBlur), on the WARP device
	uint16_t texels[16];
	for(int i = 0; i < 16; ++i)
		texels[i] = (uint16_t)(1000 * i);
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 4;
	d.MipLevels = d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R16_UNORM;
	d.SampleDesc.Count = 1;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = {texels, 8, 0};
	ID3D11Texture2D* tex = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	if(SUCCEEDED(g_dev->CreateTexture2D(&d, &sd, &tex)))
		g_dev->CreateShaderResourceView(tex, nullptr, &srv);
	SafeRelease(tex);
	CHECK(srv != nullptr, "the shadow map's texture");
	if(!srv)
		return;
	ShadowFrame sf[2];
	for(int k = 0; k < 2; ++k)
	{
		sf[k].srv = srv;
		sf[k].format = 56;
		sf[k].width = sf[k].height = 4;
		sf[k].numViews = 1;
		for(int i = 0; i < 4; ++i)
			sf[k].matrices[0][5 * i] = 1.0f;
		sf[k].strength = 1.0f;
		sf[k].presentCount = 700 + k;
		sf[k].generation = 40 + k;
		sf[k].quality = 3;
		sf[k].rawWidth = kW;
		sf[k].rawHeight = kH;
	}
	sf[0].unitMaskDropped = true;    // the live slot's VPSHD_SLOT_UNIT_MASK_DROPPED
	sf[1].unitMaskDropped = false;
	r.iterate(1, 0, &sf[0]);
	r.iterate(2, 0, &sf[1]);
	for(int i = 0; i < 3; ++i)
		r.iterate(0);
	r.end();
	SafeRelease(srv);
	CheckResult res;
	Manifest m;
	CHECK(r.rec->complete() && CheckBundle(dir, &res).ok(), "unit_shadow_mask: complete %d: %s / %s", (int)r.rec->complete(),
	      r.rec->finishWhy().c_str(), res.line().c_str());
	CHECK(ReadManifest(dir, &m, &res).ok() && m.members.count("shadow.bin"), "manifest: %s", res.line().c_str());
	// the player's side, as sceneshot loads it
	std::unique_ptr<ShadowSource> ss;
	std::unique_ptr<IceSource> is;
	FileSeamGControl* shCtl = nullptr;
	FileSeamGControl* iceCtl = nullptr;
	std::vector<ShadowRecord> recs;
	std::string note;
	const bool loaded = LoadSeamGMembers(g_dev, dir, m, &ss, &shCtl, &is, &iceCtl, &recs, &note, &why);
	CHECK(loaded && ss && shCtl && recs.size() == 2, "LoadSeamGMembers: %s (%zu records)", why.c_str(), recs.size());
	if(!loaded || !ss || !shCtl || recs.size() != 2)
		return;
	for(uint32_t k = 0; k < 2; ++k)
	{
		shCtl->seekFrame(k);
		ShadowFrame got;
		const bool have = ss->acquire(got);
		const char* w = nullptr;
		const bool merge = have && UnitShadowWanted(true, true, got.unitMaskDropped, got.quality, got.srv != nullptr, got.rawWidth,
		                                            got.rawHeight, &w);
		CHECK(have && got.unitMaskDropped == sf[k].unitMaskDropped && got.rawWidth == kW && got.rawHeight == kH,
		      "frame %u replayed: have %d, unit mask dropped %d (live %d), raw %u x %u (live %u x %u)", k + 1, (int)have,
		      (int)got.unitMaskDropped, (int)sf[k].unitMaskDropped, got.rawWidth, got.rawHeight, kW, kH);
		CHECK(merge == sf[k].unitMaskDropped, "frame %u: the scene's merge rule says %d, live %d (%s): %s", k + 1, (int)merge,
		      (int)sf[k].unitMaskDropped, w ? w : "", sf[k].unitMaskDropped ? "REPLAYED WITHOUT THE UNIT GROUND SHADOWS" : "a merge live had not");
	}
}

// Ticket 0622 (format version 11): water_frames.bin names the copy each frame was drawn with. A span of 4 frames in the
// host's order (the water sink inside the iteration's poll, before the draw): frame 12 before any copy, copy 1 accepted
// before frame 13, copy 2 of the same load (a Waves_Reload) before frame 14; frames 14 and 15 drawn with copy 2. The bundle
// passes the checker and every record names the member whose bytes the sink was handed.
void TestWaterCopies()
{
	const std::wstring dir = Dir("water_copies");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 4, nullptr, why, 3), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1));
	SrvFeed::EndSnapshot();
	const std::vector<uint8_t> c5 = FieldBytes();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	std::vector<SrvFeed::PlotRecord> t;
	uint32_t gen = 0, w = 0, h = 0;
	r.feed->copyPlots(t, gen, w, h, kPlotsBaseline);
	r.fake->give = false;
	r.iterate(10);
	r.iterate(11);
	r.iterate(12);   // the span's first frame: no water copy yet
	const WaterParts w1 = MakeWater(1, 1), w2 = MakeWater(1, 2);
	SceneRecorder::WaterSink(r.rec.get(), w1.block, w1.image.data(), w1.image.size(), true, 1, 256, 256);
	r.iterate(13, kFrameWaterModule);
	SceneRecorder::WaterSink(r.rec.get(), w2.block, w2.image.data(), w2.image.size(), true, 1, 256, 256);
	r.iterate(14, kFrameWaterModule);
	r.iterate(15, kFrameWaterModule);
	for(int i = 0; i < 4 && !r.rec->finished(); ++i)
		r.iterate(0);
	CHECK(r.rec->finished() && r.rec->complete(), "finished %d complete %d: %s", (int)r.rec->finished(), (int)r.rec->complete(),
	      r.rec->finishWhy().c_str());
	r.end();
	SrvFeed::SetEnabled(false);
	CheckResult res;
	CHECK(CheckBundle(dir, &res).ok(), "checker: %s", res.line().c_str());
	Manifest m;
	CHECK(ReadManifest(dir, &m, &res).ok() && m.formatVersion == kFormatVersion && kFormatVersion >= kWaterFramesFormatVersion,
	      "manifest: %s", res.line().c_str());
	CHECK(m.members.count("water/load0001.wtap") && m.members.count("water/load0001-c2.wtap") && m.members.count("water_frames.bin") &&
	          m.members["water_frames.bin"].kind == MemberKind::WaterFrames,
	      "members: the two copies and water_frames.bin");
	std::vector<uint8_t> b;
	std::vector<WaterFrameRecord> wf;
	CHECK(ReadMember(dir, m, "water_frames.bin", &b, &res).ok() && ParseWaterFrames(b, &wf, &res).ok() && wf.size() == 4,
	      "water_frames.bin: %s, %zu records", res.line().c_str(), wf.size());
	if(wf.size() == 4)
	{
		const std::string s1 = m.members["water/load0001.wtap"].sha256, s2 = m.members["water/load0001-c2.wtap"].sha256;
		CHECK(wf[0].frameId == 12 && wf[1].frameId == 13 && wf[2].frameId == 14 && wf[3].frameId == 15, "frame ids");
		CHECK(wf[0].flags == 0 && wf[0].loadSeq == 0 && wf[0].copySeq == 0 && Sha256HexOf(wf[0].wtapSha256) == std::string(64, '0'),
		      "frame 12: no copy in force");
		CHECK(wf[1].flags == kWaterCopyInForce && wf[1].loadSeq == 1 && wf[1].copySeq == 1 && Sha256HexOf(wf[1].wtapSha256) == s1,
		      "frame 13: copy 1 (%s)", Sha256HexOf(wf[1].wtapSha256).c_str());
		for(int k = 2; k < 4; ++k)
			CHECK(wf[k].flags == kWaterCopyInForce && wf[k].loadSeq == 1 && wf[k].copySeq == 2 && Sha256HexOf(wf[k].wtapSha256) == s2,
			      "frame %d: copy 2 (%s)", 12 + k, Sha256HexOf(wf[k].wtapSha256).c_str());
	}
	printf("ok   water copies per frame (format %u)\n", kFormatVersion);
}

void TestP2Replay()
{
	CHECK(!g_exe.empty() && GetFileAttributesW(g_exe.c_str()) != INVALID_FILE_ATTRIBUTES, "C5R_EXE missing (%S): the P2 case needs it",
	      g_exe.c_str());
	if(g_exe.empty() || GetFileAttributesW(g_exe.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	const std::wstring dir = Dir("p2_replay");
	Rig r;
	std::string why;
	CHECK(r.open(dir, 2, nullptr, why, 1), "open: %s", why.c_str());
	if(!r.rec)
		return;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	SrvFeed::SetQueueCap(4);
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1));
	SrvFeed::EndSnapshot();
	SceneOptions o;
	o.exe = g_exe;
	o.tiles = SceneOptions::kTilesBake;
	o.bake.pipeline = false;
	o.water = false;
	o.color = c5r::terrain::ColorSource::Constant;
	o.fallback = c5r::terrain::ColorSource::Constant;
	o.overlay = 0;
	o.bake.feed = r.feed.get();
	Scene a;
	c5r::Status st = a.init(g_dev, g_ctx, o);
	CHECK(st.ok(), "scene A: %s", st.str().c_str());
	const std::vector<uint8_t> c5 = FieldBytes();
	const VPShellCamera cam = Camera(1);
	r.rec->beginIteration();
	SceneRecorder::FieldSink(r.rec.get(), 1, c5.data(), c5.size());
	CHECK(a.loadField(c5.data(), c5.size(), 1, &cam).ok(), "field A");
	auto loop = [&](uint64_t it) {
		(void)it;
		r.rec->beginIteration();
		SrvFeed::Batch bt;
		if(r.feed->drain(bt) && !bt.changedPlots.empty())
			a.plotsChanged(bt.changedPlots.data(), bt.changedPlots.size());
	};
	loop(2);                                   // the snapshot's own plots
	SrvFeed::SetPlot(5, Rec(1, 7));            // an improvement: re-baked
	SrvFeed::SetPlot(6, Rec(1, -1));           // nothing a bake reads: filtered
	loop(3);
	for(uint32_t i = 0; i < 10; ++i)           // more events than the cap of 4: an overflow, the changed plots kept
	{
		SrvFeed::Event e = {};
		e.kind = (uint16_t)SrvFeed::kPlotState;
		e.plot = (int32_t)(i % 16);
		e.turn = 3;
		SrvFeed::Push(e);
		SrvFeed::SetPlot(i % 16, Rec((int8_t)(i % 3 == 0 ? 2 : 1), -1));
	}
	loop(4);
	SrvFeed::BeginSnapshot(4, 4);              // a second load: a new generation, a new baseline
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(3, -1));
	SrvFeed::EndSnapshot();
	loop(5);
	SrvFeed::SetPlot(2, Rec(3, 4));
	loop(6);
	ResidencySource* ra = static_cast<ResidencySource*>(a.source());
	CHECK(ra && ra->plotsPassed > 0 && ra->plotsFiltered > 0, "live: passed %lld filtered %lld", ra ? (long long)ra->plotsPassed : -1LL,
	      ra ? (long long)ra->plotsFiltered : -1LL);
	// two frames to close the bundle
	for(int64_t f = 1; f <= 2; ++f)
	{
		r.rec->beginIteration();
		double now = 0;
		SceneFrame fr;
		fr.cam = Camera(f);
		fr.frameId = f;
		if(r.rec->beforeDraw(f, f - 1, r.water, &now))
		{
			r.target.fill((int)f);
			r.rec->afterDraw(g_ctx, fr, now, kW, kH, r.target.rtv, r.target.dsv, c5r::terrain::Overlay(), 0);
			r.rec->afterEnd(g_ctx);
		}
	}
	r.rec->step(RecorderCounters());
	r.end();
	SrvFeed::SetQueueCap(65536);
	CheckResult res;
	CHECK(r.rec->complete() && CheckBundle(dir, &res).ok(), "bundle: %s / %s", r.rec->finishWhy().c_str(), res.line().c_str());
	Manifest m;
	std::vector<uint8_t> b;
	std::vector<FeedRecord> recs;
	CHECK(ReadManifest(dir, &m, &res).ok() && ReadMember(dir, m, "feed.bin", &b, &res).ok() && ParseFeed(b, &recs, &res).ok(), "feed.bin: %s",
	      res.line().c_str());
	CHECK(m.counters["feed_overflow_events"] >= 1, "overflow events %lld", (long long)m.counters["feed_overflow_events"]);
	std::vector<FeedCall> calls;
	for(const FeedRecord& fr : recs)
	{
		FeedCall c;
		c.type = fr.type;
		c.loopIter = fr.loopIter;
		c.batch = fr.batch;
		c.caller = fr.caller;
		c.gen = fr.snapshotGen;
		c.width = fr.width;
		c.height = fr.height;
		c.table = fr.plots;
		calls.push_back(c);
	}
	FileFeedControl* ctl = nullptr;
	std::unique_ptr<FeedSource> file = MakeFileFeed(calls, &ctl, &why);
	CHECK(file && ctl, "file feed refused: %s", why.c_str());
	if(!file)
	{
		a.releaseDevice();
		SrvFeed::SetEnabled(false);
		return;
	}
	SceneOptions ob = o;
	ob.bake.feed = file.get();
	Scene bsc;
	CHECK(bsc.init(g_dev, g_ctx, ob).ok(), "scene B");
	ctl->setLoopIter(1);
	CHECK(bsc.loadField(c5.data(), c5.size(), 1, &cam).ok(), "field B");
	for(uint64_t it = 2; it <= 9; ++it)
	{
		ctl->setLoopIter(it);
		SrvFeed::Batch bt;
		if(file->drain(bt) && !bt.changedPlots.empty())
			bsc.plotsChanged(bt.changedPlots.data(), bt.changedPlots.size());
	}
	ResidencySource* rb = static_cast<ResidencySource*>(bsc.source());
	CHECK(rb && !ctl->failed() && ctl->remaining() == 0, "replay: failed %d (%s), %zu left", (int)ctl->failed(), ctl->why().c_str(),
	      ctl->remaining());
	if(ra && rb)
	{
		CHECK(rb->plotsChanged == ra->plotsChanged && rb->plotsPassed == ra->plotsPassed && rb->plotsFiltered == ra->plotsFiltered,
		      "counters live %lld/%lld/%lld replay %lld/%lld/%lld", (long long)ra->plotsChanged, (long long)ra->plotsPassed,
		      (long long)ra->plotsFiltered, (long long)rb->plotsChanged, (long long)rb->plotsPassed, (long long)rb->plotsFiltered);
		CHECK(rb->stats()->raises == ra->stats()->raises && rb->stats()->plotsChanged == ra->stats()->plotsChanged, "residency raises %lld / %lld",
		      (long long)rb->stats()->raises, (long long)ra->stats()->raises);
		printf("ok   P2 replay: %zu feed records, changed %lld passed %lld filtered %lld, overflow events %lld\n", recs.size(),
		       (long long)ra->plotsChanged, (long long)ra->plotsPassed, (long long)ra->plotsFiltered,
		       (long long)m.counters["feed_overflow_events"]);
	}
	// a feed.bin with its PLOTS records removed is refused, not replayed (contract 7)
	std::vector<FeedCall> noPlots;
	for(const FeedCall& c : calls)
		if(c.type == kFeedDrain)
			noPlots.push_back(c);
	CHECK(!MakeFileFeed(noPlots, nullptr, &why) && !why.empty(), "a feed without its tables was accepted");
	a.releaseDevice();
	bsc.releaseDevice();
	SrvFeed::SetEnabled(false);
}

// ---- --stamp ---------------------------------------------------------------------------------------------------------------
int Stamp(const std::wstring& dir, int argc, wchar_t** argv)
{
	Manifest m;
	CheckResult res;
	if(!ReadManifest(dir, &m, &res).ok())
	{
		printf("%s\n", res.line().c_str());
		return RefusalExitCode(res.refusal);
	}
	for(int i = 0; i < argc; ++i)
	{
		const std::string a = srvrender::scenebundle::Narrow(argv[i]);
		const size_t eq = a.find('=');
		if(eq == std::string::npos || !eq)
		{
			printf("recorder_test --stamp: not KEY=VALUE: %s\n", a.c_str());
			return 2;
		}
		const std::string k = a.substr(0, eq), v = a.substr(eq + 1);
		if(k == "save.name")
			m.saveName = v;
		else if(k == "save.sha256")
			m.saveSha256 = v;
		else if(k == "save.turn")
			m.turn = (uint32_t)strtoul(v.c_str(), nullptr, 10);
		else if(k == "repo_commit")
			m.repoCommit = v;
		else if(k == "build_config")
			m.buildConfig = v;
		else if(k.compare(0, 5, "pins.") == 0 && k.size() > 5)
			m.pins[k.substr(5)] = v;
		else if(k == "note")
			m.versionNotes.push_back(v);
		else
		{
			printf("recorder_test --stamp: unknown key %s (save.name, save.sha256, save.turn, repo_commit, build_config, pins.<name>, note)\n",
			       k.c_str());
			return 2;
		}
	}
	const std::string js = ManifestToJson(m);
	const std::wstring tmp = dir + L"/scene.json.tmp", fin = dir + L"/scene.json";
	if(!WriteBytes(tmp, js.data(), js.size()) || !MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		printf("recorder_test --stamp: cannot write scene.json\n");
		return 1;
	}
	CheckBundle(dir, &res);
	printf("%s\n", res.line().c_str());
	return RefusalExitCode(res.refusal);
}

}  // namespace

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
	if(argc >= 3 && !wcscmp(argv[1], L"--stamp"))
		return Stamp(argv[2], argc - 3, argv + 3);
	if(argc != 3 || wcscmp(argv[1], L"--work"))
	{
		printf("usage: recorder_test --work DIR | --check DIR | --stamp DIR KEY=VALUE ...\n");
		return 2;
	}
	g_work = argv[2];
	for(wchar_t& c : g_work)
		if(c == L'\\')
			c = L'/';
	CreateDirectoryW(g_work.c_str(), NULL);
	LogOpen(nullptr, HostLog);
	wchar_t exe[MAX_PATH] = L"";
	if(GetEnvironmentVariableW(L"C5R_EXE", exe, MAX_PATH))
		g_exe = exe;
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("recorder_test: no WARP device\nRESULT: 0 passed, 1 failed, 0 skipped\n");
		return 1;
	}
	TestKeys();
	TestBundle();
	TestFog(true);    // issue 3911
	TestFog(false);
	for(int v = 0; v < 5; ++v)   // ticket 0403
		TestFogParticles(v);
	TestDecals(0);    // issue 0364
	TestDecals(1);
	TestDecals(2);
	TestUnwritable();
	TestDiskFull();
	TestShortSpans();
	TestUnits();      // issue 0440
	TestUnitShadowMask();   // ticket 4749
	TestWaterCopies();      // ticket 0622
	TestP2Replay();
	g_ctx->Release();
	g_dev->Release();
	printf("RESULT: %d passed, %d failed, 0 skipped\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
