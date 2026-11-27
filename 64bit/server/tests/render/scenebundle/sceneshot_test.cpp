// sceneshot_test.cpp - the tests of the offline scene harness's player, server/tools/sceneshot (epic 0301, task 3905;
// contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 7, 10 and "sceneshot").
// Written 2026-11-15 for this repository (GPL-3.0-or-later). The field, camera, water image and overlay builders are
// copied from server/tests/render/scenebundle/sources_test.cpp (task 3903, this repository), the field split into land
// and ocean here.
//
// Everything in a bundle here is synthetic (D83: nothing from the game's files is stored): the "live" side is this
// program's own run of the server's scene, in the host's order (each loop iteration: the feed drained through the
// recording wrapper, the changed plots to the scene, the overlay polled, the frame drawn into targets of the sinks'
// formats), whose inputs, frames and readbacks (the dumps) are written with 3902's Writer as the recorder (3904) writes
// them. The player (sceneshot::Run, compiled in from server/tools/sceneshot/sceneshot.cpp) then replays the bundle and
// its exit code and output lines are asserted. The GPU inputs: the installed game's EXE (shaders) and water art
// (read-only; C5_GAME_DIR, else C5R_GAME_DIR, else the setting render_game_dir, never the Steam install, and only build
// 4390913: ../test_install.h, ticket 0589); a missing input FAILS (COMMON 9). Every case runs on WARP and on the
// first hardware adapter (no hardware adapter: the hardware cases fail).
//
//   sceneshot_test.exe --work DIR [case ...]       last line: RESULT: <n> passed, <m> failed, 0 skipped
#define SCENESHOT_NO_MAIN
#include "../../../tools/sceneshot/sceneshot.cpp"

#include "srv_water_tap.h"
#include "c5r/heightfield/heightfield.h"
#include "decal_image_fixture.h"   // issue 0364
#include "../test_install.h"       // ticket 0589

using namespace srvrender;
namespace sb = srvrender::scenebundle;
namespace c5h = c5r::heightfield;

namespace {

int g_pass = 0, g_fail = 0;
std::wstring g_work, g_game;
std::vector<std::string> g_lines;   // the player's output of the last Run
std::vector<std::string> g_log;     // the render units' notes of the live side

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
			for(const std::string& l_ : g_lines)                                                                               \
				printf("    out: %s\n", l_.c_str());                                                                           \
			fflush(stdout);                                                                                                    \
		}                                                                                                                      \
	} while(0)

void Collect(void*, const char* line) { g_lines.push_back(line); }
void LiveLog(const char* line) { g_log.push_back(line); }

std::string NarrowW(const std::wstring& w) { return sb::Narrow(w); }

bool Has(const char* needle)
{
	for(const std::string& l : g_lines)
		if(l.find(needle) != std::string::npos)
			return true;
	return false;
}
int Count(const char* needle)
{
	int n = 0;
	for(const std::string& l : g_lines)
		if(l.find(needle) != std::string::npos)
			n++;
	return n;
}
std::string Last() { return g_lines.empty() ? std::string() : g_lines.back(); }

int Play(const std::vector<std::string>& args)
{
	g_lines.clear();
	const int rc = sceneshot::Run(args, &Collect, nullptr);
	LogOpen(nullptr, &LiveLog);   // Run resets the render units' log
	return rc;
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void RemoveTree(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
				continue;
			const std::wstring p = dir + L"\\" + fd.cFileName;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveTree(p);
			else
				DeleteFileW(p.c_str());
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out)
{
	out.clear();
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"rb") || !f)
		return false;
	uint8_t buf[65536];
	size_t k;
	while((k = fread(buf, 1, sizeof(buf), f)) > 0)
		out.insert(out.end(), buf, buf + k);
	fclose(f);
	return true;
}

bool WriteAll(const std::wstring& path, const std::vector<uint8_t>& b) { return sceneshot::WriteFileBytes(path, b.data(), b.size()); }

uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

// ---- synthetic inputs (sources_test.cpp's builders) ------------------------------------------------------------------------
const uint32_t kW = 160, kH = 120, kSamples = 256;

// 4 x 4 plots (256 x 256 samples): the left half land (20 << 8, class 2), the right half below the water at z 10
// issue 3911: the fog case's field is 8 x 8 grid plots (an 8 x 4 table + 4 margin rows; 8 plots wide, so that FOWAnim's
// k = floor(W 0.002) / W is not 0: W = 511)
// Ticket 0199 part 2: with placed, the EXE's hex-to-world placement M_h2w ([0xD91030], 64 samples per plot, odd rows half a
// plot right, the plot's centre at +32 / +32 samples; a live Tiny tap has the same rows with a +128 row offset for its
// margin rows), so that c5r::arc::plotToWorldXY puts plots (1, 1) and (2, 2) at world (0, -27.7) and (32, 27.7), inside
// the synthetic cameras' view. The other cases keep the zero matrix (the field's bytes and their bundles unchanged).
std::vector<uint8_t> FieldBytes(uint32_t mapH = 4, uint32_t mapW = 4, bool placed = false)
{
	c5h::HeightGrid g;
	if(placed)
	{
		const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 32, 0, 1};
		memcpy(g.hexToWorld, m, sizeof(m));
	}
	g.mapW = mapW;
	g.mapH = mapH;
	g.samplesX = mapW * 64;
	g.samplesY = mapH * 64;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = (float)g.samplesX / 2.0f;
	g.oy = (float)g.samplesY / 2.0f * 0.866f;
	g.waterZ = 10.0f;
	g.h.resize((size_t)g.samplesX * g.samplesY);
	for(uint32_t y = 0; y < g.samplesY; ++y)
		for(uint32_t x = 0; x < g.samplesX; ++x)
			g.h[(size_t)y * g.samplesX + x] = x < 128 ? (uint16_t)(20 << 8) : (uint16_t)0;
	std::vector<uint8_t> img;
	c5h::writeTap(g, img);
	return img;
}

// sources_test.cpp Main::camera: a camera at (cx, 0, z) looking straight down, the grid constants of the field
VPShellCamera Camera(float cx, int64_t frame, float z = 160.0f, uint32_t samplesY = kSamples, float cy = 0.0f,
                     uint32_t samplesX = kSamples)
{
	VPShellCamera c;
	memset(&c, 0, sizeof(c));
	c.seq = 2;
	c.status = VPCAM_OK;
	c.presentCount = frame;
	c.frameStamp = frame;
	c.numViews = 1;
	const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, -z, 1};
	const float w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, cx, cy, z, 1};
	memcpy(c.view[0], v, sizeof(v));
	memcpy(c.camWorld, w, sizeof(w));
	const float n = 1.0f, f = 1000.0f;
	const float ys = 1.0f / tanf(15.0f * 3.14159265f / 180.0f), xs = ys * 120.0f / 160.0f;
	const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
	memcpy(c.proj, p, sizeof(p));
	c.bbW = kW;
	c.bbH = kH;
	c.gridSeq = 1;
	c.gridValid = 1;
	c.wrapX = 1;
	c.wrapPeriod = (float)samplesX - 1.0f;
	c.sx = 1.0f;
	c.sy = 0.866f;
	c.heightScale = 0.2f;
	c.ox = samplesX / 2.0f;
	c.oy = samplesY / 2.0f * 0.866f;
	c.samplesX = samplesX;
	c.samplesY = samplesY;
	return c;
}

struct Block
{
	std::vector<uint8_t> data;
	VPShellWaterRecord r = {};
};
void PutF(std::vector<uint8_t>& b, size_t off, float v) { memcpy(b.data() + off, &v, 4); }
const float kAges[4] = {1.0f, 8.95f, -1.0f, 3.0f};

std::vector<uint8_t> WaveObj(uint32_t chunks, uint32_t waves)
{
	std::vector<uint8_t> o(0xB0, 0);
	auto u = [&](size_t off, uint32_t v) { memcpy(o.data() + off, &v, 4); };
	auto f = [&](size_t off, float v) { memcpy(o.data() + off, &v, 4); };
	u(0x08, 8);
	u(0x0C, 2);
	u(0x10, 16);
	f(0x1C, 0.08f);
	f(0x20, 0.02f);
	f(0x24, 9.0f);
	f(0x28, 0.05f);
	f(0x2C, 4.0f);
	f(0x30, 6.5f);
	f(0x34, 8.7f);
	f(0x38, 2.1f);
	f(0x3C, 5.0f);
	f(0x40, 0.0478f);
	f(0x44, 0.0478f);
	f(0x48, 0.0478f);
	f(0x50, 128.0f);
	u(0x54, chunks ? 0x12345678u : 0);
	u(0x58, chunks);
	u(0x5C, waves ? 0x23456780u : 0);
	u(0x60, waves);
	return o;
}

std::vector<uint8_t> WaveVertices(int k)
{
	std::vector<uint8_t> v(8 * 12, 0);
	const uint16_t half[3] = {0x0000, 0x4800, 0x4C00};   // 0, 8, 16
	for(int w = 0; w < 2; ++w)
		for(int c = 0; c < 4; ++c)
		{
			uint8_t* p = v.data() + 12 * (4 * w + c);
			const uint16_t h = half[(k + w) % 3];
			memcpy(p, &h, 2);
			memcpy(p + 2, &h, 2);
			p[4] = p[6] = (uint8_t)((c & 1) ? 255 : 0);
			p[5] = p[7] = (uint8_t)((c & 2) ? 255 : 0);
			p[8] = (uint8_t)w;
		}
	return v;
}

// sources_test.cpp Image(load, pid, waveChunks, mapWH, visibleWaves = true)
std::vector<uint8_t> Image(uint32_t load, uint32_t pid, int waveChunks, uint32_t mapWH)
{
	const bool visibleWaves = true;
	std::vector<Block> bl;
	auto add = [&](uint32_t sys, uint32_t role, std::vector<uint8_t> d) -> uint32_t {
		Block b;
		b.r.system = sys;
		b.r.role = role;
		uint32_t idx = 0;
		for(const auto& x : bl)
			if(x.r.system == sys && x.r.role == role)
				idx++;
		b.r.index = idx;
		b.r.vbRecord = b.r.ibRecord = 0xFFFFFFFFu;
		b.data = std::move(d);
		bl.push_back(b);
		return (uint32_t)bl.size() - 1;
	};
	auto vb = [](uint32_t n, uint32_t stride, uint8_t seed) {
		std::vector<uint8_t> v(n * stride);
		for(size_t i = 0; i < v.size(); ++i)
			v[i] = (uint8_t)(i * 7 + seed);
		return v;
	};
	auto ib = [](uint32_t n, uint32_t verts, uint32_t e) {
		std::vector<uint8_t> v(n * e);
		for(uint32_t i = 0; i < n; ++i)
		{
			uint32_t x = i % verts;
			memcpy(v.data() + i * e, &x, e);
		}
		return v;
	};
	auto draw = [&](uint32_t sys, uint32_t v, uint32_t i, uint32_t pass, uint32_t material) {
		uint32_t d = add(sys, VPWROLE_DRAW, {});
		Block& b = bl[d];
		b.r.topology = 4;
		b.r.vbRecord = v;
		b.r.ibRecord = i;
		b.r.vertexCount = bl[v].r.count;
		b.r.indexCount = bl[i].r.count;
		b.r.material = material;
		b.r.pass = pass;
	};
	add(VPWSYS_RIVER, VPWROLE_OBJECT, std::vector<uint8_t>(0x30, 1));
	add(VPWSYS_RIVER, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 2));
	{
		uint32_t v = add(VPWSYS_RIVER, VPWROLE_VB, vb(4, 32, 3));
		bl[v].r.stride = 32;
		bl[v].r.count = 4;
		uint32_t i = add(VPWSYS_RIVER, VPWROLE_IB, ib(6, 4, 4));
		bl[i].r.dxgiFormat = 42;
		bl[i].r.count = 6;
		draw(VPWSYS_RIVER, v, i, 4, 91);
	}
	std::vector<uint8_t> water(0xEC, 0);
	PutF(water, 0xD8, 0.18f);
	PutF(water, 0xDC, 0.145891f);
	PutF(water, 0xE0, 0.03f);
	PutF(water, 0xE4, 0.00607878f);
	uint32_t three = 3;
	memcpy(water.data() + 0xE8, &three, 4);
	add(VPWSYS_OCEAN, VPWROLE_OBJECT, water);
	const uint32_t nw = 2u * (uint32_t)waveChunks;
	add(VPWSYS_WAVE, VPWROLE_OBJECT, WaveObj((uint32_t)waveChunks, nw));
	if(waveChunks)
	{
		std::vector<uint8_t> arr(36u * waveChunks, 0);
		for(int k = 0; k < waveChunks; ++k)
		{
			const uint32_t first = 2u * k, n = 2;
			const float bnd[4] = {visibleWaves ? -100.0f : 10.0f * k, visibleWaves ? -100.0f : 0.0f, visibleWaves ? 100.0f : 10.0f * k + 9.0f,
			                      visibleWaves ? 100.0f : 9.0f};
			memcpy(arr.data() + 36 * k + 0x0C, &first, 4);
			memcpy(arr.data() + 36 * k + 0x10, &n, 4);
			memcpy(arr.data() + 36 * k + 0x14, bnd, 16);
		}
		bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, arr)].r.exeOffset = 0x54;
		std::vector<uint8_t> wv(16u * nw, 0);
		for(uint32_t i = 0; i < nw; ++i)
		{
			const float rec[4] = {0.0f, 0.0f, kAges[i % 4], 0.0f};
			memcpy(wv.data() + 16 * i, rec, 16);
		}
		uint32_t wa = add(VPWSYS_WAVE, VPWROLE_OBJECT, wv);
		bl[wa].r.exeOffset = 0x5C;
		bl[wa].r.flags = 2;
	}
	for(int k = 0; k < waveChunks; ++k)
	{
		bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 6))].r.exeOffset = 0x54;
		uint32_t v = add(VPWSYS_WAVE, VPWROLE_VB, visibleWaves ? WaveVertices(k) : vb(8, 12, (uint8_t)(9 + k)));
		bl[v].r.stride = 12;
		bl[v].r.count = 8;
		uint32_t i = add(VPWSYS_WAVE, VPWROLE_IB, ib(12, 8, 2));
		bl[i].r.dxgiFormat = 57;
		bl[i].r.count = 12;
		draw(VPWSYS_WAVE, v, i, 0, 92);
	}
	uint64_t off = sizeof(VPShellWaterImage) + bl.size() * sizeof(VPShellWaterRecord);
	for(auto& b : bl)
	{
		if(b.data.empty())
			continue;
		off = (off + 15) & ~15ull;
		b.r.dataOffset = off;
		b.r.dataBytes = b.data.size();
		b.r.fnv1a64 = Fnv(b.data.data(), b.data.size());
		off += b.data.size();
	}
	std::vector<uint8_t> img((size_t)off, 0);
	VPShellWaterImage h = {};
	h.magic = VPSHELL_WATER_MAGIC;
	h.version = VPSHELL_WATER_IMAGE_VERSION;
	h.headerBytes = sizeof(h);
	h.recordBytes = sizeof(VPShellWaterRecord);
	h.recordCount = (uint32_t)bl.size();
	h.loadSeq = load;
	h.exePid = pid;
	h.mapW = h.mapH = mapWH;
	h.waterZ = 10.117f;
	h.imageBytes = off;
	memcpy(img.data(), &h, sizeof(h));
	for(size_t i = 0; i < bl.size(); ++i)
	{
		memcpy(img.data() + sizeof(h) + i * sizeof(VPShellWaterRecord), &bl[i].r, sizeof(VPShellWaterRecord));
		if(!bl[i].data.empty())
			memcpy(img.data() + bl[i].r.dataOffset, bl[i].data.data(), bl[i].data.size());
	}
	return img;
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

void ExeProj(float m[16])
{
	const float wx = 256.0f, wy = 256.0f * 0.866f, sx = 2.0f / wx, sy = 2.0f / wy;
	const float v[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -0.5f, 0, -1.0f, -1.0f, 0.5f, 1};
	memcpy(m, v, sizeof(v));
}

SrvFeed::PlotRecord Rec(int8_t terrain, int16_t improvement, uint8_t known)
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
	r.known = known;
	return r;
}

// feed.bin as the recorder writes it: each recorded call encoded at once with the loop iteration of the host's loop
struct FeedBytes : FeedRecordSink
{
	sb::FeedEncoder enc;
	std::vector<uint8_t> bytes;
	uint64_t iter = 0;
	int drains = 0, tables = 0;
	void onDrain(const SrvFeed::Batch& b) override
	{
		std::vector<uint8_t> r;
		enc.addDrain(iter, -1, b, &r);
		bytes.insert(bytes.end(), r.begin(), r.end());
		drains++;
	}
	void onPlots(uint32_t caller, const std::vector<SrvFeed::PlotRecord>& t, uint32_t gen, uint32_t w, uint32_t h) override
	{
		std::vector<uint8_t> r;
		enc.addPlots(iter, caller, gen, w, h, t, &r);
		bytes.insert(bytes.end(), r.begin(), r.end());
		tables++;
	}
};

// ---- the devices ----------------------------------------------------------------------------------------------------------
struct Dev
{
	const char* name;
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	std::string description, luid;
	std::string adapterArg;   // the player's --adapter for "the same adapter": warp, or bundle (by the manifest)
};
Dev g_devs[2] = {{"warp"}, {"hw"}};

// ---- the live side and its bundle -------------------------------------------------------------------------------------------
struct Spec
{
	bool bake = true;            // render_tiles=bake with the stand-in baker (else none, constant colour)
	bool water = true;           // the module's water with the synthetic image and coast waves
	bool overlay = true;         // frames 2..5 bound to one overlay texture
	std::vector<int> dumped = {0, 3, 5};
	bool corruptDump = false;    // frame 103's dumps changed after the readback (the comparison must see it)
	bool corruptDepth = true;    // with corruptDump: one depth value too (false: two colour pixels only)
	std::string exePin;          // pins.exe_sha256 ("" none)
	std::string tiles;           // render_config render_tiles override ("" = from bake)
	bool namedFar = false;       // cameras/far.vpcam with a frame stamp outside the span
	// issue 3911: the fog of war (render_fow=1, FOWLevel 1) with the real c5r::fow modules (the EXE's FOW programs, the
	// install's cloud textures): an 8 x 4 table (2 visible, 2 seen, 28 unexplored plots) on an 8 x 8 grid, the overlay bound
	// in every frame, three fog frames before the span (T 0.2 s at its start), one fog edit inside it
	bool fog = false;
	double fogClockShift = 0;    // added to fog_clock.bin's T (the clock must matter: the comparison must see it)
	bool fogNoClock = false;     // fog_clock.bin left out
	// issue 0364 (format version 3): render_decals=1 with decals/load0001.dtap (a synthetic set, generation 5, load 1) applied
	// to the live scene before its field and handed by the player to its scene; the stand-in baker draws no decals, so the
	// picture is the one without them and the case asserts the hand-over (the render: lines) and the refusals
	bool decals = false;
	std::vector<std::string> decalNames;   // the set's texture names (empty = the fixture's; others: the live side skips it)
	std::string decalMods;       // render_config render_decal_mods
	bool decalNoMember = false;  // render_decals=1 with no member and the recorder's no-set note
	// issue 0440 (format version 5): render_units=1 (and render_units_anim=1) with the recorder's two gap notes instead of
	// units_db and units_history.bin (the checker's rule): the player must refuse to draw units it cannot replay
	bool unitsGap = false;
	// ticket 0403 (format version 6): the fog at FOWLevel 3 with the FOWLevel-3 particles (the real modules, seed 1): their
	// state before the span's first frame (spawned in the three pre-span frames, FowScene::particleState) as particles.bin
	bool fogParticles = false;
	bool partNoMember = false;   // render_fow_particles=1 without particles.bin (with partNote: the recorder's gap note)
	bool partNote = false;
	bool partBadHash = false;    // the state's flow-field hash changed (another install's field: the player must refuse it)
	// ticket 0199 part 2: render_arc=1 with one SpawnArrowEvent record (kUiMirror kind 1, plots (1, 1) -> (2, 2)) in the drain
	// of the span's first frame: the arc grows over the span (recorded clock); arcNoKey: the live side draws it but the
	// manifest has no render_arc (the player must then differ: the case's teeth)
	bool arc = false;
	bool arcNoKey = false;
	// ticket 0622 (format version 11): with water, the live side takes a later copy of load 1 (copy 2, one coast-wave chunk
	// instead of two: another wave count, as after a Waves_Reload) before frame index waterCopy2At's poll (-1: none);
	// waterFrames 1 writes water_frames.bin as the recorder does, 0 writes none and the bundle as format 10 (before 0622),
	// 2 writes records that name copy 1 for every frame (a wrong record: the comparison must see it)
	int waterCopy2At = -1;
	int waterFrames = 1;
};
const int kFrames = 6;
const int64_t kFirst = 100;
const double kNow[kFrames] = {1000.0, 1016.7, 1033.4, 1333.4, 1350.1, 1366.8};   // one dt above the 250 ms clamp

std::wstring GameExe() { return g_game + L"\\CivilizationV_DX11.exe"; }

bool MakeBundle(Dev& dv, const std::wstring& dir, const Spec& sp, std::string& why)
{
	LogOpen(nullptr, &LiveLog);
	g_log.clear();
	RemoveTree(dir);
	why.clear();
	// the feed's table before the field (the DLL's LeaveUpdate), recorded through the wrapper the host would install
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	const uint32_t tableW = sp.fog ? 8u : 4u;   // issue 3911
	SrvFeed::BeginSnapshot(tableW, 4);
	for(uint32_t i = 0; i < tableW * 4; ++i)
	{
		SrvFeed::PlotRecord r = Rec(1, -1, 3);
		if(sp.fog)
			r.fow = i < 2 ? 3 : i < 4 ? 1 : 0;   // issue 3911: visible, seen, unexplored (most: the clouds in view)
		SrvFeed::SetPlot(i, r);
	}
	SrvFeed::EndSnapshot();
	FeedBytes fb;
	std::unique_ptr<FeedSource> recording = MakeRecordingFeed(LiveFeed(), fb);
	SceneOptions o;
	o.exe = GameExe();
	o.install = g_game;
	o.tiles = sp.bake ? SceneOptions::kTilesBake : SceneOptions::kTilesNone;
	o.color = sp.bake ? c5r::terrain::ColorSource::Caches : c5r::terrain::ColorSource::Constant;
	o.fallback = c5r::terrain::ColorSource::Constant;
	o.water = sp.water;
	o.waterMode = WaterSceneOptions::kFull;
	o.overlay = sp.overlay ? 1 : 0;
	o.bake.pipeline = false;
	o.bake.install = g_game;
	o.bake.exe = GameExe();
	o.bake.res.radius = -1;
	o.bake.feed = recording.get();
	o.fow = sp.fog ? 1 : 0;   // issue 3911
	o.fowLevel = sp.fogParticles ? 3 : 1;   // ticket 0403: the particles at FOWLevel 3
	o.decals = sp.decals ? 1 : 0;   // issue 0364
	o.arc = sp.arc ? 1 : 0;         // ticket 0199 part 2
	Scene live;
	if(sp.fog)   // the host's StartRecorder: the fog's table reads through the recording feed, before Scene::init
		live.fow().setTable(MakeFeedSourcePlotTable(*recording));
	c5r::Status st = live.init(dv.dev, dv.ctx, o);
	if(!st.ok())
	{
		why = "live init: " + st.str();
		return false;
	}
	struct Rel
	{
		Scene& s;
		~Rel()
		{
			s.releaseDevice();
			s.detach();
		}
	} rel{live};
	// the overlay: one texture, frames 2..5 bound to it
	const OverlayTexels tex = Texels(7);
	std::vector<uint8_t> ovl;
	std::string sha;
	FileOverlayControl* ovlCtl = nullptr;
	std::vector<FileOverlayFrame> ofr(kFrames);
	if(sp.overlay)
	{
		if(!sb::EncodeOverlay(tex.bytes.data(), tex.bytes.size(), &ovl).ok())
		{
			why = "EncodeOverlay";
			return false;
		}
		sha = sb::Sha256Hex(ovl.data(), ovl.size());
		for(int k = sp.fog ? 0 : 2; k < kFrames; ++k)   // issue 3911: the fog case binds every frame
		{
			ofr[k].bound = true;
			ofr[k].generation = 40 + (uint32_t)k / 4;
			ofr[k].presentCount = 200 + (uint64_t)k;
			ofr[k].numViews = 1;
			ExeProj(ofr[k].overlayProj[0]);
			ofr[k].sha256 = sha;
		}
		std::map<std::string, OverlayTexels> texMap;
		texMap[sha] = tex;
		std::unique_ptr<OverlaySource> os = MakeFileOverlaySource(dv.dev, ofr, texMap, &ovlCtl, &why);
		if(!os)
			return false;
		live.setOverlaySource(std::move(os));
	}
	if(sp.fog)   // the fog with its overlay, as server.exe starts it (this Scene has no section)
		RestartFowWithOverlay(live, dv.dev, o, *recording);
	const uint32_t mapH = sp.fog ? 8u : 4u;
	const std::vector<uint8_t> c5 = FieldBytes(mapH, tableW, sp.arc);   // ticket 0199: the arc case places its plots
	const std::vector<uint8_t> img = Image(1, 4321, 2, 4);
	VPShellWaterTap wb = {};
	wb.state = VPWTAP_READY;
	wb.loadSeq = 1;
	wb.copySeq = 1;
	wb.exePid = 4321;
	wb.recordCount = ((const VPShellWaterImage*)img.data())->recordCount;
	wb.imageBytes = img.size();
	wb.fnv1a64 = Fnv(img.data(), img.size());
	// ticket 0622: copy 2 of load 1 (one wave chunk: 2 waves where copy 1 has 4), taken live inside the span
	const std::vector<uint8_t> img2 = Image(1, 4321, 1, 4);
	VPShellWaterTap wb2 = wb;
	wb2.copySeq = 2;
	wb2.recordCount = ((const VPShellWaterImage*)img2.data())->recordCount;
	wb2.imageBytes = img2.size();
	wb2.fnv1a64 = Fnv(img2.data(), img2.size());
	std::vector<int> frameCopy(kFrames, 1);   // the copy each frame was drawn with
	std::vector<VPShellCamera> cams;
	for(int k = 0; k < kFrames; ++k)
		cams.push_back(sp.fog ? Camera(40.0f + 2.0f * k, kFirst + k, 160.0f, mapH * 64, 28.0f, tableW * 64)   // issue 3911: in the
		                                                                                                     // overlay's window, over unexplored plots
		                      : Camera(4.0f * k - 10.0f, kFirst + k, 160.0f, mapH * 64));
	// the host's loop: iteration 1 drains the snapshot and takes the field (tapPoll) and the water image (water poll);
	// iteration 2 drains; frames 0..5 in iterations 3..8, plot edits before iterations 5 and 7
	auto drain = [&](uint64_t it) {
		fb.iter = it;
		SrvFeed::Batch b;
		if(recording->drain(b))
		{
			if(!b.changedPlots.empty())
				live.plotsChanged(b.changedPlots.data(), b.changedPlots.size());
			live.fowFeed(b);   // issue 3911: as the host's Loop
			live.arcFeed(b);   // ticket 0199 part 2: as the host's Loop (render_arc=0: nothing)
		}
	};
	drain(1);
	// issue 0364: the decal set in force before the field (the player hands it over at the same point)
	const std::vector<uint8_t> decalImg = sp.decalNames.empty() ? decalfixture::MakeImage(5, 1) : decalfixture::MakeImage(5, 1, sp.decalNames);
	if(sp.decals && !sp.decalNoMember && sp.decalNames.empty())
	{
		std::string dw;
		if(!live.loadDecalImage(decalImg.data(), decalImg.size(), &dw).ok())
		{
			why = "live decals: " + dw;
			return false;
		}
	}
	st = live.loadField(c5.data(), c5.size(), 1, &cams[0]);
	if(!st.ok())
	{
		why = "live field: " + st.str();
		return false;
	}
	if(sp.water && !live.water().loadImage(wb, img.data(), img.size(), true, 1, kSamples, kSamples))
	{
		why = "live water: " + live.water().reader()->lastWhy();
		return false;
	}
	drain(2);
	sceneshot::Targets tg;
	if(!tg.ensure(dv.dev, kW, kH, why))
		return false;
	std::vector<sb::FrameRecord> frs;
	std::map<int, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> dumps;
	WaterClockState clock0;
	FowClockState fogClock0;
	FowParticleState part0;   // ticket 0403
	std::string partWhy;
	bool havePart = false;
	if(sp.fog)   // issue 3911: three fog frames before the span (not recorded): FOWAnim's T is 0.2 s at its start
		for(int k = 0; k < 3; ++k)
		{
			ovlCtl->seekFrame(0);
			live.poll();
			SceneFrame f;
			f.cam = cams[0];
			f.cam.frameStamp = f.cam.presentCount = kFirst - 3 + k;
			f.frameId = kFirst - 3 + k;
			f.nowMs = 600.0 + 100.0 * k;
			if(!live.fowAdmit(f.cam, f.nowMs))
			{
				why = "live: the fog held pre-span frame " + std::to_string(k) + " (" + FowScene::StateName(live.fow().state()) + ")";
				return false;
			}
			c5r::terrain::FrameStats fs;
			st = live.draw(dv.ctx, tg.rtv, tg.dsv, kW, kH, f, fs);
			dv.ctx->OMSetRenderTargets(0, nullptr, nullptr);
			if(!st.ok())
			{
				why = "live pre-span draw: " + st.str();
				return false;
			}
		}
	for(int k = 0; k < kFrames; ++k)
	{
		const uint64_t it = 3 + (uint64_t)k;
		if(it == 5 && !sp.fog)
			SrvFeed::SetPlot(5, Rec(1, 7, 3));   // an improvement: re-baked
		if(it == 7 && !sp.fog)
			SrvFeed::SetPlot(9, Rec(2, -1, 3));  // a terrain change: re-baked
		if(it == 6 && sp.fog)
		{
			SrvFeed::PlotRecord r = Rec(1, -1, 3);
			r.fow = 3;
			SrvFeed::SetPlot(12, r);             // issue 3911: an unexplored plot becomes visible: the fog reads its table
		}
		if(it == 3 && sp.arc)   // ticket 0199 part 2: the proxy's record, as the ui channel's post handler pushes it
		{
			SrvFeed::Event e = {};
			e.kind = SrvFeed::kUiMirror;
			e.flags = 1;   // UiBridge::kKindSpawnArrow
			e.plot = -1;
			e.turn = -1;
			e.a[0] = 1;
			e.a[1] = 1;
			e.a[2] = 2;
			e.a[3] = 2;
			SrvFeed::Push(e);
		}
		drain(it);
		if(ovlCtl)
			ovlCtl->seekFrame((uint32_t)k);
		if(sp.water && sp.waterCopy2At >= 0 && k == sp.waterCopy2At)   // ticket 0622: as WaterScene::poll takes a re-copy
			if(!live.water().loadImage(wb2, img2.data(), img2.size(), true, 1, kSamples, kSamples))
			{
				why = "live water copy 2: " + live.water().reader()->lastWhy();
				return false;
			}
		if(sp.water && sp.waterCopy2At >= 0 && k >= sp.waterCopy2At)
			frameCopy[k] = 2;
		live.poll();
		SceneFrame f;
		f.cam = cams[k];
		f.frameId = kFirst + k;
		f.nowMs = kNow[k];
		if(k == 0)
		{
			clock0 = live.water().clockState();
			fogClock0 = live.fow().clockState();
			if(sp.fogParticles)   // ticket 0403: the recorder's call at the span's first frame
				havePart = live.fow().particleState(part0, partWhy);
		}
		if(sp.fog && !live.fowAdmit(f.cam, f.nowMs))
		{
			why = "live: the fog held frame " + std::to_string(kFirst + k) + " (" + FowScene::StateName(live.fow().state()) + ")";
			return false;
		}
		// the live server does not settle; this synthetic live side does, so that its dumps hold every tile baked
		SettleResult sr;
		st = live.settle(dv.ctx, f, kW, kH, 20000.0, &sr);
		if(!st.ok() || !sr.settled)
		{
			why = "live settle: " + st.str() + " " + sr.what;
			return false;
		}
		c5r::terrain::FrameStats fs;
		const int64_t wd0 = live.water().drawsTotal, wv0 = live.water().waveDrawsTotal;
		st = live.draw(dv.ctx, tg.rtv, tg.dsv, kW, kH, f, fs);
		dv.ctx->OMSetRenderTargets(0, nullptr, nullptr);
		if(!st.ok())
		{
			why = "live draw: " + st.str();
			return false;
		}
		// the flags as 3904's recorder sets them: bit 1 when the water scene's draws grew, bit 2 when its wave draws grew
		const bool module = live.water().drawsTotal > wd0, waves = live.water().waveDrawsTotal > wv0;
		sb::FrameRecord r;
		memset(&r, 0, sizeof(r));
		r.frameId = kFirst + k;
		r.loopIter = it;
		r.nowMs = kNow[k];
		r.width = kW;
		r.height = kH;
		if(live.lastOverlay().srv)
		{
			r.overlayBound = 1;
			r.overlayGeneration = ofr[k].generation;
			r.overlayPresentCount = ofr[k].presentCount;
			r.overlayNumViews = ofr[k].numViews;
			memcpy(r.overlayProj, ofr[k].overlayProj, sizeof(r.overlayProj));
			for(int i = 0; i < 32; ++i)
				r.overlaySha256[i] = (uint8_t)strtoul(sha.substr(2 * i, 2).c_str(), nullptr, 16);
		}
		r.flags = (module ? sb::kFrameWaterModule : 0) | (waves ? sb::kFrameWaves : 0) | (live.fow().terrainInputs().on ? sb::kFrameFog : 0);
		if(std::find(sp.dumped.begin(), sp.dumped.end(), k) != sp.dumped.end())
		{
			r.flags |= sb::kFrameDumped;
			std::vector<uint8_t> col, dep;
			if(!sceneshot::ReadBack(dv.dev, dv.ctx, tg.colour, col, why) || !sceneshot::ReadBack(dv.dev, dv.ctx, tg.depth, dep, why))
				return false;
			dumps[k] = std::make_pair(col, dep);
		}
		frs.push_back(r);
	}
	if(sp.water)
		for(const sb::FrameRecord& r : frs)
			if(!(r.flags & sb::kFrameWaterModule) || !(r.flags & sb::kFrameWaves))
			{
				why = "live: frame " + std::to_string(r.frameId) + " drew no module water or no coast waves (flags " + std::to_string(r.flags) +
				      ", " + WaterWhyName(live.water().why()) + "): the case would have no teeth";
				return false;
			}
	if(sp.fog)
		for(const sb::FrameRecord& r : frs)
			if(!(r.flags & sb::kFrameFog))
			{
				why = "live: frame " + std::to_string(r.frameId) + " drew no fog (state " + FowScene::StateName(live.fow().state()) +
				      "): the case would have no teeth";
				return false;
			}
	if(sp.fog && (live.fow().fogGeneration() < 2 || fogClock0.clockS != 0.2 || fogClock0.clockLastMs != 800.0))
	{
		why = "live: the fog's edit or clock did not happen (fog generation " + std::to_string(live.fow().fogGeneration()) + ", T " +
		      std::to_string(fogClock0.clockS) + ")";
		return false;
	}
	if(sp.fogParticles && (!havePart || !part0.spawned || live.fow().particleFrames != 3 + kFrames))
	{
		why = "live: the particles were not spawned before the span or not drawn in every frame (" + partWhy + ", particle frames " +
		      std::to_string(live.fow().particleFrames) + ")";
		return false;
	}
	if(sp.arc && (live.arcCounters().applied != 1 || live.arcCounters().drawnFrames != kFrames || !live.arcCounters().ready))
	{
		why = "live: the arc was not spawned once and drawn in every frame (ready " + std::to_string(live.arcCounters().ready) + ", applied " +
		      std::to_string(live.arcCounters().applied) + ", drawn " + std::to_string(live.arcCounters().drawnFrames) + ", spawn errors " +
		      std::to_string(live.arcCounters().spawnErrors) + "): the case would have no teeth";
		return false;
	}
	ResidencySource* res = sp.bake ? static_cast<ResidencySource*>(live.source()) : nullptr;
	if(sp.bake && (!res || res->plotsPassed != 2))
	{
		why = "live: the two plot edits did not pass the filter (" + std::to_string(res ? res->plotsPassed : -1) + ")";
		return false;
	}
	SrvFeed::SetEnabled(false);
	// the bundle (3902's Writer, as the recorder writes it)
	sb::Writer w;
	st = w.open(dir);
	bool ok = st.ok();
	ok = ok && w.addMember("heightfield/load0001.c5hf", sb::MemberKind::Heightfield, c5.data(), c5.size()).ok();
	if(sp.water)
	{
		std::vector<uint8_t> wt;
		sb::EncodeWaterTap(wb, 1, 1, kSamples, kSamples, img.data(), img.size(), &wt);
		ok = ok && w.addMember("water/load0001.wtap", sb::MemberKind::WaterTap, wt.data(), wt.size()).ok();
		std::vector<uint8_t> wt2;   // ticket 0622
		if(sp.waterCopy2At >= 0)
		{
			sb::EncodeWaterTap(wb2, 1, 1, kSamples, kSamples, img2.data(), img2.size(), &wt2);
			ok = ok && w.addMember("water/load0001-c2.wtap", sb::MemberKind::WaterTap, wt2.data(), wt2.size()).ok();
		}
		if(sp.waterFrames)   // ticket 0622 (format version 11): the recorder's water_frames.bin
		{
			ok = ok && w.appendStart("water_frames.bin", sb::MemberKind::WaterFrames).ok();
			for(int k = 0; k < kFrames; ++k)
			{
				const bool two = frameCopy[k] == 2 && sp.waterFrames == 1;
				const std::vector<uint8_t>& bytes = two ? wt2 : wt;
				sb::WaterFrameRecord r;
				memset(&r, 0, sizeof(r));
				r.frameId = kFirst + k;
				r.flags = sb::kWaterCopyInForce;
				r.loadSeq = 1;
				r.copySeq = two ? 2 : 1;
				const std::string h = sb::Sha256Hex(bytes.data(), bytes.size());
				for(int i = 0; i < 32; ++i)
					r.wtapSha256[i] = (uint8_t)strtoul(h.substr(2 * i, 2).c_str(), nullptr, 16);
				ok = ok && w.append("water_frames.bin", &r, sizeof(r)).ok();
			}
		}
		sb::WaterClockFile wc;
		wc.clockStartMs = clock0.clockStartMs;
		wc.waveLastMs = clock0.waveLastMs;
		wc.lastNowMs = clock0.lastNowMs;
		wc.waves = clock0.waves;
		std::vector<uint8_t> wcb;
		sb::EncodeWaterClock(wc, &wcb);
		ok = ok && w.addMember("water_clock.bin", sb::MemberKind::WaterClock, wcb.data(), wcb.size()).ok();
	}
	if(sp.fog && !sp.fogNoClock)   // issue 3911
	{
		sb::FogClockFile fc;
		fc.clockS = fogClock0.clockS + sp.fogClockShift;
		fc.clockLastMs = fogClock0.clockLastMs;
		std::vector<uint8_t> fcb;
		sb::EncodeFogClock(fc, &fcb);
		ok = ok && w.addMember("fog_clock.bin", sb::MemberKind::FogClock, fcb.data(), fcb.size()).ok();
	}
	if(sp.fogParticles && !sp.partNoMember)   // ticket 0403
	{
		sb::FogParticlesFile pf;
		pf.flags = sb::kFogParticlesSpawned;
		pf.seed = part0.seed;
		pf.worldW = part0.worldW;
		pf.worldH = part0.worldH;
		pf.state = part0.sim;
		if(sp.partBadHash)   // the flow field's hash (after magic, version, the settings, the field's width and height)
			pf.state[8 + sizeof(c5r::fow::ParticleSettings) + 8] ^= 0x5A;
		std::vector<uint8_t> pb;
		sb::EncodeFogParticles(pf, &pb);
		ok = ok && w.addMember("particles.bin", sb::MemberKind::FogParticles, pb.data(), pb.size()).ok();
	}
	if(sp.bake || sp.fog)
	{
		ok = ok && w.appendStart("feed.bin", sb::MemberKind::Feed).ok();
		ok = ok && w.append("feed.bin", fb.bytes.data(), fb.bytes.size()).ok();
	}
	if(sp.decals && !sp.decalNoMember)   // issue 0364
		ok = ok && w.addMember("decals/load0001.dtap", sb::MemberKind::DecalTap, decalImg.data(), decalImg.size()).ok();
	if(sp.overlay)
		ok = ok && w.addMember("overlay/" + sha.substr(0, 16) + ".ovl", sb::MemberKind::OverlayTexture, ovl.data(), ovl.size()).ok();
	ok = ok && w.appendStart("frames.bin", sb::MemberKind::Frames).ok() && w.appendStart("cameras.bin", sb::MemberKind::CameraBlocks).ok();
	for(int k = 0; k < kFrames; ++k)
		ok = ok && w.append("frames.bin", &frs[k], sizeof(frs[k])).ok() && w.append("cameras.bin", &cams[k], sizeof(cams[k])).ok();
	std::vector<int64_t> dumpedIds;
	for(auto& kv : dumps)
	{
		const int64_t id = kFirst + kv.first;
		dumpedIds.push_back(id);
		std::vector<uint8_t>& col = kv.second.first;
		std::vector<uint8_t>& dep = kv.second.second;
		if(sp.corruptDump && kv.first == 3)
		{
			col[4 * (kW * 60 + 40) + 2] ^= 0x10;   // two pixels' red, one depth value
			col[4 * (kW * 61 + 41) + 2] ^= 0x01;
			float z;
			memcpy(&z, &dep[4 * (kW * 60 + 40)], 4);
			z = z * 0.5f;
			if(sp.corruptDepth)
				memcpy(&dep[4 * (kW * 60 + 40)], &z, 4);
		}
		std::vector<uint8_t> bmp;
		sb::EncodeDumpColour(kW, kH, col.data(), &bmp);
		ok = ok && w.addMember("dumps/" + std::to_string(id) + ".bmp", sb::MemberKind::DumpColour, bmp.data(), bmp.size()).ok();
		ok = ok && w.addMember("dumps/" + std::to_string(id) + ".depth.f32", sb::MemberKind::DumpDepth, dep.data(), dep.size()).ok();
	}
	{
		std::vector<uint8_t> b;
		sb::EncodeStreamHead(sb::MemberKind::CameraBlocks, &b);
		const uint8_t* p = (const uint8_t*)&cams[3];
		b.insert(b.end(), p, p + sizeof(VPShellCamera));
		ok = ok && w.addMember("cameras/default.vpcam", sb::MemberKind::NamedCameraBlock, b.data(), b.size()).ok();
		const char* js = "{\"name\": \"default\", \"synthetic\": true}";
		ok = ok && w.addMember("cameras/default.json", sb::MemberKind::NamedCamera, js, strlen(js)).ok();
		if(sp.namedFar)
		{
			VPShellCamera farCam = Camera(6.0f, 999);
			sb::EncodeStreamHead(sb::MemberKind::CameraBlocks, &b);
			p = (const uint8_t*)&farCam;
			b.insert(b.end(), p, p + sizeof(VPShellCamera));
			ok = ok && w.addMember("cameras/far.vpcam", sb::MemberKind::NamedCameraBlock, b.data(), b.size()).ok();
		}
	}
	if(!ok)
	{
		why = "writing the bundle";
		return false;
	}
	sb::Manifest m;
	m.bundleId = "SYNTH-3905-" + std::string(dv.name) + "_20261115-000000_test";
	m.created = "2026-11-15T00:00:00-05:00";
	m.recorderKeys["render_test_record"] = sceneshot::Fwd(NarrowW(dir));
	m.serverExeSha256 = std::string(64, 'a');
	m.repoCommit = "0000000000000000000000000000000000000000";
	m.buildConfig = "release";
	if(!sp.exePin.empty())
		m.pins["exe_sha256"] = sp.exePin;
	m.saveName = "SYNTH-3905";
	m.saveSha256 = std::string(64, 'd');
	m.mapW = tableW;
	m.mapH = mapH;
	m.turn = 1;
	m.loadSeq = 1;
	m.vt = "unknown";
	m.install = sceneshot::Fwd(NarrowW(g_game));
	m.machine = "test";
	m.adapterLuid = dv.luid;
	m.adapterDescription = dv.description;
	m.adapterDriver = "-";
	m.renderConfig["render_tiles"] = !sp.tiles.empty() ? sp.tiles : sp.bake ? "bake" : "none";
	m.renderConfig["render_baker"] = "standin";
	m.renderConfig["render_color"] = sp.bake ? "caches" : "constant";
	m.renderConfig["render_fallback"] = "constant";
	m.renderConfig["render_water"] = sp.water ? "1" : "0";
	m.renderConfig["render_overlay"] = sp.overlay ? "1" : "0";
	m.renderConfig["render_bake_radius"] = "-1";
	if(sp.fog)
	{
		m.renderConfig["render_fow"] = "1";
		m.renderConfig["render_fow_level"] = sp.fogParticles ? "3" : "1";
		m.renderConfig["render_fow_particles"] = sp.fogParticles ? "1" : "0";   // ticket 0403
		if(sp.fogParticles && sp.partNote)
			m.versionNotes.push_back(std::string(sb::kFogParticlesNote) + ": the server drew the FOWLevel-3 particles (test): the player draws none");
	}
	m.renderConfig["render_decals"] = sp.decals ? "1" : "0";   // issue 0364
	if(sp.arc && !sp.arcNoKey)
		m.renderConfig["render_arc"] = "1";   // ticket 0199 part 2
	m.renderConfig["render_decal_mods"] = sp.decalMods;
	if(sp.decalNoMember)
		m.versionNotes.push_back(std::string(sb::kDecalNoSetNote) + " (frame 100, the reader has no set yet): the span is drawn without decals");
	if(sp.unitsGap)   // issue 0440: the live side drew no units (no unit scene here); the bundle says it cannot carry them
	{
		m.renderConfig["render_units"] = "1";
		m.renderConfig["render_units_anim"] = "1";
		m.versionNotes.push_back(std::string(sb::kUnitsNoDbNote) + ": the unit scene had no database at the span's first frame (test)");
		m.versionNotes.push_back(std::string(sb::kUnitsNoHistoryNote) + ": the unit scene was not ready at the span's first frame (test)");
	}
	m.firstFrameId = kFirst;
	m.frameCount = kFrames;
	m.dumpedFrames = dumpedIds;
	m.versionNotes.push_back("synthetic bundle of sceneshot_test.cpp (task 3905)");
	for(const char* c : sb::kCounterNames)
		m.counters[c] = 0;
	st = w.finish(m);
	if(!st.ok())
	{
		why = "finish: " + st.str();
		return false;
	}
	if(sp.water && !sp.waterFrames)   // ticket 0622: a bundle written before water_frames.bin (the Writer writes kFormatVersion)
	{
		std::vector<uint8_t> js;
		std::string s;
		const std::string from = "\"format_version\": " + std::to_string(sb::kFormatVersion);
		if(ReadAll(dir + L"\\scene.json", js))
			s.assign(js.begin(), js.end());
		const size_t at = s.find(from);
		if(at == std::string::npos)
		{
			why = "scene.json: no " + from;
			return false;
		}
		s.replace(at, from.size(), "\"format_version\": 10");
		WriteAll(dir + L"\\scene.json", std::vector<uint8_t>(s.begin(), s.end()));
	}
	sb::CheckResult cr;
	const bool refusedOnPurpose = sp.fogParticles && sp.partNoMember && !sp.partNote;   // ticket 0403: the checker's negative
	if(!refusedOnPurpose && !sb::CheckBundle(dir, &cr).ok())
	{
		why = "the synthetic bundle fails the checker: " + cr.line();
		return false;
	}
	return true;
}

std::vector<std::string> Args(const std::wstring& bundle, const std::wstring& out, std::vector<std::string> more)
{
	std::vector<std::string> a = {"--bundle", NarrowW(bundle), "--out", NarrowW(out), "--install", NarrowW(g_game)};
	a.insert(a.end(), more.begin(), more.end());
	return a;
}

std::wstring Dir(Dev& dv, const char* name) { return g_work + L"\\" + sb::Widen(std::string(dv.name) + "_" + name); }

// ---- the cases -------------------------------------------------------------------------------------------------------------
// Ticket 0199 part 2: the server's arc offline. A bundle whose live side drew the arc (render_arc=1, one kind-1 record in
// feed.bin, the arc growing over the span on the recorded clock) is played byte-equal; the same live run with the key left
// out of render_config is played without the arc and must differ (the arc is in the dumps: the case's teeth).
void CaseArc(Dev& dv)
{
	std::string why;
	Spec sp;
	sp.arc = true;
	const std::wstring b = Dir(dv, "arc"), out = Dir(dv, "arc_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] arc bundle: %s", dv.name, why.c_str());
	if(made)
	{
		const int rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == 0 && Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3, "[%s] arc: exit %d (want 0, 3 equal): %s", dv.name, rc,
		      Last().c_str());
		CHECK(Has("sceneshot: render_config: arc 1"), "[%s] arc: the player read render_arc=1", dv.name);
	}
	Spec nk = sp;
	nk.arcNoKey = true;
	const std::wstring b2 = Dir(dv, "arc_nokey"), out2 = Dir(dv, "arc_nokey_out");
	RemoveTree(out2);
	std::string why2;
	const bool made2 = MakeBundle(dv, b2, nk, why2);
	CHECK(made2, "[%s] arc bundle without the key: %s", dv.name, why2.c_str());
	if(made2)
	{
		const int rc2 = Play(Args(b2, out2, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc2 == sceneshot::kDiffers && Has("sceneshot: render_config: arc 0") && Count("colour DIFFERS") >= 2,
		      "[%s] arc (negative): without render_arc the player draws no arc and differs (frames 103 and 105 at least): exit %d (want %d)",
		      dv.name, rc2, (int)sceneshot::kDiffers);
	}
	RemoveTree(b);
	RemoveTree(out);
	RemoveTree(b2);
	RemoveTree(out2);
}

// Ticket 0622 (format version 11): a span that holds a Waves_Reload. The live side draws frame 100 with copy 1 of load 1
// (4 coast waves) and takes copy 2 (2 waves) before frame 102's poll; its water clock at the span's start is copy 1's.
// With water_frames.bin the player switches at frame 102 and every dumped frame (100 before, 103 and 105 after) is equal.
// Teeth: the same live run written as format 10 (no member) is drawn the old way, copy 2 for every frame, and its copy-1
// clock is refused (live-terrain.md 9.4's gap, W6P03); records naming copy 1 for every frame differ at 103 and 105.
void CaseWaterCopies(Dev& dv)
{
	Spec sp;
	sp.waterCopy2At = 2;
	const std::wstring b = Dir(dv, "wcopies"), out = Dir(dv, "wcopies_out");
	RemoveTree(out);
	std::string why;
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] water copies bundle: %s", dv.name, why.c_str());
	if(made)
	{
		const int rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == 0 && Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3, "[%s] water copies: exit %d (want 0, 3 equal): %s", dv.name,
		      rc, Last().c_str());
		CHECK(Has("water image water/load0001.wtap (copy 1) in use from frame 100 (water_frames.bin)") &&
		          Has("water image water/load0001-c2.wtap (load 1 copy 2) in use from frame 102, as live (water_frames.bin)") &&
		          Count("in use from frame") == 2,
		      "[%s] water copies: the switch lines", dv.name);
	}
	Spec old = sp;
	old.waterFrames = 0;
	const std::wstring b2 = Dir(dv, "wcopies_v10"), out2 = Dir(dv, "wcopies_v10_out");
	RemoveTree(out2);
	std::string why2;
	const bool made2 = MakeBundle(dv, b2, old, why2);
	CHECK(made2, "[%s] format-10 water copies bundle: %s", dv.name, why2.c_str());
	if(made2)
	{
		const int rc2 = Play(Args(b2, out2, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc2 == sceneshot::kReplayFailed && Has("(format 10, save") && Has("recorded image of load seq 1 copy 2 loaded") &&
		          Has("refused by the water scene (4 waves") && Count("in use from frame") == 0,
		      "[%s] format 10 (negative): the last copy and copy 1's clock, refused: exit %d (want %d): %s", dv.name, rc2,
		      (int)sceneshot::kReplayFailed, Last().c_str());
	}
	Spec lie = sp;
	lie.waterFrames = 2;
	const std::wstring b3 = Dir(dv, "wcopies_lie"), out3 = Dir(dv, "wcopies_lie_out");
	RemoveTree(out3);
	std::string why3;
	const bool made3 = MakeBundle(dv, b3, lie, why3);
	CHECK(made3, "[%s] water copies bundle with copy 1 named throughout: %s", dv.name, why3.c_str());
	if(made3)
	{
		const int rc3 = Play(Args(b3, out3, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc3 == sceneshot::kDiffers && Count("colour EQUAL") == 1 && Has("COMPARE frame 100 160x120: colour EQUAL") &&
		          Count("in use from frame") == 1,
		      "[%s] copy 1 named for every frame (negative): 100 equal, 103 and 105 differ: exit %d (want %d)", dv.name, rc3,
		      (int)sceneshot::kDiffers);
	}
	RemoveTree(b);
	RemoveTree(out);
	RemoveTree(b2);
	RemoveTree(out2);
	RemoveTree(b3);
	RemoveTree(out3);
}

// P1 on a synthetic bundle: residency (stand-in) with two plot edits in the span, the module's water with coast waves
// (one dt above the clamp), the overlay bound from frame 2: every dumped frame equal byte for byte, colour and depth.
void CaseEqualsLive(Dev& dv)
{
	std::string why;
	const std::wstring b = Dir(dv, "full"), out = Dir(dv, "full_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, Spec(), why);
	CHECK(made, "[%s] bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	const int rc = Play(Args(b, out, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0, "[%s] exit %d (want 0): %s", dv.name, rc, Last().c_str());
	CHECK(Count("sceneshot: COMPARE frame") == 3 && Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3, "[%s] three equal comparisons",
	      dv.name);
	CHECK(Has(": the bundle's adapter"), "[%s] the bundle's adapter was not picked", dv.name);
	CHECK(Last().find("sceneshot: OK 3 shot(s), each byte-equal") == 0, "[%s] last line '%s'", dv.name, Last().c_str());
	CHECK(Exists(out + L"\\100.png") && Exists(out + L"\\103.png") && Exists(out + L"\\105.png") && Exists(out + L"\\105.depth.f32") &&
	          Exists(out + L"\\sceneshot.json") && !Exists(out + L"\\101.png"),
	      "[%s] outputs", dv.name);
	std::vector<uint8_t> dep;
	CHECK(ReadAll(out + L"\\103.depth.f32", dep) && dep.size() == (size_t)kW * kH * 4, "[%s] depth size %zu", dv.name, dep.size());
	CHECK(Has("the water image water/load0001.wtap") || Has("water image water/load0001.wtap"), "[%s] water image not used", dv.name);
	CHECK(Has("2 field loads") == false && Has("1 field loads"), "[%s] field loads line", dv.name);
	// the comparison has teeth: the same live run with two pixels and one depth value of frame 103's dumps changed after
	// the readback must be found, and only there
	std::string why2;
	Spec bad;
	bad.corruptDump = true;
	const std::wstring b2 = Dir(dv, "corrupt"), out2 = Dir(dv, "corrupt_out");
	RemoveTree(out2);
	const bool made2 = MakeBundle(dv, b2, bad, why2);
	CHECK(made2, "[%s] corrupt bundle: %s", dv.name, why2.c_str());
	if(made2)
	{
		const int rc2 = Play(Args(b2, out2, {"--dumped", "--compare", "--diff", "--adapter", dv.adapterArg}));
		CHECK(rc2 == sceneshot::kDiffers, "[%s] corrupt dump: exit %d (want %d)", dv.name, rc2, (int)sceneshot::kDiffers);
		CHECK(Has("COMPARE frame 103 160x120: colour DIFFERS (2 of 19200 pixels differ, 0 in alpha; largest channel difference 16;"),
		      "[%s] the colour difference line", dv.name);
		CHECK(Has("depth DIFFERS (1 of 19200 values differ"), "[%s] the depth difference line", dv.name);
		CHECK(Count("colour EQUAL") == 2, "[%s] frames 100 and 105 still equal", dv.name);
		CHECK(Last().find("sceneshot: FAILED 10 Differs:") == 0, "[%s] last line '%s'", dv.name, Last().c_str());
		CHECK(Exists(out2 + L"\\103.diff.png") && !Exists(out2 + L"\\100.diff.png"), "[%s] the difference image", dv.name);
	}
	RemoveTree(b2);
	RemoveTree(out2);
	// --tolerance PIXELS:DELTA: two colour pixels (16 and 1) and no depth difference pass at 2:16 and fail at 1:16 and
	// 2:15; with the depth value changed too, no colour tolerance passes
	Spec col;
	col.corruptDump = true;
	col.corruptDepth = false;
	const std::wstring b3 = Dir(dv, "colour"), out3 = Dir(dv, "colour_out");
	RemoveTree(out3);
	std::string why3;
	const bool made3 = MakeBundle(dv, b3, col, why3);
	CHECK(made3, "[%s] colour bundle: %s", dv.name, why3.c_str());
	if(made3)
	{
		int r3 = Play(Args(b3, out3, {"--dumped", "--compare", "--no-pictures", "--tolerance", "2:16", "--adapter", dv.adapterArg}));
		CHECK(r3 == 0 && Has("frame 103 differs within --tolerance 2:16") && Last().find("each within --tolerance") != std::string::npos,
		      "[%s] --tolerance 2:16: exit %d", dv.name, r3);
		r3 = Play(Args(b3, out3, {"--dumped", "--compare", "--no-pictures", "--tolerance", "1:16", "--adapter", dv.adapterArg}));
		CHECK(r3 == sceneshot::kDiffers && Has("beyond --tolerance 1:16"), "[%s] --tolerance 1:16: exit %d", dv.name, r3);
		r3 = Play(Args(b3, out3, {"--dumped", "--compare", "--no-pictures", "--tolerance", "2:15", "--adapter", dv.adapterArg}));
		CHECK(r3 == sceneshot::kDiffers, "[%s] --tolerance 2:15: exit %d", dv.name, r3);
		r3 = Play(Args(b3, out3, {"--dumped", "--compare", "--tolerance", "2", "--adapter", dv.adapterArg}));
		CHECK(r3 == sceneshot::kUsage && Has("--tolerance '2': PIXELS:DELTA"), "[%s] --tolerance 2: exit %d", dv.name, r3);
	}
	if(made2 && MakeBundle(dv, b2, bad, why2))
	{
		const int r4 = Play(Args(b2, out2, {"--dumped", "--compare", "--no-pictures", "--tolerance", "100:255", "--adapter", dv.adapterArg}));
		CHECK(r4 == sceneshot::kDiffers, "[%s] a depth difference passes no tolerance: exit %d", dv.name, r4);
	}
	RemoveTree(b2);
	RemoveTree(out2);
	RemoveTree(b3);
	RemoveTree(out3);
}

// The other selectors on the full bundle: --frame, --span, --camera (its frame in the span, compared), a camera outside
// the span (drawn after the last frame; --compare refused), --no-pictures.
void CaseSelectors(Dev& dv)
{
	std::string why;
	Spec sp;
	sp.namedFar = true;
	const std::wstring b = Dir(dv, "sel"), out = Dir(dv, "sel_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	int rc = Play(Args(b, out, {"--frame", "103", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Count("SHOT 103 frame 103") == 1 && Count("colour EQUAL") == 1, "[%s] --frame 103: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--span", "101:104", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Count("sceneshot: SHOT ") == 4 && Exists(out + L"\\101.png") && Exists(out + L"\\104.png"), "[%s] --span: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--camera", "default", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Has("its frame 103 is in the span") && Count("colour EQUAL") == 1 && Exists(out + L"\\default.png"),
	      "[%s] --camera default: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--camera", "far", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Has("its frame 999 is not in the span") && Exists(out + L"\\far.png"), "[%s] --camera far: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--camera", "far", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kNoDump && Last().find("sceneshot: FAILED 12 NoDump:") == 0, "[%s] --camera far --compare: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--frame", "101", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kNoDump && Has("frame 101 has no dump"), "[%s] --frame 101 --compare: exit %d", dv.name, rc);
	const std::wstring out2 = Dir(dv, "sel_nopic");
	RemoveTree(out2);
	rc = Play(Args(b, out2, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Exists(out2 + L"\\sceneshot.json") && !Exists(out2 + L"\\100.png") && !Exists(out2 + L"\\100.depth.f32"),
	      "[%s] --no-pictures: exit %d", dv.name, rc);
	RemoveTree(out2);
	RemoveTree(out);
}

// render_tiles=none (no feed), no overlay: the simpler path, equal too.
void CaseNoResidency(Dev& dv)
{
	std::string why;
	Spec sp;
	sp.bake = false;
	sp.overlay = false;
	const std::wstring b = Dir(dv, "plain"), out = Dir(dv, "plain_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	const int rc = Play(Args(b, out, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3, "[%s] exit %d: %s", dv.name, rc, Last().c_str());
	RemoveTree(b);
	RemoveTree(out);
}

// Issue 3911 (format version 2): the fog of war replayed from the bundle. The fog's table reads (feed.bin PLOTS caller 3,
// one of them for an edit inside the span), its clock (fog_clock.bin, T 0.2 s at the span's start) and each frame's nowMs:
// every dumped frame equal byte for byte with the fog drawn (flag bit 3). The clock matters: the same bundle with T moved
// by 10.37 s differs; without fog_clock.bin the player refuses to draw.
void CaseFog(Dev& dv)
{
	std::string why;
	Spec sp;
	sp.fog = true;
	sp.bake = false;
	sp.water = false;
	const std::wstring b = Dir(dv, "fog"), out = Dir(dv, "fog_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] fog bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	int rc = Play(Args(b, out, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0, "[%s] fog: exit %d (want 0): %s", dv.name, rc, Last().c_str());
	CHECK(Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3 && Count("MISMATCH") == 0, "[%s] fog: three equal comparisons", dv.name);
	CHECK(Has("sceneshot: render_config: fog 1 (level 1)") && Has("sceneshot: fog started: FOWLevel 1, mask 512, table 8x4") &&
	          Has("sceneshot: fog_clock.bin in use: T 0.200000 s, last fog frame at 800.000 ms"),
	      "[%s] fog: the player's fog lines", dv.name);
	// teeth: FOWAnim's T moved by 10.37 s: the clouds move by 2 T world units (x 1, 1.5, 2 per plane), k = 1/511 of the
	// cloud texture each (on a map under 500 units wide k is 0 and T has no effect: hence the 8-plot-wide map)
	Spec shifted = sp;
	shifted.fogClockShift = 10.37;
	const std::wstring b2 = Dir(dv, "fog_shift"), out2 = Dir(dv, "fog_shift_out");
	RemoveTree(out2);
	std::string why2;
	const bool made2 = MakeBundle(dv, b2, shifted, why2);
	CHECK(made2, "[%s] shifted fog bundle: %s", dv.name, why2.c_str());
	if(made2)
	{
		rc = Play(Args(b2, out2, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kDiffers && Count("colour DIFFERS") >= 1 && Count("depth EQUAL") == 3,
		      "[%s] fog clock moved by 10.37 s: exit %d (want %d), %d frames differ", dv.name, rc, (int)sceneshot::kDiffers, Count("colour DIFFERS"));
	}
	RemoveTree(b2);
	RemoveTree(out2);
	// fog_clock.bin missing: refused before anything is drawn or written
	Spec noClock = sp;
	noClock.fogNoClock = true;
	const std::wstring b3 = Dir(dv, "fog_noclock"), out3 = Dir(dv, "fog_noclock_out");
	RemoveTree(out3);
	std::string why3;
	const bool made3 = MakeBundle(dv, b3, noClock, why3);
	CHECK(made3, "[%s] fog bundle without its clock: %s", dv.name, why3.c_str());
	if(made3)
	{
		rc = Play(Args(b3, out3, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kReplayFailed && Last().find("fog_clock.bin is missing") != std::string::npos && !Exists(out3),
		      "[%s] no fog_clock.bin: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kReplayFailed, Last().c_str());
	}
	RemoveTree(b3);
	RemoveTree(out3);
	// --set render_fow=0 draws without the fog: not the recorded picture (the fog is drawn in the bundle's frames)
	rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--set", "render_fow=0", "--adapter", dv.adapterArg}));
	CHECK(rc != 0 && Has("MISMATCH frame 100: the fog drawn (flag bit 3): live 1, replay 0") && Count("colour DIFFERS") == 3,
	      "[%s] --set render_fow=0: exit %d, %d frames differ (want 3: the fog is in the picture)", dv.name, rc, Count("colour DIFFERS"));
	RemoveTree(b);
	RemoveTree(out);
}

// Format version 6 (ticket 0403): the fog at FOWLevel 3 with its particles. The live side spawns them in the three frames
// before the span and draws them in every frame; particles.bin holds their state at the span's first frame. The player
// loads it after the fog's clock (FowScene::setParticleState) and draws every dumped frame byte-equal, colour and depth.
// Teeth: --set render_fow_particles=0 draws no particles and differs. Refusals: render_fow_particles=1 without the member
// or a note (the checker: MissingMember, nothing written); a state of another flow field (the player: ReplayFailed,
// nothing written). With the recorder's gap note and no member the bundle plays without particles (a NOTE, differs).
void CaseFogParticles(Dev& dv)
{
	std::string why;
	Spec sp;
	sp.fog = true;
	sp.fogParticles = true;
	sp.bake = false;
	sp.water = false;
	const std::wstring b = Dir(dv, "fogpart"), out = Dir(dv, "fogpart_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] fog particles bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	int rc = Play(Args(b, out, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
	CHECK(rc == 0, "[%s] fog particles: exit %d (want 0): %s", dv.name, rc, Last().c_str());
	CHECK(Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3 && Count("MISMATCH") == 0, "[%s] fog particles: three equal comparisons",
	      dv.name);
	CHECK(Has("sceneshot: render_config: fog particles 1") && Has("sceneshot: particles.bin in use: spawned, seed 1"),
	      "[%s] fog particles: the player's particle lines", dv.name);
	// teeth: the same bundle without the particles differs
	const std::wstring out2 = Dir(dv, "fogpart_off_out");
	RemoveTree(out2);
	rc = Play(Args(b, out2, {"--dumped", "--compare", "--no-pictures", "--set", "render_fow_particles=0", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kDiffers && Count("colour DIFFERS") >= 1 && Has("particles.bin not used"),
	      "[%s] --set render_fow_particles=0: exit %d (want %d), %d frames differ", dv.name, rc, (int)sceneshot::kDiffers, Count("colour DIFFERS"));
	RemoveTree(out2);
	// render_fow_particles=1 without the member or a note: refused by the checker
	Spec noMember = sp;
	noMember.partNoMember = true;
	const std::wstring b3 = Dir(dv, "fogpart_nomember"), out3 = Dir(dv, "fogpart_nomember_out");
	RemoveTree(out3);
	std::string why3;
	const bool made3 = MakeBundle(dv, b3, noMember, why3);
	CHECK(made3, "[%s] fog particles bundle without the member: %s", dv.name, why3.c_str());
	if(made3)
	{
		rc = Play(Args(b3, out3, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
		CHECK(rc == sb::RefusalExitCode(sb::Refusal::MissingMember) && Has("scenebundle: REFUSED MissingMember particles.bin") && !Exists(out3),
		      "[%s] no particles.bin: exit %d (want %d)", dv.name, rc, sb::RefusalExitCode(sb::Refusal::MissingMember));
	}
	RemoveTree(b3);
	RemoveTree(out3);
	// the recorder's gap note instead: plays without particles and says so
	Spec noted = noMember;
	noted.partNote = true;
	const std::wstring b4 = Dir(dv, "fogpart_note"), out4 = Dir(dv, "fogpart_note_out");
	RemoveTree(out4);
	std::string why4;
	const bool made4 = MakeBundle(dv, b4, noted, why4);
	CHECK(made4, "[%s] fog particles bundle with the gap note: %s", dv.name, why4.c_str());
	if(made4)
	{
		rc = Play(Args(b4, out4, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kDiffers && Has("sceneshot: NOTE: render_fow_particles=1 without particles.bin") && Count("colour DIFFERS") >= 1,
		      "[%s] the gap note: exit %d (want %d)", dv.name, rc, (int)sceneshot::kDiffers);
	}
	RemoveTree(b4);
	RemoveTree(out4);
	// a state of another flow field: the player refuses it before it draws
	Spec badHash = sp;
	badHash.partBadHash = true;
	const std::wstring b5 = Dir(dv, "fogpart_hash"), out5 = Dir(dv, "fogpart_hash_out");
	RemoveTree(out5);
	std::string why5;
	const bool made5 = MakeBundle(dv, b5, badHash, why5);
	CHECK(made5, "[%s] fog particles bundle of another flow field: %s", dv.name, why5.c_str());
	if(made5)
	{
		rc = Play(Args(b5, out5, {"--dumped", "--compare", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kReplayFailed && Last().find("another flow field") != std::string::npos && !Exists(out5),
		      "[%s] another flow field: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kReplayFailed, Last().c_str());
	}
	RemoveTree(b5);
	RemoveTree(out5);
	RemoveTree(b);
	RemoveTree(out);
}

// Format version 3 (issue 0364): render_decals=1 with decals/load0001.dtap. The player hands the member to its scene's decal
// reader before the field (the render: lines of the reader's file entry and the residency's setDecals), with the recorded mods'
// folder as C5_DOCUMENTS; P1 equal. Refusals: the recorded mods' folder missing (Install), --mods not a MODS folder (Usage), a
// texture name the install lacks (ReplayFailed, nothing written). A bundle with the recorder's no-set note plays without a set.
void CaseDecals(Dev& dv)
{
	std::string why;
	const std::wstring mods = g_work + L"\\docs_0364\\My Games\\Sid Meier's Civilization 5\\MODS";
	SHCreateDirectoryExW(nullptr, mods.c_str(), nullptr);
	Spec sp;
	sp.decals = true;
	sp.water = false;
	sp.overlay = false;
	sp.decalMods = NarrowW(mods);
	const std::wstring b = Dir(dv, "decals"), out = Dir(dv, "decals_out");
	RemoveTree(out);
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] decal bundle: %s", dv.name, why.c_str());
	if(!made)
		return;
	int rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Count("colour EQUAL") == 3 && Count("depth EQUAL") == 3 && Count("MISMATCH") == 0, "[%s] decals: exit %d (want 0): %s", dv.name, rc,
	      Last().c_str());
	CHECK(Has("sceneshot: render_config: decals 1, mods folder ") && Has("docs_0364/My Games/Sid Meier's Civilization 5/MODS"),
	      "[%s] decals: the render_config line with the recorded mods' folder", dv.name);
	CHECK(Has("sceneshot: decals/load0001.dtap: decal set generation 5 (load 1, 3 decals, 3 names) applied before the span"),
	      "[%s] decals: the player's hand-over line", dv.name);
	CHECK(Has("decals: generation 5 (load 1) applied from a file") && Has("decals: set generation 5 (load 1) in use: 3 decals"),
	      "[%s] decals: the scene's reader and the residency took the set", dv.name);
	// --set render_decals=0: no set applied (a diagnosis)
	rc = Play(Args(b, out, {"--dumped", "--no-pictures", "--set", "render_decals=0", "--adapter", dv.adapterArg}));
	CHECK(rc == 0 && Has("sceneshot: decals off: no decal set is applied") && !Has("applied from a file"), "[%s] --set render_decals=0: exit %d", dv.name,
	      rc);
	// --mods that is not a MODS folder
	RemoveTree(out);
	rc = Play(Args(b, out, {"--dumped", "--no-pictures", "--mods", NarrowW(g_work), "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kUsage && Last().find("does not end in") != std::string::npos && !Exists(out), "[%s] --mods not a MODS folder: exit %d: %s",
	      dv.name, rc, Last().c_str());
	// the recorded mods' folder missing
	{
		Spec gone = sp;
		gone.decalMods = NarrowW(g_work + L"\\docs_gone\\My Games\\Sid Meier's Civilization 5\\MODS");
		const std::wstring b2 = Dir(dv, "decals_gone"), out2 = Dir(dv, "decals_gone_out");
		RemoveTree(out2);
		std::string w2;
		CHECK(MakeBundle(dv, b2, gone, w2), "[%s] decal bundle (mods gone): %s", dv.name, w2.c_str());
		rc = Play(Args(b2, out2, {"--dumped", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kInstall && Last().find("mods folder") != std::string::npos && Last().find("is missing") != std::string::npos &&
		          !Exists(out2),
		      "[%s] the recorded mods' folder missing: exit %d: %s", dv.name, rc, Last().c_str());
		RemoveTree(b2);
	}
	// a texture name the install does not hold: the residency's setDecals refuses, nothing drawn or written
	{
		Spec badName = sp;
		badName.decalNames = {"no_such_decal_texture_0364.dds", "roadsandrails_new_h.dds", "blank_texture_spec_decal.dds"};
		const std::wstring b3 = Dir(dv, "decals_badname"), out3 = Dir(dv, "decals_badname_out");
		RemoveTree(out3);
		std::string w3;
		CHECK(MakeBundle(dv, b3, badName, w3), "[%s] decal bundle (bad name): %s", dv.name, w3.c_str());
		rc = Play(Args(b3, out3, {"--dumped", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kReplayFailed && Last().find("no_such_decal_texture_0364.dds") != std::string::npos && !Exists(out3),
		      "[%s] a decal texture the install lacks: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kReplayFailed, Last().c_str());
		RemoveTree(b3);
	}
	// render_decals=1 without a member, with the recorder's no-set note: played without a set
	{
		Spec none = sp;
		none.decalNoMember = true;
		const std::wstring b4 = Dir(dv, "decals_noset"), out4 = Dir(dv, "decals_noset_out");
		RemoveTree(out4);
		std::string w4;
		CHECK(MakeBundle(dv, b4, none, w4), "[%s] decal bundle (no set): %s", dv.name, w4.c_str());
		rc = Play(Args(b4, out4, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == 0 && Has("sceneshot: no decal member: decals: no decal set in force at the span's first frame") && !Has("applied from a file"),
		      "[%s] no-set note: exit %d: %s", dv.name, rc, Last().c_str());
		RemoveTree(b4);
		RemoveTree(out4);
	}
	RemoveTree(b);
	RemoveTree(out);
}

// Ticket 0614: a recorded mods' folder inside a pool instance (a folder holding instance.json). Unclaimed: played as before,
// with the NOTE naming the instance. Claimed (<C5_POOL_DIR>\claims\<name>.json exists, a readable or an unreadable one):
// refused before anything is drawn (Install, the instance, the claim file and its owner named, --out not created).
void CaseClaimedInstance(Dev& dv)
{
	std::string why;
	const std::wstring inst = g_work + L"\\inst_0614\\c5h-T0614", pool = g_work + L"\\pool_0614";
	const std::wstring mods = inst + L"\\Documents\\My Games\\Sid Meier's Civilization 5\\MODS";
	SHCreateDirectoryExW(nullptr, mods.c_str(), nullptr);
	SHCreateDirectoryExW(nullptr, (pool + L"\\claims").c_str(), nullptr);
	const std::string ij = "{\"name\": \"c5h-T0614\", \"state\": \"ready\"}";
	WriteAll(inst + L"\\instance.json", std::vector<uint8_t>(ij.begin(), ij.end()));
	wchar_t saved[4096];
	const DWORD ns = GetEnvironmentVariableW(L"C5_POOL_DIR", saved, 4096);
	SetEnvironmentVariableW(L"C5_POOL_DIR", pool.c_str());
	Spec sp;
	sp.decals = true;
	sp.water = false;
	sp.overlay = false;
	sp.decalMods = NarrowW(mods);
	const std::wstring b = Dir(dv, "claimed"), out = Dir(dv, "claimed_out"), claim = pool + L"\\claims\\c5h-t0614.json";
	RemoveTree(out);
	DeleteFileW(claim.c_str());
	const bool made = MakeBundle(dv, b, sp, why);
	CHECK(made, "[%s] claimed-instance bundle: %s", dv.name, why.c_str());
	if(made)
	{
		int rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == 0 && Has("sceneshot: NOTE: the sources are pool instance c5h-T0614's own (unclaimed now)"),
		      "[%s] unclaimed instance: exit %d (want 0 with the NOTE): %s", dv.name, rc, Last().c_str());
		RemoveTree(out);
		const std::string cj = "{\"name\": \"c5h-T0614\", \"owner\": \"t-0614\", \"ticket\": \"0614\", \"expires_at\": \"2100-01-01T00:00:00-05:00\"}";
		WriteAll(claim, std::vector<uint8_t>(cj.begin(), cj.end()));
		rc = Play(Args(b, out, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kInstall && Last().find("the mods' Documents folder") != std::string::npos &&
		          Last().find("lies in pool instance c5h-T0614, which the pool shows claimed") != std::string::npos &&
		          Last().find("owner t-0614, ticket 0614") != std::string::npos && Last().find("--asset-root DIR") != std::string::npos &&
		          !Has("sceneshot: COMPARE") && !Exists(out),
		      "[%s] claimed instance: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kInstall, Last().c_str());
		WriteAll(claim, std::vector<uint8_t>(3, 'x'));   // an unreadable claim still holds the instance
		rc = Play(Args(b, out, {"--dumped", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kInstall && Last().find("owner ?") != std::string::npos && !Exists(out), "[%s] unreadable claim: exit %d: %s", dv.name,
		      rc, Last().c_str());
	}
	DeleteFileW(claim.c_str());
	SetEnvironmentVariableW(L"C5_POOL_DIR", ns > 0 && ns < 4096 ? saved : nullptr);
	RemoveTree(b);
	RemoveTree(out);
	RemoveTree(g_work + L"\\inst_0614");
	RemoveTree(pool);
}

// The negatives of contract 10 and the acceptance: each exit code and output line, and nothing written.
void CaseRefusals(Dev& dv)
{
	std::string why;
	const std::wstring b = Dir(dv, "full");   // CaseEqualsLive's bundle
	const std::wstring out = Dir(dv, "neg_out");
	RemoveTree(out);
	sb::CheckResult cr;
	CHECK(sb::CheckBundle(b, &cr).ok(), "[%s] the full bundle is missing (CaseEqualsLive first): %s", dv.name, cr.line().c_str());
	// a missing bundle fails (never skips)
	int rc = Play(Args(g_work + L"\\no_such_bundle", out, {"--dumped", "--adapter", dv.adapterArg}));
	CHECK(rc == sb::RefusalExitCode(sb::Refusal::NoManifest) && rc == 21 && Has("scenebundle: REFUSED NoManifest") &&
	          Last().find("sceneshot: FAILED 21 NoManifest: no bundle at") == 0,
	      "[%s] missing bundle: exit %d", dv.name, rc);
	CHECK(!Exists(out), "[%s] missing bundle: --out was created", dv.name);
	// the command line
	rc = Play({"--bundle", NarrowW(b), "--out", NarrowW(out)});
	CHECK(rc == sceneshot::kUsage && Has("exactly one of --frame"), "[%s] no selector: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--frame", "100", "--dumped"}));
	CHECK(rc == sceneshot::kUsage, "[%s] two selectors: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--dumped", "--diff"}));
	CHECK(rc == sceneshot::kUsage && Has("--diff needs --compare"), "[%s] --diff alone: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--dumped", "--bogus"}));
	CHECK(rc == sceneshot::kUsage && Has("unknown argument '--bogus'"), "[%s] unknown argument: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--span", "104:101"}));
	CHECK(rc == sceneshot::kUsage, "[%s] reversed span: exit %d", dv.name, rc);
	// ticket 0625: main() unsets an inherited C5_SCENE_ASSET_ROOT for the cases; the player's own rule (ticket 0614: the
	// variable together with --install or --mods is a usage error, never settled silently) is unchanged
	SetEnvironmentVariableW(L"C5_SCENE_ASSET_ROOT", (g_work + L"\\asset_root_0625").c_str());
	rc = Play(Args(b, out, {"--dumped", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kUsage && Has("C5_SCENE_ASSET_ROOT '") && Has("not with --install (unset C5_SCENE_ASSET_ROOT"),
	      "[%s] C5_SCENE_ASSET_ROOT with --install: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kUsage, Last().c_str());
	rc = Play({"--bundle", NarrowW(b), "--out", NarrowW(out), "--dumped", "--mods", NarrowW(g_work), "--adapter", dv.adapterArg});
	CHECK(rc == sceneshot::kUsage && Has("not with --mods (unset C5_SCENE_ASSET_ROOT"),
	      "[%s] C5_SCENE_ASSET_ROOT with --mods: exit %d (want %d): %s", dv.name, rc, (int)sceneshot::kUsage, Last().c_str());
	SetEnvironmentVariableW(L"C5_SCENE_ASSET_ROOT", nullptr);
	// a camera name not in the bundle
	rc = Play(Args(b, out, {"--camera", "nowhere", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kNoCamera && Has("camera 'nowhere' is not in the bundle (named cameras: default)"), "[%s] camera: exit %d", dv.name, rc);
	// a frame outside the span
	rc = Play(Args(b, out, {"--frame", "99", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kFrameOutside && Has("frame 99 is outside the recorded span 100:105"), "[%s] frame 99: exit %d", dv.name, rc);
	rc = Play(Args(b, out, {"--span", "104:107", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kFrameOutside && Has("span 104:107 is outside"), "[%s] span 104:107: exit %d", dv.name, rc);
	CHECK(!Exists(out), "[%s] refusals: --out was created", dv.name);
	// a settle timeout: its own exit code and no picture (the live budgets: no burst, half a bake per second)
	rc = Play(Args(b, out, {"--frame", "100", "--bake-rate", "0.5", "--bake-burst", "0", "--settle-ms", "300", "--adapter", dv.adapterArg}));
	CHECK(rc == sceneshot::kSettleTimeout && Has("NOT settled") && Last().find("sceneshot: FAILED 5 SettleTimeout: frame 100:") == 0 &&
	          Has("(no picture)"),
	      "[%s] settle timeout: exit %d", dv.name, rc);
	CHECK(!Exists(out + L"\\100.png") && !Exists(out + L"\\sceneshot.json"), "[%s] settle timeout: a picture was written", dv.name);
	// a copy of the bundle with an older format_version, a changed member byte, an unlisted file
	const std::wstring c = Dir(dv, "copy");
	auto copyBundle = [&]() {
		RemoveTree(c);
		CreateDirectoryW(c.c_str(), nullptr);
		WIN32_FIND_DATAW fd;
		std::vector<std::wstring> sub = {L"", L"heightfield", L"water", L"overlay", L"dumps", L"cameras"};
		for(const std::wstring& s : sub)
		{
			const std::wstring from = s.empty() ? b : b + L"\\" + s, to = s.empty() ? c : c + L"\\" + s;
			CreateDirectoryW(to.c_str(), nullptr);
			HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
			if(h == INVALID_HANDLE_VALUE)
				continue;
			do
				if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					CopyFileW((from + L"\\" + fd.cFileName).c_str(), (to + L"\\" + fd.cFileName).c_str(), FALSE);
			while(FindNextFileW(h, &fd));
			FindClose(h);
		}
	};
	copyBundle();
	std::vector<uint8_t> js;
	CHECK(ReadAll(c + L"\\scene.json", js), "[%s] scene.json", dv.name);
	std::string s(js.begin(), js.end());
	// ticket 0493: the bundle is written as format version 7 (6 under 0403, 5 under 0440, 4 under 4457, 3 under 0364); a
	// version-6 bundle (recorded before the feed's read order of f916762a5) is the older one, refused at load with a message
	// naming both versions and the change, not at the replay's "out of order"
	// ticket 4875: written as format version 9 (the model tap member); a version-7 bundle names versions 8 and 9
	// ticket 4749: written as format version 10 (shadow.bin's form 2); version 9 is still read, so the refusal names 9 as the
	// oldest version read and lists versions 8 to 10
	// ticket 0622: written as format version 11 (water_frames.bin); the refusal lists versions 8 to 11
	const size_t at = s.find("\"format_version\": 11");
	CHECK(at != std::string::npos, "[%s] format_version not found", dv.name);
	if(at != std::string::npos)
	{
		s.replace(at, strlen("\"format_version\": 11"), "\"format_version\": 7");
		WriteAll(c + L"\\scene.json", std::vector<uint8_t>(s.begin(), s.end()));
		rc = Play(Args(c, out, {"--dumped", "--adapter", dv.adapterArg}));
		CHECK(rc == sb::RefusalExitCode(sb::Refusal::OlderVersion) && Has("scenebundle: REFUSED OlderVersion") &&
		          Has("format_version 7 is older than 9") && Has("version 8: the trees and the near-camera pairs") &&
		          Has("version 9: the cities and landmarks") && Has("version 10: shadow.bin's file form 2") &&
		          Has("version 11: the member water_frames.bin") && !Has("out of order"),
		      "[%s] older format: exit %d", dv.name, rc);
		// ticket 4749: the same bundle as format version 9 (it has no shadow.bin, so no member form changes) replays as before
		copyBundle();
		std::string s9(js.begin(), js.end());
		s9.replace(s9.find("\"format_version\": 11"), strlen("\"format_version\": 11"), "\"format_version\": 9");
		{   // ticket 0622: a format-9 bundle has no water_frames.bin (the checker refuses one): its entry and file go
			const size_t a = s9.find("\"water_frames.bin\": {");
			CHECK(a != std::string::npos, "[%s] the bundle has water_frames.bin", dv.name);
			if(a != std::string::npos)
			{
				const size_t e = s9.find('}', a), c0 = s9.rfind(',', a);
				s9.erase(c0, e + 1 - c0);
			}
			DeleteFileW((c + L"\\water_frames.bin").c_str());
		}
		WriteAll(c + L"\\scene.json", std::vector<uint8_t>(s9.begin(), s9.end()));
		const std::wstring out9 = Dir(dv, "neg_out9");   // its own: this case's `out` must stay unmade (the refusals)
		RemoveTree(out9);
		rc = Play(Args(c, out9, {"--dumped", "--compare", "--no-pictures", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kOk && Has("(format 9, save"), "[%s] format 9 read: exit %d", dv.name, rc);
		RemoveTree(out9);
	}
	copyBundle();
	std::vector<uint8_t> fb;
	CHECK(ReadAll(c + L"\\feed.bin", fb) && fb.size() > 40, "[%s] feed.bin", dv.name);
	if(fb.size() > 40)
	{
		fb[30] ^= 0x40;
		WriteAll(c + L"\\feed.bin", fb);
		rc = Play(Args(c, out, {"--dumped", "--adapter", dv.adapterArg}));
		CHECK(rc == sb::RefusalExitCode(sb::Refusal::ShaMismatch) && Has("scenebundle: REFUSED ShaMismatch feed.bin"), "[%s] bad member: exit %d",
		      dv.name, rc);
	}
	copyBundle();
	WriteAll(c + L"\\stray.txt", std::vector<uint8_t>(3, 'x'));
	rc = Play(Args(c, out, {"--dumped", "--adapter", dv.adapterArg}));
	CHECK(rc == sb::RefusalExitCode(sb::Refusal::UnlistedFile) && Has("scenebundle: REFUSED UnlistedFile"), "[%s] unlisted file: exit %d", dv.name,
	      rc);
	RemoveTree(c);
	CHECK(!Exists(out), "[%s] refused bundles: --out was created", dv.name);
	// the EXE pin and render_tiles=tap
	Spec pin;
	pin.exePin = std::string(64, '0');
	const std::wstring bp = Dir(dv, "pin");
	if(MakeBundle(dv, bp, pin, why))
	{
		rc = Play(Args(bp, out, {"--dumped", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kInstall && Has("the bundle's pins.exe_sha256 is 0000"), "[%s] EXE pin: exit %d", dv.name, rc);
	}
	else
		CHECK(false, "[%s] pin bundle: %s", dv.name, why.c_str());
	RemoveTree(bp);
	Spec tap;
	tap.tiles = "tap";
	const std::wstring bt = Dir(dv, "tap");
	if(MakeBundle(dv, bt, tap, why))
	{
		rc = Play(Args(bt, out, {"--dumped", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kUnsupported && Has("render_tiles=tap"), "[%s] render_tiles=tap: exit %d", dv.name, rc);
	}
	else
		CHECK(false, "[%s] tap bundle: %s", dv.name, why.c_str());
	RemoveTree(bt);
	// issue 0440: render_units=1 with the recorder's gap notes: the checker accepts the bundle, the player refuses to draw
	// it (Unsupported, the note named, nothing written); --set render_units=0 draws it as a diagnostic (render_units_anim
	// dropped with it, as the server's parser wants)
	Spec ug;
	ug.unitsGap = true;
	const std::wstring bu = Dir(dv, "units_gap");
	if(MakeBundle(dv, bu, ug, why))
	{
		sb::CheckResult ucr;
		CHECK(sb::CheckBundle(bu, &ucr).ok(), "[%s] units gap bundle refused by the checker: %s", dv.name, ucr.line().c_str());
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg}));
		CHECK(rc == sceneshot::kUnsupported && Has("render_units=1 and the bundle notes 'gap: units: no unit database") &&
		          Has("sceneshot: render_config: units 1 (passes 0, anim 1)"),
		      "[%s] units gap: exit %d", dv.name, rc);
		CHECK(!Exists(out), "[%s] units gap: --out was created", dv.name);
		// ticket 4737 (rule A): a bundle without render_test_units_charge_any (recorded before 4737) replays the fights with
		// the 4775 rush-only rule (charge_any 0); with the key "1" the charge; a bad value is refused by the server's parser
		CHECK(Has("sceneshot: render_config: units charge_any 0, tier gate absent"), "[%s] units gap: the 4737 keys absent", dv.name);
		// ticket 4735 (rule A): without render_test_units_turn_first (recorded before 4735) no turn; with "1" the turn
		CHECK(Has("sceneshot: render_config: units turn_first 0"), "[%s] units gap: the 4735 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_turn_first=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units turn_first 1"),
		      "[%s] units gap with render_test_units_turn_first=1: exit %d", dv.name, rc);
		// tickets 4745 + 4747 (rule A): without render_test_units_reform_first (recorded before 4745) no reform; with "1" it
		CHECK(Has("sceneshot: render_config: units reform_first 0"), "[%s] units gap: the 4745 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_reform_first=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units reform_first 1"),
		      "[%s] units gap with render_test_units_reform_first=1: exit %d", dv.name, rc);
		// ticket 4742 (rule A): without render_test_units_walk_home_in_fight (recorded before 4742) the walk after the fight's
		// end; with "1" the walk home inside the fight
		CHECK(Has("sceneshot: render_config: units walk_home 0"), "[%s] units gap: the 4742 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_walk_home_in_fight=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units walk_home 1"),
		      "[%s] units gap with render_test_units_walk_home_in_fight=1: exit %d", dv.name, rc);
		// ticket 4748 (rule A): without render_test_units_city_in_place (recorded before 4748) a melee attacker on a city
		// approaches first; with "1" it strikes from where it stands
		CHECK(Has("sceneshot: render_config: units city_in_place 0"), "[%s] units gap: the 4748 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_city_in_place=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units city_in_place 1"),
		      "[%s] units gap with render_test_units_city_in_place=1: exit %d", dv.name, rc);
		// ticket 4746 (rule A): without render_test_units_ready_after_move (recorded before 4746) every waiting figure
		// enters combat ready; with "1" only after a movement (stock's state 2)
		CHECK(Has("sceneshot: render_config: units ready_after_move 0"), "[%s] units gap: the 4746 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_ready_after_move=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units ready_after_move 1"),
		      "[%s] units gap with render_test_units_ready_after_move=1: exit %d", dv.name, rc);
		// ticket 4750 (rule A): without render_test_units_defend_from_stance (recorded before 4750) a non-answering
		// defender bombard-defends at once; with the key "1" it takes the stance first
		CHECK(Has("sceneshot: render_config: units defend_from_stance 0"), "[%s] units gap: the 4750 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_defend_from_stance=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units defend_from_stance 1"),
		      "[%s] units gap with render_test_units_defend_from_stance=1: exit %d", dv.name, rc);
		// ticket 5050 (rule A): without render_test_units_reform_ends_at_arrival / _attack_from_stance (recorded before
		// 5050) the rules before 5050; with "1" (the host's default since 5050 W74, so every new bundle carries it) stock's
		// phase-1 end at the last arrival / the attacker's phase-2 stance; with "0" the rules before 5050 again
		CHECK(Has("sceneshot: render_config: units reform_ends_at_arrival 0, attack_from_stance 0"),
		      "[%s] units gap: the 5050 keys absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_reform_ends_at_arrival=1",
		                         "--set", "render_test_units_attack_from_stance=1"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units reform_ends_at_arrival 1, attack_from_stance 1"),
		      "[%s] units gap with the 5050 keys = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_reform_ends_at_arrival=0",
		                         "--set", "render_test_units_attack_from_stance=0"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units reform_ends_at_arrival 0, attack_from_stance 0"),
		      "[%s] units gap with the 5050 keys = 0: exit %d", dv.name, rc);
		// ticket 5057 (rule A): without render_units_rush_ride_through / _defender_waits_first_landing (recorded before
		// 5057) the rules before 5057; with "1" (the host's default, so every new bundle carries it) the ride-through and
		// the type 2 wait; with "0" the rules before 5057 again
		CHECK(Has("sceneshot: render_config: units rush_ride_through 0, defender_waits_first_landing 0"),
		      "[%s] units gap: the 5057 keys absent", dv.name);
		// ticket 5066 (rule A): without render_units_fight_after_walk (recorded before 5066) the rules before 5066
		CHECK(Has("sceneshot: render_config: units fight_after_walk 0"), "[%s] units gap: the 5066 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_rush_ride_through=1",
		                         "--set", "render_units_defender_waits_first_landing=1"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units rush_ride_through 1, defender_waits_first_landing 1"),
		      "[%s] units gap with the 5057 keys = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_rush_ride_through=0",
		                         "--set", "render_units_defender_waits_first_landing=0"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units rush_ride_through 0, defender_waits_first_landing 0"),
		      "[%s] units gap with the 5057 keys = 0: exit %d", dv.name, rc);
		// ticket 5053 (rule A): without render_test_units_death_by_variant / _end_after_dying (recorded before 5053) or
		// with "0" (the host's default) the rules before 5053; with "1" the death by variant and the end after the dying
		CHECK(Has("sceneshot: render_config: units death_by_variant 0, end_after_dying 0"),
		      "[%s] units gap: the 5053 keys absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_death_by_variant=1",
		                         "--set", "render_test_units_end_after_dying=1"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units death_by_variant 1, end_after_dying 1"),
		      "[%s] units gap with the 5053 keys = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_death_by_variant=0",
		                         "--set", "render_test_units_end_after_dying=0"}));
		CHECK(rc == sceneshot::kUnsupported &&
		          Has("sceneshot: render_config: units death_by_variant 0, end_after_dying 0"),
		      "[%s] units gap with the 5053 keys = 0: exit %d", dv.name, rc);
		// ticket 5059 (rule A): without render_units_victory_word (recorded before 5059) the rule before 5059; with "1" (the
		// host's default, so every new bundle carries it) victory only for a member with the enable word Victory; with "0"
		// the rule before 5059 again
		CHECK(Has("sceneshot: render_config: units victory_word 0"), "[%s] units gap: the 5059 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_victory_word=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units victory_word 1"),
		      "[%s] units gap with the 5059 key = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_victory_word=0"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units victory_word 0"),
		      "[%s] units gap with the 5059 key = 0: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_fight_after_walk=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units fight_after_walk 1"),
		      "[%s] units gap with the 5066 key = 1: exit %d", dv.name, rc);
		// ticket 5067 S1 (rule A): without render_units_type2_phase2 (recorded before 5067) no phase 2; "1" the phase 2;
		// "0" none again
		CHECK(Has("sceneshot: render_config: units type2_phase2 0"), "[%s] units gap: the 5067 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_type2_phase2=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units type2_phase2 1"),
		      "[%s] units gap with the 5067 key = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_type2_phase2=0"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units type2_phase2 0"),
		      "[%s] units gap with the 5067 key = 0: exit %d", dv.name, rc);
		// ticket 0743 (rule A): without render_units_melee_reach (recorded before 0743) the reach before 0743; "1" stock's
		// melee reach; "0" the rule before again
		CHECK(Has("sceneshot: render_config: units melee_reach 0"), "[%s] units gap: the 0743 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_melee_reach=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units melee_reach 1"),
		      "[%s] units gap with the 0743 key = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_melee_reach=0"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units melee_reach 0"),
		      "[%s] units gap with the 0743 key = 0: exit %d", dv.name, rc);
		// ticket 5061 (rule A): without render_units_defender_step (recorded before 5061) the defender stands; "1" stock's
		// type 2 defender step; "0" the rule before again
		CHECK(Has("sceneshot: render_config: units defender_step 0"), "[%s] units gap: the 5061 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_defender_step=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units defender_step 1"),
		      "[%s] units gap with the 5061 key = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_defender_step=0"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units defender_step 0"),
		      "[%s] units gap with the 5061 key = 0: exit %d", dv.name, rc);

		// ticket 5070: without render_units_positions / render_units_ready_hold_ms (recorded before 4753) own and the default
		// hold; a bundle recorded with exe replays with the EXE's positions (before 5070 the player stayed at own whatever the
		// bundle said: 0742's overtaking bundle replayed its walk and both fights at other times); a bad value is refused by
		// the server's own parser
		CHECK(Has("sceneshot: render_config: units positions own, ready_hold_ms 30000"), "[%s] units gap: the 4753 keys absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_positions=exe", "--set",
		                         "render_units_ready_hold_ms=5000"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units positions exe, ready_hold_ms 5000"),
		      "[%s] units gap with render_units_positions=exe: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_positions=own"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units positions own, ready_hold_ms 30000"),
		      "[%s] units gap with render_units_positions=own: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_positions=sideways"}));
		CHECK(rc == sceneshot::kUnsupported && Has("render_units_positions=sideways: own or exe") &&
		          !Has("sceneshot: render_config: units positions"),
		      "[%s] units gap with render_units_positions=sideways: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg}));   // the cases below read the lines of a plain run
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units positions own, ready_hold_ms 30000"),
		      "[%s] units gap (plain run after the 5070 keys): exit %d", dv.name, rc);
		// ticket 5065 (rule A): without render_units_orders_after_walk (recorded before 5065) the rule before 5065; with "1"
		// (the host's default) a walking unit's work and fortify after its walk
		CHECK(Has("sceneshot: render_config: units orders_after_walk 0"), "[%s] units gap: the 5065 key absent", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units_orders_after_walk=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units orders_after_walk 1"),
		      "[%s] units gap with the 5065 key = 1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_charge_any=1", "--set",
		                         "render_test_units_tier_gate=1"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units charge_any 1, tier gate 1"),
		      "[%s] units gap with render_test_units_charge_any=1: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_charge_any=0"}));
		CHECK(rc == sceneshot::kUnsupported && Has("sceneshot: render_config: units charge_any 0, tier gate absent"),
		      "[%s] units gap with render_test_units_charge_any=0: exit %d", dv.name, rc);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_test_units_charge_any=on"}));
		CHECK(rc == sceneshot::kUnsupported && Has("render_test_units_charge_any=on: 0 or 1") &&
		          !Has("sceneshot: render_config: units charge_any"),
		      "[%s] units gap with render_test_units_charge_any=on: exit %d", dv.name, rc);
		CHECK(!Exists(out), "[%s] units gap (4737 keys): --out was created", dv.name);
		rc = Play(Args(bu, out, {"--dumped", "--adapter", dv.adapterArg, "--set", "render_units=0"}));
		CHECK(rc == sceneshot::kOk && Has("sceneshot: render_config: units 0 (passes 0, anim 0)"), "[%s] units gap with --set render_units=0: exit %d",
		      dv.name, rc);
		RemoveTree(out);
	}
	else
		CHECK(false, "[%s] units gap bundle: %s", dv.name, why.c_str());
	RemoveTree(bu);
	// the install's EXE missing
	rc = Play({"--bundle", NarrowW(b), "--out", NarrowW(out), "--dumped", "--install", NarrowW(g_work + L"\\no_install"), "--adapter", dv.adapterArg});
	CHECK(rc == sceneshot::kInstall && Has("is missing"), "[%s] no install: exit %d", dv.name, rc);
	// an adapter that is not here
	rc = Play(Args(b, out, {"--dumped", "--adapter", "00000000DEADBEEF"}));
	CHECK(rc == sceneshot::kNoDevice, "[%s] adapter: exit %d", dv.name, rc);
	CHECK(!Exists(out), "[%s] --out was created", dv.name);
	RemoveTree(out);
}

bool MakeDev(Dev& d, bool warp)
{
	const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	D3D_FEATURE_LEVEL fl;
	if(FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &d.dev, &fl,
	                            &d.ctx)) &&
	   FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls + 1, 1, D3D11_SDK_VERSION, &d.dev,
	                            &fl, &d.ctx)))
		return false;
	sceneshot::Device tmp;
	tmp.dev = d.dev;
	tmp.ctx = d.ctx;
	sceneshot::DescribeDevice(tmp);
	tmp.dev = nullptr;
	tmp.ctx = nullptr;
	d.description = tmp.description;
	d.luid = tmp.luid;
	d.adapterArg = warp ? "warp" : "bundle";
	return true;
}

}  // namespace

int main(int argc, char** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	setvbuf(stdout, nullptr, _IONBF, 0);
	std::vector<std::string> only;
	for(int i = 1; i < argc; ++i)
	{
		if(!strcmp(argv[i], "--work") && i + 1 < argc)
		{
			++i;
			g_work = sb::Widen(argv[i]);
			continue;
		}
		only.push_back(argv[i]);
	}
	if(g_work.empty())
	{
		printf("sceneshot_test: --work DIR is required\nRESULT: 0 passed, 1 failed, 0 skipped\n");
		return 2;
	}
	std::replace(g_work.begin(), g_work.end(), L'/', L'\\');
	CreateDirectoryW(g_work.c_str(), nullptr);
	// ticket 0589: never the Steam install by default (it is Firaxis' new build since 2026-11-19), only build 4390913
	const c5test::Install inst = c5test::GameDir();
	g_game = inst.dir;
	printf("sceneshot_test: installed game %s (from %s)\n", NarrowW(g_game).c_str(), inst.from.c_str());
	LogOpen(nullptr, &LiveLog);
	// issue 0364: the live side's decal mods' folders (ModFolders): never this machine's own Documents
	SetEnvironmentVariableW(L"C5_DOCUMENTS", (g_work + L"\\no-documents").c_str());
	// ticket 0625: every case chooses its own sources (--install of the test's install, --mods); an inherited
	// C5_SCENE_ASSET_ROOT (the rule of ticket 0614 for scene players, which teams export globally) would make each of those
	// runs a usage error. Run (compiled in) reads this process's environment, so the cases run without the variable;
	// CaseRefusals sets it on purpose to check that the player's rule still holds.
	{
		wchar_t inherited[4096];
		const DWORD n = GetEnvironmentVariableW(L"C5_SCENE_ASSET_ROOT", inherited, 4096);
		if(n > 0)
		{
			printf("sceneshot_test: C5_SCENE_ASSET_ROOT=%s inherited: unset for the cases (they name their own sources)\n",
			       n < 4096 ? NarrowW(std::wstring(inherited, n)).c_str() : "<longer than 4095 characters>");
			SetEnvironmentVariableW(L"C5_SCENE_ASSET_ROOT", nullptr);
		}
	}
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	CHECK(Exists(GameExe()), "the installed game's EXE is needed (C5_GAME_DIR or C5R_GAME_DIR, %s); a skip counts as failed",
	      NarrowW(GameExe()).c_str());
	const std::string buildWhy = Exists(GameExe()) ? c5test::ExeBuildWhy(GameExe()) : std::string("missing");
	if(Exists(GameExe()))
		CHECK(buildWhy.empty(), "%s", buildWhy.c_str());
	const bool warpOk = MakeDev(g_devs[0], true), hwOk = MakeDev(g_devs[1], false);
	CHECK(warpOk, "no WARP device");
	CHECK(hwOk, "no hardware device (the hardware cases fail; a skip counts as failed)");
	if(buildWhy.empty())
		for(int i = 0; i < 2; ++i)
		{
			Dev& dv = g_devs[i];
			if(!dv.dev || (!only.empty() && std::find(only.begin(), only.end(), std::string(dv.name)) == only.end()))
				continue;
			printf("sceneshot_test: adapter %s: '%s' (LUID %s)\n", dv.name, dv.description.c_str(), dv.luid.c_str());
			const int before = g_fail;
			CaseEqualsLive(dv);
			CaseSelectors(dv);
			CaseNoResidency(dv);
			CaseFog(dv);   // issue 3911
			CaseFogParticles(dv);   // ticket 0403
			CaseDecals(dv);   // issue 0364
			CaseClaimedInstance(dv);   // ticket 0614
			CaseArc(dv);      // ticket 0199 part 2
			CaseWaterCopies(dv);   // ticket 0622
			CaseRefusals(dv);
			RemoveTree(Dir(dv, "full"));
			RemoveTree(Dir(dv, "full_out"));
			RemoveTree(Dir(dv, "sel"));
			printf("sceneshot_test: adapter %s: %s\n", dv.name, g_fail == before ? "PASS" : "FAIL");
			if(g_fail != before)
				for(const std::string& l : g_log)
					printf("    live log: %s\n", l.c_str());
		}
	for(Dev& d : g_devs)
	{
		SafeRelease(d.ctx);
		SafeRelease(d.dev);
	}
	printf("RESULT: %d passed, %d failed, 0 skipped\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
