// sceneshot.cpp - see sceneshot.h (epic 0301, task 3905). Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// The replay (contract offline-scene-harness.md 4, 6, 7 and the "sceneshot" section): the bundle is checked whole first
// (scenebundle::CheckBundle: nothing is drawn from a bundle the checker refuses), then the scene is built as the render
// thread builds it (Scene::init without a section: the player's form, 3903) with SceneOptions made from the bundle's
// render_config (server.cfg keys, through ParseHostConfig, then the port of the host's SceneOpts below), the file feed in
// the residency's config, the file overlay source in the scene. The recorded inputs are then fed in the recorded order:
// every feed record up to a frame's loop iteration (a DRAIN record is drained and its changed plots go to
// Scene::plotsChanged, whose residency reads the next PLOTS record itself; a baseline PLOTS record is where the height
// field was applied, so the next heightfield member is loaded there and its takeBaseline reads it), the water image of
// the field's load and water_clock.bin before the first frame, then each frame of the span in turn with its camera block,
// its nowMs, its overlay pair and its target size, drawn into targets of the sinks' formats (colour B8G8R8A8_UNORM,
// depth R32_TYPELESS through a D32_FLOAT view). Frames before a shot are drawn too: the coast waves integrate every drawn
// frame's dt (contract 6). Before each shot the residency is settled (Scene::settle); a timeout ends the run with
// kSettleTimeout and no picture. A shot's colour and depth are read back, compared byte for byte with the server's own
// dumps (--compare) before any PNG is written, and then written.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sceneshot.h"
#include "srv_scene_bundle.h"
#include "srv_scene_sources.h"
#include "srv_render_scene.h"
#include "srv_render_host.h"
#include "srv_residency_source.h"
#include "srv_render_common.h"
#include "srv_scene_recorder.h"   // issue 4308: the shadow and ice members' file sources
#include "units/srv_units_core.h"       // issue 0440: the server's unit scene (MakeSceneUnitsWith, UnitsHostProbe)
#include "units/srv_units_feedport.h"   // issue 0440: its table copies from the file feed
#include "vp64_options.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default (the bundle's
// render_config names the resolved values the player uses; render_bake_radius -2 falls back to this default).
static Vp64Opt::Options g_sceneshotOptions;
const Vp64Opt::Options& SrvOptions() { return g_sceneshotOptions; }

// ticket 4938: the --option lines (KEY=VALUE, KEY an [Extras] option) parsed by the options loader into out, every other
// option at its default (out is reset first). False with why: no '=', a key that is not an [Extras] option, or any warning
// of the loader (a value it would not take). The player applies them to SrvOptions() before the scene starts.
static bool ApplyOptions(const std::vector<std::string>& lines, Vp64Opt::Options& out, std::string& why)
{
	out = Vp64Opt::Options();
	std::string text;
	for(const std::string& l : lines)
	{
		const size_t eq = l.find('=');
		std::string key = eq == std::string::npos ? std::string() : l.substr(0, eq);
		while(!key.empty() && (key.back() == ' ' || key.back() == '\t'))
			key.pop_back();
		while(!key.empty() && (key[0] == ' ' || key[0] == '\t'))
			key.erase(0, 1);
		if(key.empty())
		{
			why = "--option '" + l + "': KEY=VALUE with an [Extras] option";
			return false;
		}
		const Vp64Opt::Id id = Vp64Opt::FindKey(key.c_str(), key.size());
		if(id == Vp64Opt::kCount || strcmp(Vp64Opt::Definition(id).section, "Extras") != 0)
		{
			why = "--option '" + l + "': " + key + " is not an [Extras] option";
			return false;
		}
		text += "[Extras]\n" + l + "\n";
	}
	struct Warn
	{
		std::string first;
		unsigned n = 0;
	} w;
	Vp64Opt::Parse(text.data(), text.size(), out,
	               [](void* c, const char* line) {
		               Warn* p = static_cast<Warn*>(c);
		               if(p->n++ == 0)
			               p->first = line;
	               },
	               &w);
	if(w.n)
	{
		why = "--option: " + w.first;
		return false;
	}
	return true;
}

namespace sceneshot {

using namespace srvrender;
namespace sb = srvrender::scenebundle;

const char* ExitName(int code)
{
	switch(code)
	{
	case kOk: return "OK";
	case kUsage: return "Usage";
	case kNoCamera: return "NoCamera";
	case kFrameOutside: return "FrameOutside";
	case kSettleTimeout: return "SettleTimeout";
	case kResidencyStopped: return "ResidencyStopped";
	case kNoDevice: return "NoDevice";
	case kReplayFailed: return "ReplayFailed";
	case kInstall: return "Install";
	case kDiffers: return "Differs";
	case kWriteFailed: return "WriteFailed";
	case kNoDump: return "NoDump";
	case kUnsupported: return "Unsupported";
	default: break;
	}
	if(code >= 21 && code <= 42)
		return sb::RefusalName((sb::Refusal)(code - 20));
	return "?";
}

namespace {

// ---- output ---------------------------------------------------------------------------------------------------------------
LineFn g_line = nullptr;
void* g_lineCtx = nullptr;

void Emit(const char* fmt, ...)
{
	char b[2048];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	if(g_line)
		g_line(g_lineCtx, b);
}

void RenderLine(const char* line)
{
	if(g_line)
		g_line(g_lineCtx, line);   // "render: ..." (srv_render_common.cpp)
}

int Fail(int code, const std::string& why)
{
	Emit("sceneshot: FAILED %d %s: %s", code, ExitName(code), why.c_str());
	return code;
}

std::wstring W(const std::string& s) { return sb::Widen(s); }
std::string N(const std::wstring& s) { return sb::Narrow(s); }

std::string Fwd(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	return s;
}

bool FileExists(const std::wstring& p)
{
	const DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p)
{
	const DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool WriteFileBytes(const std::wstring& path, const void* p, size_t n)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"wb") || !f)
		return false;
	const bool ok = n == 0 || fwrite(p, 1, n, f) == n;
	return (fclose(f) == 0) && ok;
}

std::string JsonEscape(const std::string& s)
{
	std::string o;
	for(unsigned char c : s)
	{
		if(c == '"' || c == '\\')
		{
			o += '\\';
			o += (char)c;
		}
		else if(c < 0x20)
		{
			char b[8];
			sprintf_s(b, "\\u%04x", c);
			o += b;
		}
		else
			o += (char)c;
	}
	return o;
}

// ---- ticket 0614: where the game's and the mods' files come from ----------------------------------------------------------
// A live run on a pool instance rewrites that instance's mods database while it starts (0 activated, then 3): a player
// reading the recording instance's MODS folder and database then drew without the mods (".fxsxml not found", exit 8;
// offline-scene-harness.md 34). Two rules: (1) a source path inside a pool instance (the nearest folder at or above it
// that holds instance.json) whose claim file exists is refused, the pool's own rule (tools/harness/pool.py: a claim is the
// file <pool dir>\claims\<name>.json; pool dir C5_POOL_DIR, else <C5_DEFAULT_INSTANCES_ROOT, else %USERPROFILE%\
// c5-instances>\_pool; `pool.py holder PATH` answers the same question); (2) --asset-root DIR: a frozen copy of the
// activated mods' folders and the mods database with a never-launched install (assetroot.py make), checked against its
// asset-root.txt before anything is read. Neither falls back to another source.
std::wstring EnvW(const wchar_t* name)
{
	wchar_t b[4096];
	const DWORD n = GetEnvironmentVariableW(name, b, 4096);
	return n > 0 && n < 4096 ? std::wstring(b, n) : std::wstring();
}

std::wstring FullPathW(const std::wstring& p)
{
	std::wstring s = p;
	std::replace(s.begin(), s.end(), L'/', L'\\');
	wchar_t b[4096];
	const DWORD n = GetFullPathNameW(s.c_str(), 4096, b, nullptr);
	if(n > 0 && n < 4096)
		s.assign(b, n);
	while(s.size() > 3 && s.back() == L'\\')
		s.pop_back();
	return s;
}

std::wstring PoolDirW()
{
	std::wstring p = EnvW(L"C5_POOL_DIR");
	if(!p.empty())
		return p;
	std::wstring root = EnvW(L"C5_DEFAULT_INSTANCES_ROOT");
	if(root.empty())
	{
		const std::wstring home = EnvW(L"USERPROFILE");
		if(home.empty())
			return std::wstring();
		root = home + L"\\c5-instances";
	}
	return root + L"\\_pool";
}

bool ReadSmallFile(const std::wstring& p, std::string& out, size_t cap = 1 << 20)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, p.c_str(), L"rb") || !f)
		return false;
	out.clear();
	char b[4096];
	size_t n;
	while((n = fread(b, 1, sizeof(b), f)) > 0 && out.size() < cap)
		out.append(b, n);
	fclose(f);
	return out.size() < cap;
}

// The string value of "key" in a flat JSON object (the claim file's owner, ticket, expires_at); "" when absent
std::string JsonStr(const std::string& t, const char* key)
{
	const std::string k = std::string("\"") + key + "\"";
	size_t i = t.find(k);
	if(i == std::string::npos)
		return std::string();
	i = t.find_first_not_of(" \t\r\n", i + k.size());
	if(i == std::string::npos || t[i] != ':')
		return std::string();
	i = t.find_first_not_of(" \t\r\n", i + 1);
	if(i == std::string::npos || t[i] != '"')
		return std::string();
	std::string v;
	for(++i; i < t.size() && t[i] != '"'; ++i)
		v += t[i] == '\\' && i + 1 < t.size() ? t[++i] : t[i];
	return v;
}

// The pool instance `path` lies in: the nearest folder at or above it that holds instance.json ("" when none)
std::wstring InstanceOf(const std::wstring& path)
{
	std::wstring d = FullPathW(path);
	for(;;)
	{
		if(FileExists(d + L"\\instance.json"))
			return d;
		const size_t k = d.find_last_of(L'\\');
		if(k == std::wstring::npos)
			return std::wstring();
		d.resize(k);
	}
}

// Rule 1: "" when `path` may be read; else why not. `what` names the source in the line.
std::string ClaimedWhy(const std::wstring& path, const char* what)
{
	const std::wstring inst = InstanceOf(path);
	if(inst.empty())
		return std::string();
	std::wstring name = inst.substr(inst.find_last_of(L'\\') + 1);
	const std::wstring pool = PoolDirW();
	const std::string head = std::string(what) + " '" + Fwd(N(path)) + "' lies in pool instance " + N(name);
	if(pool.empty())
		return head + ", and no pool folder is known (C5_POOL_DIR, USERPROFILE unset): whether a live run holds it cannot be told";
	std::wstring lower = name;
	CharLowerBuffW(&lower[0], (DWORD)lower.size());
	const std::wstring claim = pool + L"\\claims\\" + lower + L".json";
	if(!FileExists(claim))
		return std::string();
	std::string text, owner, ticket, until;
	if(ReadSmallFile(claim, text))
	{
		owner = JsonStr(text, "owner");
		ticket = JsonStr(text, "ticket");
		until = JsonStr(text, "expires_at");
	}
	return head + ", which the pool shows claimed (" + Fwd(N(claim)) + ": owner " + (owner.empty() ? "?" : owner) + (ticket.empty() ? "" : ", ticket " + ticket) +
	       (until.empty() ? "" : ", until " + until) +
	       "): a live run may be writing its MODS folder and mods database (ticket 0614); give the player --asset-root DIR "
	       "(server/tools/sceneshot/assetroot.py make) or an unclaimed source";
}

// Rule 2: the asset root (assetroot.py writes it; asset-root.txt is KEY=VALUE lines, UTF-8)
struct AssetRoot
{
	std::wstring dir, install, mods, db;
	std::string name, madeFrom, madeAt;
	int activated = -1;
	size_t files = 0;
};

const wchar_t kAssetCiv[] = L"\\Documents\\My Games\\Sid Meier's Civilization 5";

// The mods tree's listing hash: SHA-256 over the sorted lines "<path relative to MODS, '/' separators>\t<size>\n" (UTF-8,
// sorted by bytes; assetroot.py's tree_hash() is the same function)
bool TreeHash(const std::wstring& root, std::string& hex, size_t& files, std::string& why)
{
	std::vector<std::string> lines;
	std::vector<std::wstring> todo{L""};
	while(!todo.empty())
	{
		const std::wstring rel = todo.back();
		todo.pop_back();
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((root + (rel.empty() ? L"" : L"\\" + rel) + L"\\*").c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return why = "cannot list '" + Fwd(N(root + L"\\" + rel)) + "'", false;
		do
		{
			const std::wstring n = fd.cFileName;
			if(n == L"." || n == L"..")
				continue;
			const std::wstring r = rel.empty() ? n : rel + L"\\" + n;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				todo.push_back(r);
			else
			{
				const unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
				lines.push_back(Fwd(N(r)) + "\t" + std::to_string(size) + "\n");
			}
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	std::sort(lines.begin(), lines.end());
	std::string all;
	for(const std::string& l : lines)
		all += l;
	hex = sb::Sha256Hex(all.data(), all.size());
	files = lines.size();
	return !hex.empty() || (why = "no SHA-256 (bcrypt)", false);
}

// Reads and checks the asset root at `arg`: 0, or the Fail() code with its line
int LoadAssetRoot(const std::string& arg, const std::string& from, AssetRoot& r)
{
	const int64_t q0 = Qpc();
	r.dir = FullPathW(W(arg));
	const std::string where = from + " '" + Fwd(N(r.dir)) + "'";
	const std::wstring mf = r.dir + L"\\asset-root.txt";
	std::string text;
	if(!DirExists(r.dir))
		return Fail(kInstall, where + ": the asset root is missing");
	if(!ReadSmallFile(mf, text))
		return Fail(kInstall, where + ": no asset-root.txt (an asset root is made by server/tools/sceneshot/assetroot.py make)");
	std::map<std::string, std::string> kv;
	size_t at = 0;
	while(at < text.size())
	{
		size_t e = text.find('\n', at);
		if(e == std::string::npos)
			e = text.size();
		std::string l = text.substr(at, e - at);
		at = e + 1;
		if(!l.empty() && l.back() == '\r')
			l.pop_back();
		if(l.empty() || l[0] == '#')
			continue;
		const size_t eq = l.find('=');
		if(eq == std::string::npos)
			return Fail(kInstall, where + ": asset-root.txt line '" + l + "' is not KEY=VALUE");
		kv[l.substr(0, eq)] = l.substr(eq + 1);
	}
	for(const char* k : {"format", "name", "made_from", "made_at", "install", "mods_db_sha256", "mods_tree_sha256", "mods_files", "activated"})
		if(!kv.count(k) || kv[k].empty())
			return Fail(kInstall, where + ": asset-root.txt has no " + k);
	if(kv["format"] != "1")
		return Fail(kInstall, where + ": asset-root.txt format " + kv["format"] + " (this player reads format 1)");
	r.name = kv["name"];
	r.madeFrom = kv["made_from"];
	r.madeAt = kv["made_at"];
	r.activated = atoi(kv["activated"].c_str());
	r.install = FullPathW(W(kv["install"]));
	r.mods = r.dir + kAssetCiv + L"\\MODS";
	r.db = r.dir + kAssetCiv + L"\\cache\\Civ5ModsDatabase.db";
	const std::string inRoot = ClaimedWhy(r.dir, "the asset root");
	if(!inRoot.empty())
		return Fail(kInstall, inRoot);
	const std::string inInstall = ClaimedWhy(r.install, "the asset root's install");
	if(!inInstall.empty())
		return Fail(kInstall, inInstall);
	if(!DirExists(r.install))
		return Fail(kInstall, where + ": its install '" + Fwd(N(r.install)) + "' is missing");
	if(!DirExists(r.mods))
		return Fail(kInstall, where + ": its MODS folder is missing");
	if(FileExists(r.db + L"-journal") || FileExists(r.db + L"-wal"))
		return Fail(kInstall, where + ": a -journal or -wal file beside its mods database: something writes the asset root");
	std::string hex;
	uint64_t bytes = 0;
	if(!FileExists(r.db) || !sb::Sha256FileHex(r.db, &hex, &bytes).ok())
		return Fail(kInstall, where + ": its mods database cannot be read");
	if(hex != kv["mods_db_sha256"])
		return Fail(kInstall, where + ": its mods database has SHA-256 " + hex + ", asset-root.txt says " + kv["mods_db_sha256"] +
		                          ": the asset root changed since it was made (make it again)");
	size_t files = 0;
	std::string why;
	if(!TreeHash(r.mods, hex, files, why))
		return Fail(kInstall, where + ": " + why);
	if(hex != kv["mods_tree_sha256"] || std::to_string(files) != kv["mods_files"])
		return Fail(kInstall, where + ": its MODS folder lists " + std::to_string(files) + " files with listing hash " + hex + ", asset-root.txt says " +
		                          kv["mods_files"] + " files, " + kv["mods_tree_sha256"] + ": the asset root changed since it was made (make it again)");
	r.files = files;
	Emit("sceneshot: asset root %s (%s): '%s', made from %s at %s, install %s, %zu files under MODS, %d activated mods; asset-root.txt checked in %.0f ms",
	     Fwd(N(r.dir)).c_str(), from.c_str(), r.name.c_str(), r.madeFrom.c_str(), r.madeAt.c_str(), Fwd(N(r.install)).c_str(), r.files, r.activated,
	     QpcToMs(Qpc() - q0));
	return 0;
}

// ---- the command line -------------------------------------------------------------------------------------------------------
struct Args
{
	std::string bundle, out, camera, adapter = "bundle", install, exe;
	std::string mods;                          // issue 0364: --mods DIR, the mods' folder of the decals (else render_decal_mods)
	bool haveFrame = false, haveSpan = false, dumped = false;
	int64_t frame = 0, spanA = 0, spanB = 0;
	bool compare = false, diff = false, noPictures = false, allowOtherExe = false, allowOtherAssets = false;
	double settleMs = 120000, bakeRate = 100000, bakeBudgetMs = 50;
	int bakeBurst = 4096;
	uint64_t tolPixels = 0;                    // --tolerance PIXELS:DELTA: colour may differ in at most PIXELS pixels per frame,
	int tolDelta = 0;                          // each by at most DELTA in any channel; depth always byte-equal (default 0:0)
	std::map<std::string, std::string> sets;   // --set KEY=VALUE: a render_config value replaced (diagnostics: not P1)
	bool unitsNoHistory = false;               // issue 0440: --units-no-history (diagnostic: units_history.bin not replayed)
	std::vector<std::string> options;          // ticket 4938: --option KEY=VALUE, an [Extras] line of VP64Options.ini
	std::string assetRoot;                     // ticket 0614: --asset-root DIR, else C5_SCENE_ASSET_ROOT (see AssetRoot below)
	std::string assetRootFrom;                 // "--asset-root" or "C5_SCENE_ASSET_ROOT"
};

bool ParseI64(const std::string& s, int64_t& out)
{
	if(s.empty())
		return false;
	char* end = nullptr;
	const long long v = strtoll(s.c_str(), &end, 10);
	if(!end || *end)
		return false;
	out = v;
	return true;
}

bool ParseNum(const std::string& s, double lo, double hi, double& out)
{
	if(s.empty())
		return false;
	char* end = nullptr;
	const double v = strtod(s.c_str(), &end);
	if(!end || *end || !(v >= lo && v <= hi))
		return false;
	out = v;
	return true;
}

bool ParseArgs(const std::vector<std::string>& v, Args& a, std::string& why)
{
	int selectors = 0;
	for(size_t i = 0; i < v.size(); ++i)
	{
		const std::string& k = v[i];
		auto val = [&](std::string& dst) {
			if(i + 1 >= v.size())
			{
				why = k + " needs a value";
				return false;
			}
			dst = v[++i];
			return true;
		};
		std::string s;
		if(k == "--bundle")
		{
			if(!val(a.bundle))
				return false;
		}
		else if(k == "--out")
		{
			if(!val(a.out))
				return false;
		}
		else if(k == "--camera")
		{
			if(!val(a.camera))
				return false;
			selectors++;
			if(a.camera.empty() || a.camera.find_first_of("/\\:.") != std::string::npos)
			{
				why = "--camera '" + a.camera + "': a name without / \\ : or .";
				return false;
			}
		}
		else if(k == "--frame")
		{
			if(!val(s))
				return false;
			if(!ParseI64(s, a.frame))
			{
				why = "--frame '" + s + "': an integer frame id";
				return false;
			}
			a.haveFrame = true;
			selectors++;
		}
		else if(k == "--span")
		{
			if(!val(s))
				return false;
			const size_t c = s.find(':');
			if(c == std::string::npos || !ParseI64(s.substr(0, c), a.spanA) || !ParseI64(s.substr(c + 1), a.spanB) || a.spanA > a.spanB)
			{
				why = "--span '" + s + "': FIRST:LAST, two frame ids, FIRST <= LAST";
				return false;
			}
			a.haveSpan = true;
			selectors++;
		}
		else if(k == "--dumped")
		{
			a.dumped = true;
			selectors++;
		}
		else if(k == "--adapter")
		{
			if(!val(a.adapter))
				return false;
		}
		else if(k == "--install")
		{
			if(!val(a.install))
				return false;
		}
		else if(k == "--exe")
		{
			if(!val(a.exe))
				return false;
		}
		else if(k == "--mods")   // issue 0364
		{
			if(!val(a.mods))
				return false;
		}
		else if(k == "--asset-root")   // ticket 0614
		{
			if(!val(a.assetRoot))
				return false;
			if(a.assetRoot.empty())
			{
				why = "--asset-root needs a folder";
				return false;
			}
			a.assetRootFrom = "--asset-root";
		}
		else if(k == "--settle-ms" || k == "--bake-rate" || k == "--bake-budget-ms" || k == "--bake-burst")
		{
			if(!val(s))
				return false;
			double d = 0;
			const bool ok = k == "--settle-ms" ? ParseNum(s, 0, 3600000, d)
			              : k == "--bake-rate" ? ParseNum(s, 0.001, 1000000, d)
			              : k == "--bake-budget-ms" ? ParseNum(s, 0.001, 1000, d)
			                                        : ParseNum(s, 0, 4096, d) && d == std::floor(d);
			if(!ok)
			{
				why = k + " '" + s + "': out of range (--settle-ms 0..3600000, --bake-rate above 0..1000000, --bake-budget-ms above 0..1000, --bake-burst 0..4096)";
				return false;
			}
			if(k == "--settle-ms")
				a.settleMs = d;
			else if(k == "--bake-rate")
				a.bakeRate = d;
			else if(k == "--bake-budget-ms")
				a.bakeBudgetMs = d;
			else
				a.bakeBurst = (int)d;
		}
		else if(k == "--compare")
			a.compare = true;
		else if(k == "--diff")
			a.diff = true;
		else if(k == "--no-pictures")
			a.noPictures = true;
		else if(k == "--allow-other-exe")
			a.allowOtherExe = true;
		else if(k == "--tolerance")
		{
			if(!val(s))
				return false;
			const size_t c = s.find(':');
			int64_t px = -1, dl = -1;
			if(c == std::string::npos || !ParseI64(s.substr(0, c), px) || !ParseI64(s.substr(c + 1), dl) || px < 0 || dl < 0 || dl > 255)
			{
				why = "--tolerance '" + s + "': PIXELS:DELTA (colour pixels per frame, largest channel difference 0..255)";
				return false;
			}
			a.tolPixels = (uint64_t)px;
			a.tolDelta = (int)dl;
		}
		else if(k == "--set")
		{
			if(!val(s))
				return false;
			const size_t eq = s.find('=');
			if(eq == std::string::npos || eq == 0 || s.compare(0, 7, "render_") != 0)
			{
				why = "--set '" + s + "': KEY=VALUE with a render_* key";
				return false;
			}
			a.sets[s.substr(0, eq)] = s.substr(eq + 1);
		}
		else if(k == "--option")   // ticket 4938: an [Extras] line, as if VP64Options.ini held it
		{
			if(!val(s))
				return false;
			a.options.push_back(s);
			Vp64Opt::Options check;
			if(!ApplyOptions(a.options, check, why))
				return false;
		}
		else if(k == "--allow-other-assets")
			a.allowOtherAssets = true;
		else if(k == "--units-no-history")   // issue 0440: a diagnostic (not P1): the controller starts at the span's first frame
			a.unitsNoHistory = true;
		else
		{
			why = "unknown argument '" + k + "'";
			return false;
		}
	}
	if(a.bundle.empty())
	{
		why = "--bundle DIR is required";
		return false;
	}
	if(a.out.empty())
	{
		why = "--out DIR is required";
		return false;
	}
	if(selectors != 1)
	{
		why = "exactly one of --frame ID, --span FIRST:LAST, --camera NAME, --dumped is required";
		return false;
	}
	if(a.diff && !a.compare)
	{
		why = "--diff needs --compare";
		return false;
	}
	if(a.diff && a.noPictures)
	{
		why = "--diff writes a picture: not with --no-pictures";
		return false;
	}
	// ticket 0614: the asset root names the install and the mods' folders itself; the environment's C5_SCENE_ASSET_ROOT
	// counts as --asset-root (never ignored: a conflict with --install or --mods is refused, not settled silently)
	if(a.assetRoot.empty())
	{
		wchar_t env[4096];
		const DWORD n = GetEnvironmentVariableW(L"C5_SCENE_ASSET_ROOT", env, 4096);
		if(n > 0 && n < 4096)
		{
			a.assetRoot = sb::Narrow(std::wstring(env, n));
			a.assetRootFrom = "C5_SCENE_ASSET_ROOT";
		}
	}
	if(!a.assetRoot.empty() && (!a.install.empty() || !a.mods.empty()))
	{
		why = a.assetRootFrom + " '" + a.assetRoot + "' names the install and the mods' folders: not with " + (!a.install.empty() ? "--install" : "--mods") +
		      (a.assetRootFrom == "C5_SCENE_ASSET_ROOT" ? " (unset C5_SCENE_ASSET_ROOT to name them yourself)" : "");
		return false;
	}
	return true;
}

// ---- the adapter ------------------------------------------------------------------------------------------------------------
std::string LuidHex(const LUID& l)
{
	char b[32];
	sprintf_s(b, "%08lX%08lX", (unsigned long)l.HighPart, (unsigned long)l.LowPart);
	return b;
}

std::string NormLuid(const std::string& s)
{
	std::string o;
	for(char c : s)
		if(isxdigit((unsigned char)c))
			o += (char)toupper((unsigned char)c);
	while(o.size() < 16 && !o.empty())
		o = "0" + o;
	return o;
}

struct Device
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	std::string description, luid;
	bool warp = false;
	~Device()
	{
		if(ctx)
		{
			ctx->ClearState();
			ctx->Flush();
		}
		SafeRelease(ctx);
		SafeRelease(dev);
	}
};

void DescribeDevice(Device& d)
{
	IDXGIDevice* xd = nullptr;
	IDXGIAdapter* ad = nullptr;
	if(SUCCEEDED(d.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&ad)))
	{
		DXGI_ADAPTER_DESC desc;
		if(SUCCEEDED(ad->GetDesc(&desc)))
		{
			d.description = N(desc.Description);
			d.luid = LuidHex(desc.AdapterLuid);
		}
	}
	SafeRelease(ad);
	SafeRelease(xd);
}

// Issue 0336 (2026-11-16): the device is created with D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS. With
// flags 0 the driver's internal threading made the ocean draw (the EXE's ocean PS) differ by 1 in one channel at about one
// pixel per frame from draw to draw, with byte-identical inputs in one process (RTX 4080, driver 32.0.16.1088: 20-26 of 96
// redraws 500 ms apart); with the flag 0 of 96 / 160 / 160 (research/05-graphics-composition/water-system.md section 15).
// It changes only how the driver schedules its own work, not what the player draws.
const UINT kDeviceFlags = D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;

int MakeDevice(const Args& a, const sb::Manifest& m, Device& d)
{
	const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	D3D_FEATURE_LEVEL fl;
	if(a.adapter == "warp")
	{
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, kDeviceFlags, fls, 2, D3D11_SDK_VERSION, &d.dev, &fl, &d.ctx);
		if(FAILED(hr))
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, kDeviceFlags, fls + 1, 1, D3D11_SDK_VERSION, &d.dev, &fl, &d.ctx);
		if(FAILED(hr))
		{
			char b[96];
			sprintf_s(b, "WARP device failed 0x%08lX", hr);
			return Fail(kNoDevice, b);
		}
		d.warp = true;
		DescribeDevice(d);
		return kOk;
	}
	IDXGIFactory1* f = nullptr;
	if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)))
		return Fail(kNoDevice, "CreateDXGIFactory1 failed");
	const std::string wantLuid = a.adapter == "bundle" ? NormLuid(m.adapterLuid) : a.adapter == "hw" ? std::string() : NormLuid(a.adapter);
	if(a.adapter != "bundle" && a.adapter != "hw" && wantLuid.size() != 16)
	{
		f->Release();
		return Fail(kUsage, "--adapter '" + a.adapter + "': bundle, hw, warp or a LUID of 16 hex digits");
	}
	IDXGIAdapter1* pick = nullptr;
	IDXGIAdapter1* byDesc = nullptr;
	SIZE_T bestVram = 0;
	std::string seen;
	for(UINT i = 0;; ++i)
	{
		IDXGIAdapter1* ad = nullptr;
		if(f->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 desc;
		if(FAILED(ad->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			ad->Release();
			continue;
		}
		const std::string dn = N(desc.Description), dl = LuidHex(desc.AdapterLuid);
		seen += (seen.empty() ? "" : ", ") + dn + " (" + dl + ")";
		if(!wantLuid.empty() && dl == wantLuid && !pick)
		{
			pick = ad;
			continue;
		}
		if(a.adapter == "bundle" && !byDesc && !m.adapterDescription.empty() && dn == m.adapterDescription)
		{
			byDesc = ad;
			continue;
		}
		if(a.adapter == "hw" && desc.DedicatedVideoMemory >= bestVram)   // as the host: the largest
		{
			bestVram = desc.DedicatedVideoMemory;
			SafeRelease(pick);
			pick = ad;
			continue;
		}
		ad->Release();
	}
	f->Release();
	if(!pick && byDesc)
	{
		pick = byDesc;   // the bundle's adapter by its description (a LUID changes at every start of Windows)
		byDesc = nullptr;
	}
	SafeRelease(byDesc);
	if(!pick)
		return Fail(kNoDevice, a.adapter == "bundle" ? "the bundle's adapter '" + m.adapterDescription + "' (LUID " + m.adapterLuid +
		                                                   ") is not on this machine (adapters: " + (seen.empty() ? "none" : seen) +
		                                                   "); --adapter hw|warp|LUID draws on another (not P1)"
		                                             : "no hardware adapter " + a.adapter + " (adapters: " + (seen.empty() ? "none" : seen) + ")");
	const HRESULT hr = D3D11CreateDevice(pick, D3D_DRIVER_TYPE_UNKNOWN, nullptr, kDeviceFlags, fls, 2, D3D11_SDK_VERSION, &d.dev, &fl, &d.ctx);
	pick->Release();
	if(FAILED(hr))
	{
		char b[96];
		sprintf_s(b, "D3D11CreateDevice failed 0x%08lX", hr);
		return Fail(kNoDevice, b);
	}
	DescribeDevice(d);
	return kOk;
}

// ---- the targets (the sinks' formats: srv_v3_sink.cpp, shell/frame/ring_sink.cpp) and their readback --------------------------
struct Targets
{
	ID3D11Texture2D* colour = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* depth = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	uint32_t w = 0, h = 0;
	void release()
	{
		SafeRelease(dsv);
		SafeRelease(depth);
		SafeRelease(rtv);
		SafeRelease(colour);
		w = h = 0;
	}
	~Targets() { release(); }
	bool ensure(ID3D11Device* dev, uint32_t W_, uint32_t H_, std::string& why)
	{
		if(colour && w == W_ && h == H_)
			return true;
		release();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = W_;
		td.Height = H_;
		td.MipLevels = td.ArraySize = 1;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		HRESULT hr = dev->CreateTexture2D(&td, nullptr, &colour);
		if(SUCCEEDED(hr))
			hr = dev->CreateRenderTargetView(colour, nullptr, &rtv);
		td.Format = DXGI_FORMAT_R32_TYPELESS;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if(SUCCEEDED(hr))
			hr = dev->CreateTexture2D(&td, nullptr, &depth);
		D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
		dd.Format = DXGI_FORMAT_D32_FLOAT;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if(SUCCEEDED(hr))
			hr = dev->CreateDepthStencilView(depth, &dd, &dsv);
		if(FAILED(hr))
		{
			char b[128];
			sprintf_s(b, "targets %ux%u failed 0x%08lX", W_, H_, hr);
			why = b;
			release();
			return false;
		}
		w = W_;
		h = H_;
		return true;
	}
};

// Tight rows of 4 bytes a texel (colour BGRA as stored; depth the float32 bits as stored).
bool ReadBack(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* t, std::vector<uint8_t>& out, std::string& why)
{
	D3D11_TEXTURE2D_DESC td;
	t->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	td.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	HRESULT hr = dev->CreateTexture2D(&td, nullptr, &st);
	if(FAILED(hr))
	{
		char b[96];
		sprintf_s(b, "staging texture failed 0x%08lX", hr);
		why = b;
		return false;
	}
	ctx->CopyResource(st, t);
	D3D11_MAPPED_SUBRESOURCE m;
	hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
	if(FAILED(hr))
	{
		st->Release();
		char b[96];
		sprintf_s(b, "Map failed 0x%08lX", hr);
		why = b;
		return false;
	}
	out.resize((size_t)td.Width * td.Height * 4);
	for(UINT y = 0; y < td.Height; ++y)
		memcpy(&out[(size_t)y * td.Width * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)td.Width * 4);
	ctx->Unmap(st, 0);
	st->Release();
	return true;
}

// ---- PNG (WIC) ------------------------------------------------------------------------------------------------------------
bool WritePng(const std::wstring& path, uint32_t w, uint32_t h, const uint8_t* bgra, std::string& why)
{
	IWICImagingFactory* f = nullptr;
	IWICStream* s = nullptr;
	IWICBitmapEncoder* e = nullptr;
	IWICBitmapFrameEncode* fr = nullptr;
	IPropertyBag2* pb = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
	if(SUCCEEDED(hr))
		hr = f->CreateStream(&s);
	if(SUCCEEDED(hr))
		hr = s->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
	if(SUCCEEDED(hr))
		hr = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e);
	if(SUCCEEDED(hr))
		hr = e->Initialize(s, WICBitmapEncoderNoCache);
	if(SUCCEEDED(hr))
		hr = e->CreateNewFrame(&fr, &pb);
	if(SUCCEEDED(hr))
		hr = fr->Initialize(pb);
	if(SUCCEEDED(hr))
		hr = fr->SetSize(w, h);
	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
	if(SUCCEEDED(hr))
		hr = fr->SetPixelFormat(&fmt);
	if(SUCCEEDED(hr) && fmt != GUID_WICPixelFormat32bppBGRA)
		hr = E_FAIL;
	if(SUCCEEDED(hr))
		hr = fr->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(bgra));
	if(SUCCEEDED(hr))
		hr = fr->Commit();
	if(SUCCEEDED(hr))
		hr = e->Commit();
	SafeRelease(pb);
	SafeRelease(fr);
	SafeRelease(e);
	SafeRelease(s);
	SafeRelease(f);
	if(FAILED(hr))
	{
		char b[64];
		sprintf_s(b, "PNG encoder 0x%08lX", hr);
		why = b;
		return false;
	}
	return true;
}

// ---- the comparison with the server's own dumps (contract 2.8) ----------------------------------------------------------------
struct ColourDiff
{
	uint64_t pixels = 0, differing = 0, alphaDiffering = 0;
	int maxDelta = 0;              // the largest channel difference of any pixel
	int medianAll = 0, p95All = 0;  // of the per-pixel largest channel difference, over every pixel
	int medianDiff = 0, p95Diff = 0;   // the same over the differing pixels only (0 when none)
	std::vector<size_t> first;          // the first differing pixels' indices (at most 8), for the DIFF lines
	bool equal() const { return differing == 0; }
};
struct DepthDiff
{
	uint64_t values = 0, differing = 0, coverage = 0;   // coverage: one side is the clear value 1.0, the other not
	double maxAbs = 0;                                  // over values finite on both sides
	bool equal() const { return differing == 0; }
};

int Percentile(const uint64_t (&hist)[256], uint64_t n, double q)
{
	if(!n)
		return 0;
	const uint64_t want = (uint64_t)std::ceil(q * (double)n);
	uint64_t acc = 0;
	for(int i = 0; i < 256; ++i)
	{
		acc += hist[i];
		if(acc >= want && acc)
			return i;
	}
	return 255;
}

ColourDiff CompareColour(const uint8_t* a, const uint8_t* b, size_t pixels, std::vector<uint8_t>* delta)
{
	ColourDiff d;
	d.pixels = pixels;
	uint64_t all[256] = {}, dif[256] = {};
	if(delta)
		delta->assign(pixels, 0);
	for(size_t i = 0; i < pixels; ++i)
	{
		int m = 0;
		for(int c = 0; c < 4; ++c)
			m = std::max(m, std::abs((int)a[4 * i + c] - (int)b[4 * i + c]));
		all[m]++;
		if(m)
		{
			d.differing++;
			dif[m]++;
			if(d.first.size() < 8)
				d.first.push_back(i);
			if(a[4 * i + 3] != b[4 * i + 3])
				d.alphaDiffering++;
		}
		d.maxDelta = std::max(d.maxDelta, m);
		if(delta)
			(*delta)[i] = (uint8_t)m;
	}
	d.medianAll = Percentile(all, pixels, 0.5);
	d.p95All = Percentile(all, pixels, 0.95);
	d.medianDiff = Percentile(dif, d.differing, 0.5);
	d.p95Diff = Percentile(dif, d.differing, 0.95);
	return d;
}

DepthDiff CompareDepth(const uint8_t* a, const uint8_t* b, size_t values)
{
	DepthDiff d;
	d.values = values;
	for(size_t i = 0; i < values; ++i)
	{
		if(!memcmp(a + 4 * i, b + 4 * i, 4))
			continue;
		d.differing++;
		float x, y;
		memcpy(&x, a + 4 * i, 4);
		memcpy(&y, b + 4 * i, 4);
		if((x == 1.0f) != (y == 1.0f))
			d.coverage++;
		if(std::isfinite(x) && std::isfinite(y))
			d.maxAbs = std::max(d.maxAbs, (double)std::fabs((double)x - (double)y));
	}
	return d;
}

// The difference image: equal pixels as the live picture's gray at a quarter, differing ones red by their difference.
std::vector<uint8_t> DiffImage(const uint8_t* live, const std::vector<uint8_t>& delta)
{
	std::vector<uint8_t> o(delta.size() * 4);
	for(size_t i = 0; i < delta.size(); ++i)
	{
		uint8_t* p = &o[4 * i];
		if(delta[i])
		{
			p[0] = 0;
			p[1] = 0;
			p[2] = (uint8_t)std::min(255, 96 + 8 * (int)delta[i]);
		}
		else
		{
			const int g = (live[4 * i] + live[4 * i + 1] + live[4 * i + 2]) / 12;
			p[0] = p[1] = p[2] = (uint8_t)g;
		}
		p[3] = 255;
	}
	return o;
}

// ---- the bundle's render_config -> SceneOptions (a port of srv_render_host.cpp's SceneOpts, which is file-local there) ----
bool ColorFrom(const std::string& v, bool fallback, c5r::terrain::ColorSource& out)
{
	namespace c5t = c5r::terrain;
	if(v == "caches" && !fallback)
		out = c5t::ColorSource::Caches;
	else if(v == "material")
		out = c5t::ColorSource::Material;
	else if(v == "constant")
		out = c5t::ColorSource::Constant;
	else if(v == "height")
		out = c5t::ColorSource::Height;
	else if(v == "normal")
		out = c5t::ColorSource::Normal;
	else
		return false;
	return true;
}

int OptionsFromBundle(const Args& a, const sb::Manifest& m, SceneOptions& o, std::wstring& install, std::wstring& exe, bool& fowParticles)
{
	std::map<std::string, std::string> rc = m.renderConfig;
	for(const auto& kv : a.sets)
	{
		Emit("sceneshot: NOTE: --set %s=%s (the bundle's: '%s'): not the recorded configuration, not P1", kv.first.c_str(), kv.second.c_str(),
		     rc.count(kv.first) ? rc[kv.first].c_str() : "");
		rc[kv.first] = kv.second;
	}
	// issue 0440: render_units_anim is recorded with render_units=1 only, and the server's parser refuses it without
	if(rc.count("render_units") && rc["render_units"] != "1")
		rc.erase("render_units_anim");
	// the contract's proposal wrote render_water as "full": the server.cfg value of the module is "1"
	auto w = rc.find("render_water");
	if(w != rc.end() && w->second == "full")
		w->second = "1";
	install = W(a.install.empty() ? m.install : a.install);
	std::replace(install.begin(), install.end(), L'/', L'\\');
	exe = a.exe.empty() ? install + L"\\CivilizationV_DX11.exe" : W(a.exe);
	std::replace(exe.begin(), exe.end(), L'/', L'\\');
	HostConfig c;
	std::string why;
	auto get = [&](const char* k) {
		auto it = rc.find(k);
		return it == rc.end() ? std::string() : it->second;
	};
	if(!ParseHostConfig(get, N(install), ".", c, why))
		return Fail(kUnsupported, "render_config refused by the server's own parser: " + why);
	if(c.tiles == "tap")
		return Fail(kUnsupported, "render_tiles=tap: the bake-cache tap is not in the bundle (contract 2.4)");
	const std::string water = get("render_water"), freeze = get("render_water_freeze");
	o.exe = exe;
	o.tilePlots = c.tilePlots;
	o.tileD4 = c.tileD4;
	ColorFrom(c.color, false, o.color);
	ColorFrom(c.fallback, true, o.fallback);
	o.constantRgba = c.constantRgba;
	o.pass = c.pass;
	o.tess = c.tess;
	o.water = c.water;
	o.waterMode = water == "placeholder" ? WaterSceneOptions::kPlaceholder : WaterSceneOptions::kFull;
	o.waterFreeze = freeze == "1";
	o.overlay = c.overlay;
	// ticket 0711 (J1, R8): the frame jobs as recorded (render_frame_jobs, render_test_frame_jobs_jitter; a bundle without
	// them: 0, every job inline at its join). The determinism proof replays the library with other values: the TEST
	// variables C5_SCENESHOT_FRAME_JOBS (0..8) and C5_SCENESHOT_FRAME_JOBS_JITTER (0 / 1) replace the bundle's; the picture is
	// byte-equal with any of them, so the run stays P1 (rule A with 0, 1 and 1 + jitter)
	{
		o.frameJobs = rc.count("render_frame_jobs") ? c.frameJobs : 0;
		o.frameJobsJitter = rc.count("render_test_frame_jobs_jitter") ? c.frameJobsJitter : false;
		char ev[16] = {};
		if(GetEnvironmentVariableA("C5_SCENESHOT_FRAME_JOBS", ev, sizeof(ev)) && ev[0])
		{
			int n = 0;
			std::string jwhy;
			if(!ParseFrameJobsKey(ev, FrameJobsLogicalCores(), n, jwhy))
				return Fail(kUsage, std::string("C5_SCENESHOT_FRAME_JOBS='") + ev + "': " + jwhy);
			o.frameJobs = n;
		}
		char ej[8] = {};
		if(GetEnvironmentVariableA("C5_SCENESHOT_FRAME_JOBS_JITTER", ej, sizeof(ej)) && ej[0])
		{
			if(std::string(ej) != "0" && std::string(ej) != "1")
				return Fail(kUsage, std::string("C5_SCENESHOT_FRAME_JOBS_JITTER='") + ej + "': 0 or 1");
			o.frameJobsJitter = ej[0] == '1';
		}
		Emit("sceneshot: render_config: frame jobs %d, jitter %d%s", o.frameJobs, o.frameJobsJitter ? 1 : 0,
		     GetEnvironmentVariableA("C5_SCENESHOT_FRAME_JOBS", ev, sizeof(ev)) || GetEnvironmentVariableA("C5_SCENESHOT_FRAME_JOBS_JITTER", ej, sizeof(ej))
		         ? " (TEST: C5_SCENESHOT_FRAME_JOBS / _JITTER)" : "");
		// ticket 0753 (J3, A6): record ahead as recorded (render_record_ahead; a bundle without it: 0); the TEST variable
		// C5_SCENESHOT_RECORD_AHEAD (auto / 0 / 1) replaces it, and the picture is byte-equal with any value (rule A with off,
		// on and on + jitter)
		o.recordAhead = rc.count("render_record_ahead") ? c.recordAhead : 0;
		char er[16] = {};
		const bool raTest = GetEnvironmentVariableA("C5_SCENESHOT_RECORD_AHEAD", er, sizeof(er)) && er[0];
		if(raTest)
		{
			const std::string s(er);
			if(s != "auto" && s != "0" && s != "1")
				return Fail(kUsage, "C5_SCENESHOT_RECORD_AHEAD='" + s + "': auto, 0 or 1");
			o.recordAhead = s == "auto" ? -1 : s == "1" ? 1 : 0;
		}
		// C5_SCENESHOT_RECORD_AHEAD_END_ONLY = 1: step 1 alone (the server's render_test_record_ahead_end_only)
		char ee[8] = {};
		if(GetEnvironmentVariableA("C5_SCENESHOT_RECORD_AHEAD_END_ONLY", ee, sizeof(ee)) && ee[0])
		{
			if(std::string(ee) != "0" && std::string(ee) != "1")
				return Fail(kUsage, std::string("C5_SCENESHOT_RECORD_AHEAD_END_ONLY='") + ee + "': 0 or 1");
			o.recordAheadEndOnly = ee[0] == '1';
		}
		Emit("sceneshot: render_config: record ahead %s%s%s", o.recordAhead < 0 ? "auto" : o.recordAhead ? "1" : "0",
		     o.recordAheadEndOnly ? ", the end only (TEST: C5_SCENESHOT_RECORD_AHEAD_END_ONLY)" : "",
		     raTest ? " (TEST: C5_SCENESHOT_RECORD_AHEAD)" : "");
	}
	// issue 4308: the shadow and the ice as recorded (a render_config without the keys: none); no install bound in the
	// player (its sources are the bundle's)
	o.shadow = get("render_shadow") == "1" ? 1 : 0;
	o.ice = get("render_ice") == "1" ? 1 : 0;
	o.shadowWaitMs = 0;
	o.iceWaitMs = 0;
	Emit("sceneshot: render_config: shadow %d, ice %d", o.shadow, o.ice);
	// issue 0364 (format version 3): the decals as recorded (render_decals "1": the decal pass in the residency, the set from
	// decals/load<NNNN>.dtap); their names and textures from the recorded mods' folder (render_decal_mods) or --mods, which
	// the scene reads through C5_DOCUMENTS (ModFolders) as the server does
	o.decals = get("render_decals") == "1" ? 1 : 0;
	o.decalWaitMs = 0;
	if(o.decals)
	{
		std::string mods = a.mods.empty() ? get("render_decal_mods") : a.mods;
		std::replace(mods.begin(), mods.end(), '/', '\\');
		static const char kTail[] = "\\My Games\\Sid Meier's Civilization 5\\MODS";
		const size_t tl = sizeof(kTail) - 1;
		if(!mods.empty())
		{
			if(mods.size() <= tl || _stricmp(mods.c_str() + mods.size() - tl, kTail) != 0)
				return Fail(kUsage, "the decals' mods folder '" + Fwd(mods) + "' does not end in " + Fwd(kTail) + " (--mods DIR)");
			if(!DirExists(W(mods)))
				return Fail(kInstall, "the decals' mods folder '" + Fwd(mods) + "' is missing (render_decal_mods of the recording; --mods DIR)");
			SetEnvironmentVariableW(L"C5_DOCUMENTS", W(mods.substr(0, mods.size() - tl)).c_str());
		}
		else   // never this machine's own Documents: a folder that does not exist (no mods' folders)
			SetEnvironmentVariableW(L"C5_DOCUMENTS", (install + L"\\no-documents-0364").c_str());
		Emit("sceneshot: render_config: decals 1, mods folder %s%s", mods.empty() ? "(none recorded: the install only)" : Fwd(mods).c_str(),
		     a.mods.empty() ? "" : " (--mods)");
	}
	else
		Emit("sceneshot: render_config: decals 0");
	// issue 0440 (format version 5): the server's unit scene as recorded (render_units, render_units_passes,
	// render_units_anim); its art from the install and the mods' folders the decals name (ModFolders through C5_DOCUMENTS),
	// its database from the bundle's units_db member (set in Run), its controller's state from units_history.bin
	o.units = get("render_units") == "1" ? 1 : 0;
	if(o.units)
	{
		o.unitsOpt.passes = get("render_units_passes") == "1";
		o.unitsOpt.anim = get("render_units_anim") != "0";
		o.unitsOpt.exe = exe;
		o.unitsOpt.install = install;
		o.unitsOpt.modsAuto = true;
		Emit("sceneshot: render_config: units 1 (passes %d, anim %d)", o.unitsOpt.passes ? 1 : 0, o.unitsOpt.anim ? 1 : 0);
		// ticket 5070: the position source and the ready hold as recorded (render_units_positions own / exe,
		// render_units_ready_hold_ms; absent, a bundle recorded before 4753: own and the default), as ParseHostConfig read
		// them above and server.exe's MakeLive and SceneOpts take them: the unit scene's UnitsOptions::positionsExe and the
		// mode writer's SceneOptions pair. Before 5070 the player left both at own, so a bundle recorded with exe replayed its
		// walks by the server's own timing instead of the EXE's kind-6 records: every fight after a walk and every clip after
		// it at another time (0742's overtaking bundle, unit-combat-server.md 18.47)
		o.unitsPositions = RenderUnitsPositionsOption();
		o.unitsReadyHoldMs = RenderUnitsReadyHoldMsOption();
		o.unitsOpt.positionsExe = o.unitsPositions == SceneOptions::kUnitsPositionsExe;
		Emit("sceneshot: render_config: units positions %s, ready_hold_ms %.0f", o.unitsOpt.positionsExe ? "exe" : "own",
		     o.unitsReadyHoldMs);
		// ticket 4737 (rule A): the fights' approach rule as recorded (render_test_units_charge_any "1": every RunCharge
		// attacker charges; absent, a bundle recorded before 4737, or "0": the 4775 rush-only rule); the tier gate is the
		// live trigger stream's (the player sends no triggers), printed only
		o.unitsOpt.chargeAny = get("render_test_units_charge_any") == "1";
		Emit("sceneshot: render_config: units charge_any %d, tier gate %s", o.unitsOpt.chargeAny ? 1 : 0,
		     rc.count("render_test_units_tier_gate") ? rc["render_test_units_tier_gate"].c_str() : "absent");
		// ticket 4735 (rule A): the turn before a fight whose figures stand as recorded (render_test_units_turn_first "1";
		// absent, a bundle recorded before 4735, or "0": no turn)
		o.unitsOpt.turnFirst = get("render_test_units_turn_first") == "1";
		Emit("sceneshot: render_config: units turn_first %d", o.unitsOpt.turnFirst ? 1 : 0);
		// ticket 4744 (rule A): a fight's standing figures as recorded (render_units_fight_drawn_figures "1": the drawn ones;
		// absent, a bundle recorded before 4744, or "0": every held figure)
		o.unitsOpt.fightDrawnFigures = get("render_units_fight_drawn_figures") == "1";
		Emit("sceneshot: render_config: units fight_drawn_figures %d", o.unitsOpt.fightDrawnFigures ? 1 : 0);
		// tickets 4745 + 4747 (rule A): the reform before a fight as recorded (render_test_units_reform_first "1"; absent, a
		// bundle recorded before 4745, or "0": no reform)
		o.unitsOpt.reformFirst = get("render_test_units_reform_first") == "1";
		Emit("sceneshot: render_config: units reform_first %d", o.unitsOpt.reformFirst ? 1 : 0);
		// ticket 4742 (rule A): the walk home inside the fight as recorded (render_test_units_walk_home_in_fight "1"; absent,
		// a bundle recorded before 4742, or "0": the walk after the fight's end)
		o.unitsOpt.walkHome = get("render_test_units_walk_home_in_fight") == "1";
		Emit("sceneshot: render_config: units walk_home %d", o.unitsOpt.walkHome ? 1 : 0);
		// ticket 4748 (rule A): a melee attack on a city as recorded (render_test_units_city_in_place "1"; absent, a bundle
		// recorded before 4748, or "0": the attacker approaches the city's centre first)
		o.unitsOpt.cityInPlace = get("render_test_units_city_in_place") == "1";
		Emit("sceneshot: render_config: units city_in_place %d", o.unitsOpt.cityInPlace ? 1 : 0);
		// ticket 4746 (rule A): which waiting figures enter combat ready as recorded (render_test_units_ready_after_move
		// "1"; absent, a bundle recorded before 4746, or "0": every waiting figure requests 1600)
		o.unitsOpt.readyAfterMove = get("render_test_units_ready_after_move") == "1";
		Emit("sceneshot: render_config: units ready_after_move %d", o.unitsOpt.readyAfterMove ? 1 : 0);
		// ticket 4750 (rule A): a non-answering defender's stance as recorded (render_test_units_defend_from_stance "1";
		// absent, a bundle recorded before 4750: 1280 at once)
		o.unitsOpt.defendFromStance = get("render_test_units_defend_from_stance") == "1";
		Emit("sceneshot: render_config: units defend_from_stance %d", o.unitsOpt.defendFromStance ? 1 : 0);
		// ticket 0796 (rule A; TEST ONLY): the Quick Combat rate as recorded (render_test_units_quick_combat "1": V2; absent, a
		// bundle recorded before 0796, or "0": every fight at real time). Said only when on (the output before 0796 otherwise)
		o.unitsOpt.quickCombat = get("render_test_units_quick_combat") == "1" ? 1 : 0;
		if(o.unitsOpt.quickCombat)
			Emit("sceneshot: render_config: units quick_combat %d", o.unitsOpt.quickCombat);
		// ticket 5050 (rule A): stock's phases 1 + 2 as recorded (render_test_units_reform_ends_at_arrival "1" /
		// render_test_units_attack_from_stance "1"; absent, a bundle recorded before 5050, or "0": the rules before 5050)
		o.unitsOpt.reformEndsAtArrival = get("render_test_units_reform_ends_at_arrival") == "1";
		o.unitsOpt.attackFromStance = get("render_test_units_attack_from_stance") == "1";
		Emit("sceneshot: render_config: units reform_ends_at_arrival %d, attack_from_stance %d",
		     o.unitsOpt.reformEndsAtArrival ? 1 : 0, o.unitsOpt.attackFromStance ? 1 : 0);
		// ticket 5057 (rule A): the rush ride-through and the type 2 defender wait as recorded (render_units_rush_ride_through /
		// render_units_defender_waits_first_landing "1"; absent, a bundle recorded before 5057, or "0": the rules before 5057)
		o.unitsOpt.rushRideThrough = get("render_units_rush_ride_through") == "1";
		o.unitsOpt.defenderWaitsFirstLanding = get("render_units_defender_waits_first_landing") == "1";
		Emit("sceneshot: render_config: units rush_ride_through %d, defender_waits_first_landing %d",
		     o.unitsOpt.rushRideThrough ? 1 : 0, o.unitsOpt.defenderWaitsFirstLanding ? 1 : 0);
		// ticket 5053 (rule A): the death by variant and the end after the dying as recorded (render_test_units_death_by_variant
		// / render_test_units_end_after_dying "1"; absent, a bundle recorded before 5053, or "0": the rules before 5053)
		o.unitsOpt.deathByVariant = get("render_test_units_death_by_variant") == "1";
		o.unitsOpt.endAfterDying = get("render_test_units_end_after_dying") == "1";
		Emit("sceneshot: render_config: units death_by_variant %d, end_after_dying %d",
		     o.unitsOpt.deathByVariant ? 1 : 0, o.unitsOpt.endAfterDying ? 1 : 0);
		// ticket 5059 (rule A): the victory rule as recorded (render_units_victory_word "1": only a member with the enable
		// word Victory; absent, a bundle recorded before 5059, or "0": the machine's 1800 alone, the rule before 5059)
		o.unitsOpt.victoryNeedsWord = get("render_units_victory_word") == "1";
		Emit("sceneshot: render_config: units victory_word %d", o.unitsOpt.victoryNeedsWord ? 1 : 0);
		// ticket 5066 (rule A): the fighter's walks around its fight as recorded (render_units_fight_after_walk "1"; absent, a
		// bundle recorded before 5066, or "0": the rules before 5066)
		o.unitsOpt.fightAfterWalk = get("render_units_fight_after_walk") == "1";
		Emit("sceneshot: render_config: units fight_after_walk %d", o.unitsOpt.fightAfterWalk ? 1 : 0);
		// ticket 5067 S1 (rule A): the type 2 phase 2 as recorded (render_units_type2_phase2 "1"; absent, a bundle recorded
		// before 5067, or "0": no phase 2 in a charge fight)
		o.unitsOpt.type2Phase2 = get("render_units_type2_phase2") == "1";
		// ticket 0743 (rule A): the melee reach as recorded (render_units_melee_reach "1"; absent, a bundle recorded
		// before 0743: off, the approach to MoveRadius of a doomed figure it was drawn with)
		o.unitsOpt.meleeReach = get("render_units_melee_reach") == "1";
		Emit("sceneshot: render_config: units melee_reach %d", o.unitsOpt.meleeReach ? 1 : 0);
		// ticket 5061 (rule A): the type 2 defender's step as recorded (render_units_defender_step "1"; absent, a bundle
		// recorded before 5061: off, the defender striking from where it stood)
		o.unitsOpt.defenderStep = get("render_units_defender_step") == "1";
		Emit("sceneshot: render_config: units defender_step %d", o.unitsOpt.defenderStep ? 1 : 0);
		Emit("sceneshot: render_config: units type2_phase2 %d", o.unitsOpt.type2Phase2 ? 1 : 0);
		// ticket 5065 (rule A): a walking unit's work and fortify after its walk as recorded (render_units_orders_after_walk
		// "1"; absent, a bundle recorded before 5065, or "0": the rule before 5065)
		o.unitsOpt.ordersAfterWalk = get("render_units_orders_after_walk") == "1";
		Emit("sceneshot: render_config: units orders_after_walk %d", o.unitsOpt.ordersAfterWalk ? 1 : 0);
		// ticket 5052 (rule A): the trigger stream's names as recorded (render_test_units_trigger_striker_weapon "1" /
		// render_test_units_trigger_terrain "1"; absent, a bundle recorded before 5052, or "0": the rules before 5052). They
		// change trigger names only, never a drawn pixel (sceneshot opens no trigger stream), so absent stays 0 although
		// the product's defaults are 1 (striker weapon since MC #10847, terrain since MC #11356, 5052 part D): no replayed
		// frame can differ, and every recorded bundle replays byte-identical (rule A).
		o.unitsOpt.triggerStrikerWeapon = get("render_test_units_trigger_striker_weapon") == "1";
		o.unitsOpt.triggerTerrain = get("render_test_units_trigger_terrain") == "1";
		Emit("sceneshot: render_config: units trigger_striker_weapon %d, trigger_terrain %d", o.unitsOpt.triggerStrikerWeapon ? 1 : 0,
		     o.unitsOpt.triggerTerrain ? 1 : 0);
		// ticket 5064 (rule A): a held projectile's pose and scale as recorded (render_units_projectile_pose "1"; absent, a
		// bundle recorded before 5064, or "0": the rule before 5064). The projectiles are the EXE's effects, never a pixel of
		// the server's, so absent stays 0 although the product's default is 1.
		o.unitsOpt.triggerProjectilePose = get("render_units_projectile_pose") == "1";
		Emit("sceneshot: render_config: units projectile_pose %d", o.unitsOpt.triggerProjectilePose ? 1 : 0);
		// ticket 0742 (rule A): a city's volley from each shot's launch as recorded (render_units_city_volley "1"; absent, a
		// bundle recorded before 0742, or "0": the rule before 0742). The EXE's effects, never a server pixel: absent = 0.
		o.unitsOpt.triggerCityVolley = get("render_units_city_volley") == "1";
		Emit("sceneshot: render_config: units city_volley %d", o.unitsOpt.triggerCityVolley ? 1 : 0);
		// ticket 5055 (rule A): both sides of a melee named by the striker as recorded (render_units_trigger_striker_melee "1";
		// absent, a bundle recorded before 5055, or "0": 5052's rule; in effect only with trigger_striker_weapon "1"). Names
		// only, never a drawn pixel, so absent stays 0 although the product's default is 1 (MC #11607).
		o.unitsOpt.triggerStrikerMelee = get("render_units_trigger_striker_melee") == "1";
		Emit("sceneshot: render_config: units trigger_striker_melee %d", o.unitsOpt.triggerStrikerMelee ? 1 : 0);
		// ticket 5058 (rule A): a city striker's sound names by its owner's era as recorded (render_units_trigger_city_era
		// "1"; absent, a bundle recorded before 5058, or "0": the rule before 5058). Names only, never a drawn pixel, so
		// absent stays 0 although the product's default is 1.
		o.unitsOpt.triggerCityEra = get("render_units_trigger_city_era") == "1";
		Emit("sceneshot: render_config: units trigger_city_era %d", o.unitsOpt.triggerCityEra ? 1 : 0);
		// ticket 5062 (rule A): a city's own strike flies its owner era's projectile as recorded (render_units_trigger_city_shot
		// "1"; absent, a bundle recorded before 5062, or "0": the rule before 5062). The projectile is the EXE's effect, never
		// a pixel of the server's, so absent stays 0 although the product's default is 1.
		o.unitsOpt.triggerCityShot = get("render_units_trigger_city_shot") == "1";
		Emit("sceneshot: render_config: units trigger_city_shot %d", o.unitsOpt.triggerCityShot ? 1 : 0);
		// ticket 4749 (format version 10): the units' shadow as recorded (render_units_shadow "0": off; "1" or absent, the
		// server's default): with it the scene merges the server's unit mask on every frame whose shadow.bin record carries
		// kUnitMaskDropped, as the live render thread did (Scene::unitShadowStep); a version-9 record never carries it
		o.unitsShadow = get("render_units_shadow") == "0" ? 0 : 1;
		Emit("sceneshot: render_config: units shadow %d", o.unitsShadow);
		for(const std::string& n : m.versionNotes)   // the recorder could not carry the unit scene's database or state
			if(n.compare(0, 11, "gap: units:") == 0)
				return Fail(kUnsupported, "render_units=1 and the bundle notes '" + n + "': the unit scene cannot be replayed (record it again)");
		if(!o.decals)
		{
			// the decals did not set the mods' folder: --mods, else refused (never this machine's own Documents)
			std::string mods = a.mods;
			std::replace(mods.begin(), mods.end(), '/', '\\');
			static const char kTail[] = "\\My Games\\Sid Meier's Civilization 5\\MODS";
			const size_t tl = sizeof(kTail) - 1;
			if(mods.empty())
				return Fail(kUnsupported, "render_units=1 with render_decals off: the units' mods folder is not recorded (--mods DIR)");
			if(mods.size() <= tl || _stricmp(mods.c_str() + mods.size() - tl, kTail) != 0)
				return Fail(kUsage, "the units' mods folder '" + Fwd(mods) + "' does not end in " + Fwd(kTail) + " (--mods DIR)");
			if(!DirExists(W(mods)))
				return Fail(kInstall, "the units' mods folder '" + Fwd(mods) + "' is missing (--mods DIR)");
			SetEnvironmentVariableW(L"C5_DOCUMENTS", W(mods.substr(0, mods.size() - tl)).c_str());
		}
	}
	else
		Emit("sceneshot: render_config: units 0 (passes 0, anim 0)");
	// issue 3911 (format version 2): the fog as recorded (render_fow_level: the level the live fog resolved, so that no
	// GraphicsSettingsDX11.ini of this machine is read); a render_config without render_fow draws no fog
	o.fow = rc.count("render_fow") ? RenderFowOption() : 0;
	o.fowLevel = RenderFowLevelOption();
	// ticket 0403 (format version 6): the FOWLevel-3 particles as recorded (render_fow_particles "1": their state from
	// particles.bin, set after the fog's clock seed); --set render_fow_particles=0 draws none (a diagnostic)
	fowParticles = o.fow && get("render_fow_particles") == "1";
	Emit("sceneshot: render_config: fog particles %d", fowParticles ? 1 : 0);
	// ticket 4875 (format version 9): the model module as recorded (render_cities, render_landmarks, render_models_source;
	// render_test_models_draw_all for frames made from a capture); its frames from the modeltap/ members (set in Run) and
	// only when the bundle turns cities or landmarks on (MC #6263): a bundle without them replays as before
	o.models.cities = get("render_cities") == "1";
	o.models.landmarks = get("render_landmarks") == "1";
	o.models.source = get("render_models_source") == "own" ? srvmodels::kModelsSourceOwn : srvmodels::kModelsSourceExe;
	o.models.drawAll = get("render_test_models_draw_all") == "1";
	// ticket 0789: the model store as recorded; a bundle without the key (recorded before 0789) is played with it off
	// (0709's keep, what its live side drew)
	o.models.store = get("render_models_store") == "1";
	o.models.install = install;
	o.models.exe = exe;
	Emit("sceneshot: render_config: cities %d, landmarks %d%s", o.models.cities ? 1 : 0, o.models.landmarks ? 1 : 0,
	     o.models.drawAll ? " (draw all records)" : "");
	o.install = install;
	o.staleFrames = c.staleFrames;
	o.tiles = c.tiles == "none" ? SceneOptions::kTilesNone : SceneOptions::kTilesBake;
	o.bake.pipeline = c.baker != "standin";
	o.bake.install = install;
	o.bake.exe = exe;
	o.bake.res.radius = c.bakeRadius < -1 ? RenderOptionBakedTileCameraRadius() : c.bakeRadius;
	// the player's budgets (contract 6): large, so that the settle call does not wait for the live token bucket; they
	// change when a tile is baked, not what is baked
	o.bake.res.rate = a.bakeRate;
	o.bake.res.burst = a.bakeBurst;
	o.bake.res.gpuMsPerS = c.bakeGpuMsPerS;
	o.bake.budgetMs = a.bakeBudgetMs;
	o.bake.faults.failX = c.testBakeFailX;
	o.bake.faults.failY = c.testBakeFailY;
	o.bake.decalLoad = 1;   // ticket 0642: the textures off the bakes, each set used at once as recorded (mode wait)
	// ticket 0493: issue 0371's near-camera RGBA8 pairs as recorded (render_bake_near "1", the server's default since
	// ce2e535d3); a bundle without the key was recorded before 0371 and draws every tile from its BC1 pair
	o.bake.res.nearCaches = get("render_bake_near") == "1";
	o.bake.res.nearOptions.minClass = o.bake.res.sizeClass;   // as the server's (srv_render_host.cpp, issue 0371)
	Emit("sceneshot: render_config: near pairs %d", o.bake.res.nearCaches ? 1 : 0);
	// ticket 0495 (format version 8): the trees as recorded (render_forest "1": their state from forest.bin, their table
	// reads from feed.bin's PLOTS caller 4); a bundle without the key draws none
	o.forest = get("render_forest") == "1" ? 1 : 0;
	o.forestIdsWaitMs = 600000;   // the ids come from forest.bin, not from a DLL side
	Emit("sceneshot: render_config: forest %d", o.forest);
	// ticket 4852 (epic 0528): the farm fields as recorded (render_farmfields "1": the producer's input from render_config's
	// render_farmfields_state, farm_types.xml from the given install, every production inside poll() so the first frame
	// has it); a bundle without the key draws the tap's farm fields as they are
	o.farmfields = get("render_farmfields") == "1" ? 1 : 0;
	o.farmKeepTap = get("render_farmfields_keep_tap") == "1";
	o.farmThreaded = false;
	o.farmIdsWaitMs = 600000;   // the ids come from render_farmfields_state, not from a DLL side
	Emit("sceneshot: render_config: farm fields %d", o.farmfields);
	// ticket 4934 (epic 0454): the road layout as recorded (render_roadlayout "1" or "2": its input from
	// render_roadlayout_state), or as --option RoadLayout says when given (E2 / E9 pictures; on a bundle recorded without it,
	// its input from the forest's recorded plot table); a bundle without either draws the tap's roads as they are.
	// Ticket 4940: layout 2's shape from render_roadlayout_junction / _jitter (CheckBundle checked them), or, when any of
	// RoadLayout, RoadJunction, RoadJitter is given by --option, all three from the options (their effective values); the
	// choice goes to srvextras::RoadLayoutChoiceSlot() before the scene makes the producer
	{
		bool given = false;
		for(const std::string& l : a.options)
			given = given || _strnicmp(l.c_str(), "RoadLayout", 10) == 0 || _strnicmp(l.c_str(), "RoadJunction", 12) == 0 ||
			        _strnicmp(l.c_str(), "RoadJitter", 10) == 0;
		srvextras::RoadLayoutChoice rc;
		if(given)
		{
			const Vp64Opt::ExtraState rl = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadLayout);
			o.roadLayout = rl.on ? rl.value : 0;
			if(o.roadLayout == 2)
			{
				rc.junction = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadJunction).value;
				rc.jitter = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadJitter).value;
			}
		}
		else
		{
			const std::string rl = get("render_roadlayout");
			o.roadLayout = rl == "1" ? 1 : rl == "2" ? 2 : 0;
			if(o.roadLayout == 2)
			{
				const std::string j = get("render_roadlayout_junction"), t = get("render_roadlayout_jitter");
				rc.junction = j.size() == 1 ? j[0] - '0' : 0;
				rc.jitter = t.size() == 1 ? t[0] - '0' : 0;
			}
		}
		rc.layout = o.roadLayout == 2 ? 2 : 1;
		srvextras::RoadLayoutChoiceSlot() = rc;
		o.roadThreaded = false;
		Emit("sceneshot: render_config: road layout %d, junction %d, jitter %d%s", o.roadLayout, rc.junction, rc.jitter,
		     given ? " (--option)" : "");
	}
	// ticket 0199 part 2: the ranged-attack arc as recorded (render_arc "1": its spawn and remove records from feed.bin, the
	// recorded clock, ArrowSettings.xml and ArrowShader from the given install and EXE); a bundle without the key draws none
	o.arc = get("render_arc") == "1" ? 1 : 0;
	Emit("sceneshot: render_config: arc %d", o.arc);
	Emit("sceneshot: render_config: fog %d (level %d)", o.fow, o.fowLevel);   // issue 3911
	Emit("sceneshot: render_config: tiles %s, baker %s, radius %d, colour %s, fallback %s, pass %d, tess %.2f, water %s%s, overlay %d, tile plots %d; "
	     "the player's bake budgets: rate %.0f/s, burst %d, %.1f ms per call (bundle: rate %.0f/s, burst %d, %.1f ms)",
	     c.tiles.c_str(), c.baker.c_str(), o.bake.res.radius, c.color.c_str(), c.fallback.c_str(), c.pass, c.tess,
	     !c.water ? "off" : o.waterMode == WaterSceneOptions::kPlaceholder ? "placeholder" : "module", o.waterFreeze ? " (frozen)" : "", c.overlay,
	     c.tilePlots, a.bakeRate, a.bakeBurst, a.bakeBudgetMs, c.bakeRate, c.bakeBurst, c.bakeBudgetMs);
	return kOk;
}

// heightfield/load<NNNN>.c5hf, a later copy of the same load load<NNNN>-<k>.c5hf (3904's recorder): (load seq, k), the
// first copy k = 0. The members are loaded in this order, one per baseline table of the feed.
std::pair<uint32_t, uint32_t> LoadKeyOf(const std::string& path)
{
	const size_t p = path.rfind("/load");
	if(p == std::string::npos)
		return std::make_pair(0u, 0u);
	char* end = nullptr;
	const uint32_t seq = (uint32_t)strtoul(path.c_str() + p + 5, &end, 10);
	const uint32_t k = end && *end == '-' ? (uint32_t)strtoul(end + 1, nullptr, 10) : 0u;
	return std::make_pair(seq, k);
}

struct Shot
{
	size_t k = 0;                // the frame record
	std::string name;            // output stem: the frame id, or the camera name
	bool camReplaced = false;    // --camera without its frame in the span: the last frame drawn with the named block
	VPShellCamera cam = {};
};

struct ShotResult
{
	std::string name;
	int64_t frameId = 0;
	uint32_t w = 0, h = 0;
	std::string colourSha, depthSha;
	bool compared = false, withinTolerance = false;
	ColourDiff cd;
	DepthDiff dd;
	std::string settle;
	double settleMs = 0, shotMs = 0;
	std::string png, depth, diff;
};

}  // namespace

// =====================================================================================================================
int Run(const std::vector<std::string>& args, LineFn line, void* ctx)
{
	g_line = line;
	g_lineCtx = ctx;
	struct LogScope
	{
		LogScope() { LogOpen(nullptr, &RenderLine); }
		~LogScope() { LogOpen(nullptr, nullptr); }
	} logScope;
	const int64_t t0 = Qpc();
	Args a;
	std::string why;
	if(!ParseArgs(args, a, why))
		return Fail(kUsage, why);
	// ticket 0614: an asset root is checked before the bundle is read; it then names the install and the mods' folder
	AssetRoot assetRoot;
	if(!a.assetRoot.empty())
	{
		if(const int rc = LoadAssetRoot(a.assetRoot, a.assetRootFrom, assetRoot))
			return rc;
		a.install = N(assetRoot.install);
		a.mods = N(assetRoot.mods);
	}
	const std::wstring dir = W(a.bundle);
	// 1. the whole check (a missing bundle is NoManifest: it fails, it never skips)
	sb::CheckResult cr;
	if(!sb::CheckBundle(dir, &cr).ok())
	{
		Emit("%s", cr.line().c_str());
		const int code = sb::RefusalExitCode(cr.refusal);
		return Fail(code, (cr.refusal == sb::Refusal::NoManifest ? "no bundle at '" + Fwd(a.bundle) + "' (scene.json missing): " : std::string()) +
		                      "the checker refused the bundle");
	}
	sb::Manifest m;
	if(!sb::ReadManifest(dir, &m, &cr).ok())
	{
		Emit("%s", cr.line().c_str());
		return Fail(sb::RefusalExitCode(cr.refusal), "scene.json");
	}
	Emit("sceneshot: bundle %s (format %u, save %s, map %ux%u, turn %u, %u frames from %lld, dumped %zu, machine %s, adapter '%s')", m.bundleId.c_str(),
	     m.formatVersion, m.saveName.c_str(), m.mapW, m.mapH, m.turn, m.frameCount, (long long)m.firstFrameId, m.dumpedFrames.size(),
	     m.machine.c_str(), m.adapterDescription.c_str());
	for(const std::string& n : m.versionNotes)   // issue 0440 (MC #3003): the live span drew what the player cannot
		if(n.compare(0, strlen(sb::kFogParticlesNote), sb::kFogParticlesNote) == 0)
			Emit("sceneshot: NOTE: the bundle notes '%s': the replay draws no particles, so offline does not equal live where they drew",
			     n.c_str());
	// 2. the stream members
	auto member = [&](const std::string& path, std::vector<uint8_t>& b) {
		if(!m.members.count(path))
			return false;
		sb::CheckResult r;
		if(!sb::ReadMember(dir, m, path, &b, &r).ok())
		{
			why = r.line();
			return false;
		}
		return true;
	};
	std::vector<sb::FrameRecord> frames;
	std::vector<VPShellCamera> cams;
	{
		std::vector<uint8_t> b;
		sb::CheckResult r;
		if(!member("frames.bin", b) || !sb::ParseFrames(b, &frames, &r).ok())
			return Fail(kReplayFailed, "frames.bin: " + (why.empty() ? r.line() : why));
		if(!member("cameras.bin", b) || !sb::ParseCameras(b, &cams, &r).ok())
			return Fail(kReplayFailed, "cameras.bin: " + (why.empty() ? r.line() : why));
		if(frames.empty() || frames.size() != cams.size())
			return Fail(kReplayFailed, "frames.bin holds " + std::to_string(frames.size()) + " frames, cameras.bin " + std::to_string(cams.size()) +
			                               " blocks: nothing to draw");
	}
	// 3. the shots
	std::vector<Shot> shots;
	auto indexOf = [&](int64_t id) -> int {
		for(size_t k = 0; k < frames.size(); ++k)
			if(frames[k].frameId == id)
				return (int)k;
		return -1;
	};
	const int64_t firstId = frames.front().frameId, lastId = frames.back().frameId;
	if(a.haveFrame || a.haveSpan)
	{
		const int64_t lo = a.haveFrame ? a.frame : a.spanA, hi = a.haveFrame ? a.frame : a.spanB;
		if(lo < firstId || hi > lastId)
			return Fail(kFrameOutside, (a.haveFrame ? "frame " + std::to_string(a.frame) : "span " + std::to_string(lo) + ":" + std::to_string(hi)) +
			                               " is outside the recorded span " + std::to_string(firstId) + ":" + std::to_string(lastId));
		if(a.haveFrame && indexOf(a.frame) < 0)
			return Fail(kFrameOutside, "frame " + std::to_string(a.frame) + " is inside the span " + std::to_string(firstId) + ":" +
			                               std::to_string(lastId) + " but was not drawn (no record)");
		for(size_t k = 0; k < frames.size(); ++k)
			if(frames[k].frameId >= lo && frames[k].frameId <= hi)
			{
				Shot s;
				s.k = k;
				s.name = std::to_string(frames[k].frameId);
				s.cam = cams[k];
				shots.push_back(s);
			}
		if(shots.empty())
			return Fail(kFrameOutside, "span " + std::to_string(lo) + ":" + std::to_string(hi) + " holds no recorded frame");
	}
	else if(a.dumped)
	{
		for(int64_t id : m.dumpedFrames)
		{
			const int k = indexOf(id);
			if(k < 0)
				return Fail(kReplayFailed, "dumped frame " + std::to_string(id) + " has no record");
			Shot s;
			s.k = (size_t)k;
			s.name = std::to_string(id);
			s.cam = cams[(size_t)k];
			shots.push_back(s);
		}
		if(shots.empty())
			return Fail(kNoDump, "the bundle dumped no frame");
		std::sort(shots.begin(), shots.end(), [](const Shot& x, const Shot& y) { return x.k < y.k; });
	}
	else
	{
		const std::string path = "cameras/" + a.camera + ".vpcam";
		if(!m.members.count(path))
		{
			std::string names;
			for(const auto& kv : m.members)
				if(kv.second.kind == sb::MemberKind::NamedCameraBlock)
					names += (names.empty() ? "" : ", ") + kv.first.substr(8, kv.first.size() - 8 - 6);
			return Fail(kNoCamera, "camera '" + a.camera + "' is not in the bundle (named cameras: " + (names.empty() ? "none" : names) + ")");
		}
		std::vector<uint8_t> b;
		std::vector<VPShellCamera> one;
		sb::CheckResult r;
		if(!member(path, b) || !sb::ParseCameras(b, &one, &r).ok() || one.size() != 1)
			return Fail(kReplayFailed, path + ": " + (why.empty() ? r.line() : why));
		Shot s;
		s.name = a.camera;
		s.cam = one[0];
		const int k = indexOf(one[0].frameStamp);
		if(k >= 0)
			s.k = (size_t)k;
		else
		{
			s.k = frames.size() - 1;
			s.camReplaced = true;
		}
		Emit("sceneshot: camera '%s': %s", a.camera.c_str(),
		     k >= 0 ? ("its frame " + std::to_string(one[0].frameStamp) + " is in the span").c_str()
		            : ("its frame " + std::to_string(one[0].frameStamp) + " is not in the span: drawn after the last frame with the named block").c_str());
		shots.push_back(s);
	}
	if(a.compare)
		for(const Shot& s : shots)
			if(s.camReplaced || !(frames[s.k].flags & sb::kFrameDumped))
				return Fail(kNoDump, "--compare: frame " + std::to_string(frames[s.k].frameId) + (s.camReplaced ? " drawn with a named camera" : "") +
				                         " has no dump of the server's own");
	// 4. the options, the install and the EXE
	{   // ticket 4938: the --option lines into SrvOptions() (none: every option at its default, as before)
		std::string ow;
		if(!ApplyOptions(a.options, g_sceneshotOptions, ow))
			return Fail(kUsage, ow);
		for(const std::string& l : a.options)
			Emit("sceneshot: --option %s: an [Extras] line the recording did not have (an extra's picture or a diagnosis; "
			     "with an extra on, not P1)", l.c_str());
	}
	SceneOptions so;
	std::wstring install, exe;
	bool fowParticles = false;   // ticket 0403: render_fow_particles (after --set)
	if(const int rc = OptionsFromBundle(a, m, so, install, exe, fowParticles))
		return rc;
	// ticket 0614, rule 1: no source in a claimed pool instance (checked again before the OK line)
	const std::wstring docsUsed = (so.decals || so.units) ? EnvW(L"C5_DOCUMENTS") : std::wstring();
	auto claimedSources = [&]() {
		std::string w = ClaimedWhy(install, "the install");
		if(w.empty())
			w = ClaimedWhy(exe, "the EXE");
		if(w.empty() && !docsUsed.empty())
			w = ClaimedWhy(docsUsed, "the mods' Documents folder");
		return w;
	};
	{
		const std::string w = claimedSources();
		if(!w.empty())
			return Fail(kInstall, w);
	}
	if(!assetRoot.dir.empty() && !docsUsed.empty())
	{
		// rule 2: the asset root's database must be read as made (ModFolders' every-folder rule would widen the set silently)
		ActiveModFolders info;
		std::wstring modsRoot;
		ModFolders(&modsRoot, &info);
		if(!info.known || info.activated != assetRoot.activated || info.withoutFolder != 0)
			return Fail(kInstall, "the asset root's mods database: " + std::string(info.known ? "" : "not read; ") + std::to_string(info.activated) +
			                          " activated, " + std::to_string(info.withoutFolder) + " without a folder (asset-root.txt: " + std::to_string(assetRoot.activated) +
			                          " activated, each with its folder)");
	}
	else if(assetRoot.dir.empty())
	{
		std::wstring inst = InstanceOf(install);
		if(inst.empty() && !docsUsed.empty())
			inst = InstanceOf(docsUsed);
		const std::wstring name = inst.empty() ? std::wstring() : inst.substr(inst.find_last_of(L'\\') + 1);
		if(!name.empty())
			Emit("sceneshot: NOTE: the sources are pool instance %s's own (unclaimed now): a claim during the replay fails it; --asset-root DIR reads a "
			     "frozen copy (ticket 0614)",
			     N(name).c_str());
	}
	if(!FileExists(exe))
		return Fail(kInstall, "the EXE '" + Fwd(N(exe)) + "' is missing (--install DIR or --exe PATH)");
	{
		std::string hex;
		uint64_t bytes = 0;
		auto pin = m.pins.find("exe_sha256");
		if(pin != m.pins.end() && !pin->second.empty())
		{
			if(!sb::Sha256FileHex(exe, &hex, &bytes).ok())
				return Fail(kInstall, "cannot hash the EXE '" + Fwd(N(exe)) + "'");
			if(hex != pin->second)
			{
				if(!a.allowOtherExe)
					return Fail(kInstall, "the EXE '" + Fwd(N(exe)) + "' has SHA-256 " + hex + ", the bundle's pins.exe_sha256 is " + pin->second +
					                          " (its shaders may differ; --allow-other-exe draws anyway, not P1)");
				Emit("sceneshot: NOTE: the EXE differs from pins.exe_sha256 (--allow-other-exe): not P1");
			}
			else
				Emit("sceneshot: EXE %s = pins.exe_sha256", Fwd(N(exe)).c_str());
		}
		else
			Emit("sceneshot: NOTE: the bundle has no pins.exe_sha256: the EXE '%s' is not checked", Fwd(N(exe)).c_str());
	}
	if(m.vt != "unknown" && !(so.bake.pipeline == false && so.water == false))
	{
		sb::Manifest here;
		const int64_t qa = Qpc();
		if(!sb::ComputeAssetSet(install, m.machine, &here).ok())
			return Fail(kInstall, "cannot compute the asset set of '" + Fwd(N(install)) + "'");
		const bool same = here.vt == m.vt && here.vtHash == m.vtHash;
		Emit("sceneshot: asset set of %s: vt %s %s (%u files, %.0f ms); the bundle's: vt %s %s", Fwd(N(install)).c_str(), here.vt.c_str(),
		     here.vtHash.c_str(), here.vtFiles, QpcToMs(Qpc() - qa), m.vt.c_str(), m.vtHash.c_str());
		if(!same && !a.allowOtherAssets)
			return Fail(kInstall, "the install's asset set differs from the bundle's (D94: compare like with like; --allow-other-assets draws anyway, not P1)");
	}
	// 5. the feed (render_tiles=bake: the residency reads its tables from it; render_fow=1: the fog its, issue 3911)
	std::vector<sb::FeedRecord> feed;
	std::unique_ptr<FeedSource> fileFeed;
	FileFeedControl* feedCtl = nullptr;
	if(so.fow && !m.members.count("fog_clock.bin"))
		return Fail(kReplayFailed, "render_fow=1 but fog_clock.bin is missing: the cloud planes' clock cannot be redrawn (contract 2.11)");
	// ticket 0495 (format version 8): the trees' state (forest.bin: the session seed goes into SceneOptions before
	// Scene::init, which chooses the cards with it) and the near pairs' (near.bin, restored after the first frame's settle)
	sb::ForestStateFile forestState;
	bool haveForestState = false;
	if(so.forest)
	{
		std::vector<uint8_t> b;
		sb::CheckResult r;
		why.clear();
		if(!m.members.count("forest.bin"))
		{
			for(const std::string& n : m.versionNotes)
				if(n.compare(0, strlen(sb::kForestNote), sb::kForestNote) == 0)
					return Fail(kUnsupported, "render_forest=1 and the bundle notes '" + n + "': the trees cannot be replayed (record it again)");
			return Fail(kReplayFailed, "render_forest=1 without forest.bin");
		}
		if(!member("forest.bin", b) || !sb::ParseForestState(b, &forestState, &r).ok())
			return Fail(kReplayFailed, "forest.bin: " + (why.empty() ? r.line() : why));
		haveForestState = true;
		so.forestSeed = forestState.sessionSeed;
		Emit("sceneshot: forest.bin: %zu records, %zu built, %zu marks, cut %ux%u, session seed %08x, ids %d / %d", forestState.records.size(),
		     forestState.built.size(), forestState.marks.size(), forestState.cutW, forestState.cutH, forestState.sessionSeed, forestState.forestId,
		     forestState.jungleId);
	}
	sb::NearPairsFile nearState;
	bool haveNearState = false;
	if(so.tiles == SceneOptions::kTilesBake && so.bake.res.nearCaches)
	{
		if(m.members.count("near.bin"))
		{
			std::vector<uint8_t> b;
			sb::CheckResult r;
			why.clear();
			if(!member("near.bin", b) || !sb::ParseNearPairs(b, &nearState, &r).ok())
				return Fail(kReplayFailed, "near.bin: " + (why.empty() ? r.line() : why));
			haveNearState = true;
		}
		else
		{
			std::string note = "no near.bin (a bundle before format version 8)";
			for(const std::string& n : m.versionNotes)
				if(n.compare(0, strlen(sb::kNearNote), sb::kNearNote) == 0)
					note = n;
			Emit("sceneshot: NOTE: render_bake_near=1 and %s: the near pairs start empty, not P1", note.c_str());
		}
	}
	if(so.tiles == SceneOptions::kTilesBake || so.fow || so.units || so.forest)   // issue 0440: the unit scene's drains and table copies
	{
		std::vector<uint8_t> b;
		sb::CheckResult r;
		why.clear();
		if(!member("feed.bin", b))
			return Fail(kReplayFailed, "feed.bin: " + (why.empty() ? "missing: the residency's and the fog's plot tables (contract 7, 2.11)" : why));
		if(!sb::ParseFeed(b, &feed, &r).ok())
			return Fail(kReplayFailed, "feed.bin: " + r.line());
		std::vector<FeedCall> calls;
		for(const sb::FeedRecord& fr : feed)
		{
			FeedCall c;
			c.type = fr.type;
			c.loopIter = fr.loopIter;
			c.batch = fr.batch;
			c.drainMs = fr.drainMs;   // issue 0440: the unit scene's batch clock and table copies (as MakeFileFeed's bundle form)
			for(const sb::UnitCopy& u : fr.unitCopies)
			{
				FeedUnitCopy fc;
				fc.gen = u.tableGen;
				fc.units = u.units;
				c.unitCopies.push_back(std::move(fc));
			}
			c.playerCopies = fr.playerCopies;
			c.caller = fr.caller;
			c.gen = fr.snapshotGen;
			c.width = fr.width;
			c.height = fr.height;
			c.table = fr.plots;
			calls.push_back(std::move(c));
		}
		fileFeed = MakeFileFeed(std::move(calls), &feedCtl, &why);
		if(!fileFeed)
			return Fail(kReplayFailed, "feed.bin: " + why);
		so.bake.feed = fileFeed.get();
	}
	// 6. the device and the scene
	Device d;
	if(const int rc = MakeDevice(a, m, d))
		return rc;
	const bool sameAdapter = (!m.adapterDescription.empty() && d.description == m.adapterDescription) ||
	                         (!m.adapterLuid.empty() && d.luid == NormLuid(m.adapterLuid));
	Emit("sceneshot: device on '%s' (LUID %s)%s", d.description.c_str(), d.luid.c_str(),
	     sameAdapter ? ": the bundle's adapter" : ": NOT the bundle's adapter (a comparison here is not P1)");
	HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	struct CoScope
	{
		bool on;
		~CoScope()
		{
			if(on)
				CoUninitialize();
		}
	} coScope{SUCCEEDED(coHr)};
	std::unique_ptr<srvunits::FeedSourcePort> unitsPort;   // issue 0440: outlives the scene's unit scene
	Scene scene;
	// ticket 4934: a road layout switched on by --option on a bundle recorded without it reads the forest's table copies (the
	// forest's reads stay exactly the recording's; the producer sees the newest copy the forest took)
	struct TeeTable : FowPlotTable
	{
		std::unique_ptr<FowPlotTable> inner;
		std::shared_ptr<std::vector<SrvFeed::PlotRecord>> last = std::make_shared<std::vector<SrvFeed::PlotRecord>>();
		std::shared_ptr<std::vector<uint32_t>> meta = std::make_shared<std::vector<uint32_t>>(3, 0u);   // generation, w, h
		void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& ww, uint32_t& hh) override
		{
			inner->copy(out, g, ww, hh);
			*last = out;
			(*meta)[0] = g;
			(*meta)[1] = ww;
			(*meta)[2] = hh;
		}
	};
	std::shared_ptr<std::vector<SrvFeed::PlotRecord>> roadTableLast;
	std::shared_ptr<std::vector<uint32_t>> roadTableMeta;
	if(so.fow)   // issue 3911: the fog's table reads from the file feed, its first at Scene::init (the live fog's start)
		scene.fow().setTable(MakeFeedSourcePlotTable(*fileFeed));
	// ticket 0495: the forest's table reads from the file feed (PLOTS caller 4), as the recording host gave it the recording
	// feed's; the slot is the process's, cleared when this run ends
	struct ForestSlotScope
	{
		~ForestSlotScope() { ForestPlotTableSlot() = nullptr; }
	} forestSlotScope;
	if(so.forest)
	{
		FeedSource* ff = fileFeed.get();
		if(so.roadLayout && m.renderConfig.find("render_roadlayout_state") == m.renderConfig.end())
		{   // ticket 4934: the road layout switched on by --option reads the forest's table copies (theirs stay the recording's)
			std::shared_ptr<std::vector<SrvFeed::PlotRecord>> last(new std::vector<SrvFeed::PlotRecord>());
			std::shared_ptr<std::vector<uint32_t>> meta(new std::vector<uint32_t>(3, 0u));
			roadTableLast = last;
			roadTableMeta = meta;
			ForestPlotTableSlot() = [ff, last, meta]() -> std::unique_ptr<FowPlotTable> {
				std::unique_ptr<TeeTable> tee(new TeeTable());
				tee->inner = MakeFeedSourcePlotTable(*ff, kPlotsForest);
				tee->last = last;
				tee->meta = meta;
				return std::unique_ptr<FowPlotTable>(tee.release());
			};
		}
		else
			ForestPlotTableSlot() = [ff]() { return MakeFeedSourcePlotTable(*ff, kPlotsForest); };
	}
	{
		const c5r::Status st = scene.init(d.dev, d.ctx, so);
		if(!st.ok())
			return Fail(kReplayFailed, "Scene::init: " + st.str());
	}
	if(so.forest)
	{
		std::string fw;
		if(!scene.forest())
			return Fail(kReplayFailed, "render_forest=1 but the scene has no forest (this player does not link server/src/forest/?)");
		if(scene.forest()->failed(fw))
			return Fail(kReplayFailed, "forest: " + fw);
		if(forestState.flags & sb::kForestIdsResolved)   // the ids the live DLL side published (no DLL here)
			scene.forest()->setRecordedFeatureIds(forestState.forestId, forestState.jungleId);
	}
	if(so.farmfields)   // ticket 4852: the producer's recorded input, before the feed and the first frame
	{
		if(!scene.farm())
			return Fail(kReplayFailed, "render_farmfields=1 but the scene has no farm producer");
		auto fst = m.renderConfig.find("render_farmfields_state");
		if(fst == m.renderConfig.end())
		{
			for(const std::string& n : m.versionNotes)
				if(n.compare(0, strlen(sb::kFarmNote), sb::kFarmNote) == 0)
					return Fail(kUnsupported, "render_farmfields=1 and the bundle notes '" + n + "': the farm fields cannot be replayed (record it again)");
			return Fail(kReplayFailed, "render_farmfields=1 without render_farmfields_state");
		}
		std::string fw;
		if(!scene.farm()->restoreSpanInput(fst->second, fw))
			return Fail(kReplayFailed, "render_farmfields_state: " + fw);
		Emit("sceneshot: render_farmfields_state in use: %zu farms", scene.farm()->input().plots.size());
	}
	if(so.roadLayout)   // ticket 4934: the road producer's recorded input, else the forest's table copies (--option on)
	{
		if(!scene.roads())
			return Fail(kReplayFailed, "render_roadlayout=1 but the scene has no road producer (this player does not link server/src/extras/?)");
		auto rst = m.renderConfig.find("render_roadlayout_state");
		if(rst != m.renderConfig.end())
		{
			std::string rw;
			if(!scene.roads()->restoreSpanInput(rst->second, rw))
				return Fail(kReplayFailed, "render_roadlayout_state: " + rw);
			Emit("sceneshot: render_roadlayout_state in use");
		}
		else
		{
			for(const std::string& n : m.versionNotes)
				if(n.compare(0, strlen(sb::kRoadNote), sb::kRoadNote) == 0)
					return Fail(kUnsupported, "render_roadlayout=1 and the bundle notes '" + n + "': the road layout cannot be replayed (record it again)");
			if(!roadTableLast)
				return Fail(kUnsupported, "--option FixRoadLayout=1 on a bundle without render_roadlayout_state needs the forest's recorded plot "
				                          "table (render_forest=1)");
			std::shared_ptr<std::vector<SrvFeed::PlotRecord>> last = roadTableLast;
			std::shared_ptr<std::vector<uint32_t>> meta = roadTableMeta;
			scene.roads()->setTable([last, meta](std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& w, uint32_t& h) {
				out = *last;
				g = (*meta)[0];
				w = (*meta)[1];
				h = (*meta)[2];
			});
			Emit("sceneshot: road layout from the forest's recorded plot table (%zu records at init)", last->size());
		}
	}
	struct SceneScope
	{
		Scene& s;
		~SceneScope()
		{
			s.releaseDevice();
			s.detach();
		}
	} sceneScope{scene};
	if(so.tiles == SceneOptions::kTilesBake && !scene.source())
		return Fail(kReplayFailed, "the residency did not start (render: lines above)");
	// issue 0364 (format version 3): the decal set in force at the span's first frame, through the scene's decal reader,
	// before the field's load (so the field's first bakes read it, as the live residency's re-bakes did)
	{
		std::string dw;
		if(scene.decalFailed(dw))
			return Fail(kReplayFailed, "decals: " + dw);
		std::string note;
		if(!so.decals)   // render_decals not 1 in the bundle (the checker refuses a member then) or --set render_decals=0
			note = "decals off: no decal set is applied";
		else if(!LoadDecalMember(scene, dir, m, &note, &why))
			return Fail(kReplayFailed, "decals: " + why);
		Emit("sceneshot: %s", note.c_str());
	}
	FileOverlayControl* ovlCtl = nullptr;
	if(so.overlay)
	{
		std::unique_ptr<OverlaySource> os = MakeFileOverlaySource(d.dev, dir, m, &ovlCtl, &why);
		if(!os)
			return Fail(kReplayFailed, "overlay: " + why);
		scene.setOverlaySource(std::move(os));
	}
	// issue 4308: shadow.bin, the shadow maps and the ice meshes as the scene's sources (frame k selected below)
	FileSeamGControl* shCtl = nullptr;
	FileSeamGControl* iceCtl = nullptr;
	std::vector<sb::ShadowRecord> shRecs;
	{
		std::string iw;
		if(scene.iceFailed(iw))
			return Fail(kReplayFailed, "ice: " + iw);
		std::unique_ptr<ShadowSource> ss;
		std::unique_ptr<IceSource> is;
		std::string note;
		if(!LoadSeamGMembers(d.dev, dir, m, &ss, &shCtl, &is, &iceCtl, &shRecs, &note, &why))
			return Fail(kReplayFailed, why);
		Emit("sceneshot: %s", note.c_str());
		if(so.shadow && ss)
			scene.setShadowSource(std::move(ss));
		else
			shCtl = nullptr;
		if(so.ice && is)
			scene.setIceSource(std::move(is));
		else
			iceCtl = nullptr;
	}
	// issue 3911: the fog as the live render thread starts it (Scene::init ties the fog's overlay to the section, which the
	// player has not): started again with the overlay on, no second table read (srv_scene_sources.h RestartFowWithOverlay)
	if(so.fow)
	{
		RestartFowWithOverlay(scene, d.dev, so, *fileFeed);
		if(feedCtl->failed())
			return Fail(kReplayFailed, "the fog's first table read: " + feedCtl->why());
		Emit("sceneshot: fog started: FOWLevel %d, mask %u, table %ux%u", scene.fow().level(), scene.fow().maskSize(), scene.fow().tableWidth(),
		     scene.fow().tableHeight());
	}
	std::vector<std::string> fieldMembers, waterMembers;
	for(const auto& kv : m.members)
	{
		if(kv.second.kind == sb::MemberKind::Heightfield)
			fieldMembers.push_back(kv.first);
		else if(kv.second.kind == sb::MemberKind::WaterTap)
			waterMembers.push_back(kv.first);
	}
	if(fieldMembers.empty())
		return Fail(kReplayFailed, "no heightfield member: nothing to draw");
	std::stable_sort(fieldMembers.begin(), fieldMembers.end(),
	                 [](const std::string& x, const std::string& y) { return LoadKeyOf(x) < LoadKeyOf(y); });
	// issue 0440 (format version 5): the server's unit scene, made as server.exe makes it (MakeSceneUnitsWith with the
	// live environment opener) but with its table copies from the file feed and its database from the units_db member
	// (written into --out: the player never opens a file inside the bundle for SQLite); its controller's state from
	// units_history.bin, replayed below
	srvunits::SceneUnits* units = nullptr;
	srvunits::UnitsHostProbe* unitsProbe = nullptr;
	sb::UnitsHistoryFile hist;
	c5r::heightfield::HeightGrid unitsGrid;   // the scene's grid (the newest field loaded: the same C5HF through c5h::readTap)
	bool haveUnitsGrid = false;
	if(so.units)
	{
		std::string dbMember;
		for(const auto& kv : m.members)
			if(kv.second.kind == sb::MemberKind::UnitsDb)
				dbMember = kv.first;
		std::vector<uint8_t> b;
		sb::CheckResult r;
		why.clear();
		if(dbMember.empty() || !m.members.count("units_history.bin"))
			return Fail(kReplayFailed, "render_units=1 without " + std::string(dbMember.empty() ? "a units_db member" : "units_history.bin"));
		if(!member("units_history.bin", b) || !sb::ParseUnitsHistory(b, &hist, &r).ok())
			return Fail(kReplayFailed, "units_history.bin: " + (why.empty() ? r.line() : why));
		if(!member(dbMember, b))
			return Fail(kReplayFailed, dbMember + ": " + why);
		std::wstring o = W(a.out);
		std::replace(o.begin(), o.end(), L'/', L'\\');
		wchar_t full[MAX_PATH];
		if(!GetFullPathNameW(o.c_str(), MAX_PATH, full, nullptr))
			return Fail(kWriteFailed, "cannot create --out '" + Fwd(a.out) + "'");
		const int cr = SHCreateDirectoryExW(nullptr, full, nullptr);
		if(!(cr == ERROR_SUCCESS || cr == ERROR_ALREADY_EXISTS || cr == ERROR_FILE_EXISTS) || !DirExists(full))
			return Fail(kWriteFailed, "cannot create --out '" + Fwd(a.out) + "'");
		const std::wstring dbFile = std::wstring(full) + L"\\units-" + W(dbMember.substr(6));   // units-gen<NNNNNNNNNN>.db
		if(!WriteFileBytes(dbFile, b.data(), b.size()))
			return Fail(kWriteFailed, Fwd(N(dbFile)));
		so.unitsOpt.dbOverride = dbFile;
		unitsPort.reset(new srvunits::FeedSourcePort(*fileFeed));
		std::unique_ptr<srvunits::SceneUnits> u = srvunits::MakeSceneUnitsWith(
		    so.unitsOpt, *unitsPort, srvunits::LiveEnvOpener(so.unitsOpt), [](const char* line) { RNote("%s", line); }, srvunits::ClockFn());
		units = u.get();
		unitsProbe = dynamic_cast<srvunits::UnitsHostProbe*>(units);
		if(!unitsProbe)
			return Fail(kReplayFailed, "the unit scene has no probe (UnitsHostProbe)");
		// issue 4983: the unit extras, attached as server.exe's factory attaches them (srv_units_scene.cpp MakeLive); the units
		// are ours (a bundle with render_units=1 was recorded with ServerUnits = 1, which server.exe's render_units comes from).
		// A player without server/src/extras/units/ gets null here and draws as before; with every unit extra off the module
		// is dropped (no per-unit call)
		unitsProbe->attachExtras(srvunits::MakeUnitsExtras(), SrvOptions(), true);
		scene.setUnits(std::move(u));
		Emit("sceneshot: units: database %s (%zu bytes) as %s; history of %zu frames from frame %lld (loop iteration %llu), ready at the span %d",
		     dbMember.c_str(), b.size(), Fwd(N(dbFile)).c_str(), hist.records.size(), (long long)hist.records.front().frameId,
		     (unsigned long long)hist.records.front().loopIter, (hist.flags & sb::kUnitsHistReady) ? 1 : 0);
	}
	// ticket 4875 (format version 9): the model module, its database (the units_db member written above, else
	// C5R_LANDMARK_DB) and the modeltap/ members, applied frame by frame below (ApplyModelTapFrame)
	ModelTapMembers modelTaps;
	if(so.models.any())
	{
		std::string mw;
		if(!scene.configureModels(so.models, mw))
			return Fail(kReplayFailed, "models: " + mw);
		std::wstring db = so.unitsOpt.dbOverride;
		if(db.empty())
		{
			wchar_t env[MAX_PATH] = L"";
			if(GetEnvironmentVariableW(L"C5R_LANDMARK_DB", env, MAX_PATH))
				db = env;
		}
		if(db.empty())
			return Fail(kUnsupported, "render_cities / render_landmarks = 1: the model library needs the game database (a units_db member "
			                          "or C5R_LANDMARK_DB)");
		scene.models()->setDatabase(db, 1);
		std::string note;
		if(!LoadModelTapMembers(dir, m, &modelTaps, &note, &why))
			return Fail(kReplayFailed, "models: " + why);
		Emit("sceneshot: models: %s (database %s)", note.c_str(), Fwd(N(db)).c_str());
		// ticket 4883: the recorder's gap note for the CPU-built meshes (ticket 4879's geometry section, no member yet)
		for(const std::string& n : m.versionNotes)
			if(n.compare(0, strlen(sb::kModelGeomNote), sb::kModelGeomNote) == 0)
				Emit("sceneshot: NOTE the bundle lacks the CPU-built meshes the live models drew: %s; the replay draws the models "
				     "without them, so this bundle is not P1", n.c_str());
	}
	// 7. the replay
	size_t fieldNext = 0;
	uint32_t fieldLoad = 0;
	int64_t drains = 0, baselines = 0;
	auto loadField = [&](size_t i) -> int {
		const uint32_t seq = LoadKeyOf(fieldMembers[i]).first;
		const VPShellCamera* cam = cams[0].gridValid == 1 && cams[0].gridSeq == seq ? &cams[0] : nullptr;
		if(!LoadFieldMember(scene, dir, m, fieldMembers[i], seq, cam, &why))
			return Fail(kReplayFailed, "height field: " + why);
		fieldLoad = seq;
		Emit("sceneshot: height field %s (load seq %u) applied%s", fieldMembers[i].c_str(), seq, cam ? ", checked against frame 0's grid" : "");
		if(units)   // issue 0440: the unit scene's grid in its history frames (Scene::draw passes its m_grid)
		{
			std::vector<uint8_t> b;
			sb::CheckResult r;
			if(!member(fieldMembers[i], b) || !sb::ParseHeightfield(b, &unitsGrid, &r).ok())
				return Fail(kReplayFailed, "height field for the units: " + (why.empty() ? r.line() : why));
			haveUnitsGrid = true;
		}
		return kOk;
	};
	auto feedUpTo = [&](uint64_t iter, bool inSpan) -> int {
		if(!fileFeed)
			return kOk;
		for(;;)
		{
			const size_t pos = feed.size() - feedCtl->remaining();
			if(pos >= feed.size() || feed[pos].loopIter > iter)
				return kOk;
			const sb::FeedRecord& r = feed[pos];
			if(r.type == sb::kFeedDrain)
			{
				feedCtl->setLoopIter(r.loopIter);
				SrvFeed::Batch b;
				if(!fileFeed->drain(b))
					return Fail(kReplayFailed, "feed record " + std::to_string(pos) + " (DRAIN, loop iteration " + std::to_string(r.loopIter) +
					                               ") was not yielded: " + feedCtl->why());
				drains++;
				if(!b.changedPlots.empty())
					scene.plotsChanged(b.changedPlots.data(), b.changedPlots.size());
				scene.fowFeed(b);   // issue 3911: as the host's Loop (its table reads are PLOTS caller 3)
				scene.unitFeed(b, feedCtl->lastDrainMs());   // issue 0440: as the host's Loop (no unit scene: nothing)
				scene.forestFeed(b);   // ticket 0495: as the host's Loop (its table reads are PLOTS caller 4; no forest: nothing)
				if(scene.roads())      // ticket 4934: no table reads of its own (a restored input, or the forest's copies)
					scene.roads()->onBatch(b);
				scene.arcFeed(b);      // ticket 0199 part 2: as the host's Loop (no table reads; render_arc=0: nothing)
				if(units && feedCtl->unitCopiesLeft())
					return Fail(kReplayFailed, "feed record " + std::to_string(pos) + ": the recording took " + std::to_string(feedCtl->unitCopiesLeft()) +
					                               " unit table copies after this drain that the replay's unit scene did not take");
			}
			else if(r.caller == kPlotsBaseline)
			{
				if(inSpan)
					return Fail(kUnsupported, "a height-field load (baseline table, feed record " + std::to_string(pos) + ") inside the recorded span");
				if(fieldNext >= fieldMembers.size())
					return Fail(kReplayFailed, "feed record " + std::to_string(pos) + ": a baseline table without a heightfield member left to load (" +
					                               std::to_string(fieldMembers.size()) + " members)");
				if(const int rc = loadField(fieldNext++))
					return rc;
				baselines++;
				if(feed.size() - feedCtl->remaining() != pos + 1)
					return Fail(kReplayFailed, "the field load did not read the baseline table of feed record " + std::to_string(pos) +
					                               " (the residency refused the field? render: lines above)");
			}
			else
				return Fail(kReplayFailed, "feed record " + std::to_string(pos) + " (PLOTS of " +
				                               (r.caller == kPlotsFog ? "the fog" : r.caller == kPlotsForest ? "the forest" : "onPlotsChanged") +
				                               ", loop iteration " + std::to_string(r.loopIter) + ") was not read by the drain before it: the " +
				                               (r.caller == kPlotsFog ? "fog's" : r.caller == kPlotsForest ? "forest's" : "residency's") +
				                               " state differs from the recorded one");
			if(feedCtl->failed())
				return Fail(kReplayFailed, "feed: " + feedCtl->why());
		}
	};
	// issue 0440: the unit controller's state at the span's first frame, from its input history (units_history.bin): each
	// recorded frame from the controller's first one, after the feed up to its loop iteration, through the calls the live
	// Scene::poll and Scene::draw make (poll; the fog's plot states, beginFrame and the scene pass with the recorded camera,
	// clock and the scene's grid), its ready() state checked against the recorded one before each frame
	if(units && a.unitsNoHistory)   // the diagnostic: the scene's pre-check passed, its controller starts at the span's first frame
	{
		Emit("sceneshot: NOTE: --units-no-history: units_history.bin is not replayed: the controller starts at the span's first frame, not P1");
		units->poll();
		unitsProbe->waitPrecheck();
	}
	else if(units)
	{
		Targets htg;
		const int64_t qh = Qpc();
		for(size_t i = 0; i < hist.records.size(); ++i)
		{
			const sb::UnitsHistoryRecord& hr = hist.records[i];
			if(const int rc = feedUpTo(hr.loopIter, false))
				return rc;
			units->poll();
			const bool live = (hr.flags & sb::kUnitsHistReady) != 0;
			if(units->ready() != live)
				return Fail(kReplayFailed, "units: history frame " + std::to_string(hr.frameId) + " (record " + std::to_string(i) + "): ready() " +
				                               (live ? "1" : "0") + " live, " + (live ? "0" : "1") + " in the replay: the unit scene's state differs");
			if(i == 0)
			{
				unitsProbe->waitPrecheck();   // the live pre-check passed before this frame (its first beginFrame that built)
				std::string fw;
				if(!unitsProbe->precheckPassed() || units->failed(fw))
					return Fail(kReplayFailed, "units: the pre-check did not pass offline" + (fw.empty() ? std::string(" (render: lines above)") : ": " + fw));
			}
			if(!haveUnitsGrid)
				return Fail(kReplayFailed, "units: history frame " + std::to_string(hr.frameId) + " before any height field: the live scene had one");
			if(!htg.ensure(d.dev, frames[0].width, frames[0].height, why))
				return Fail(kReplayFailed, "units: history targets: " + why);
			const std::vector<c5r::fow::PlotState>& fs = scene.fow().states();
			units->setPlotStates(fs.empty() ? nullptr : reinterpret_cast<const uint8_t*>(fs.data()), fs.size(), scene.fow().tableWidth(),
			                     scene.fow().tableHeight(), scene.fow().fogGeneration());
			srvunits::UnitsCamera uc;   // srv_render_scene.cpp UnitsCameraOf
			memcpy(uc.view, hr.cam.view[0], sizeof(uc.view));
			memcpy(uc.proj, hr.cam.proj, sizeof(uc.proj));
			memcpy(uc.camWorld, hr.cam.camWorld, sizeof(uc.camWorld));
			uc.wrapPeriod = hr.cam.wrapPeriod;
			uc.frameId = hr.frameId;
			uc.nowMs = hr.nowMs;
			units->beginFrame(uc, &unitsGrid);
			units->drawScene(d.ctx, htg.rtv, htg.dsv, frames[0].width, frames[0].height, uc, &unitsGrid);
			d.ctx->OMSetRenderTargets(0, nullptr, nullptr);
			std::string fw;
			if(units->failed(fw))
				return Fail(kReplayFailed, "units: the unit scene failed in history frame " + std::to_string(hr.frameId) + ": " + fw);
		}
		Emit("sceneshot: units: the controller's history replayed: %zu frames (%lld..%lld) in %.0f ms", hist.records.size(),
		     (long long)hist.records.front().frameId, (long long)hist.records.back().frameId, QpcToMs(Qpc() - qh));
	}
	// the feed before the span: the field's load and every drain since the server's start
	if(const int rc = feedUpTo(frames[0].loopIter, false))
		return rc;
	if(!fileFeed || baselines == 0)
	{
		if(fileFeed && so.tiles == SceneOptions::kTilesBake)   // issue 3911: with render_tiles=none the feed is the fog's only
			return Fail(kReplayFailed, "feed.bin holds no baseline table before the span: the residency never read the field's plots");
		if(const int rc = loadField(fieldMembers.size() - 1))   // no residency: the last load before the span
			return rc;
	}
	// epic 0465, ticket 4907: live terrain's field generations (render_live_terrain_journal), once the load's field is in;
	// each frame then gets the generation the live server drew (liveTerrainReplayTo before its poll)
	bool liveReplay = false;
	{
		auto lon = m.renderConfig.find("render_live_terrain");
		if(lon != m.renderConfig.end() && lon->second == "1")
		{
			auto lj = m.renderConfig.find("render_live_terrain_journal");
			if(lj == m.renderConfig.end())
				Emit("sceneshot: NOTE: render_live_terrain=1 without its journal (the bundle's note says why): the load's field only");
			else
			{
				const c5r::Status ls = scene.liveTerrainReplayInit(lj->second);
				if(!ls.ok())
					return Fail(kReplayFailed, "live terrain: " + ls.str());
				liveReplay = true;
			}
		}
	}
	// ticket 0622 (format version 11): water_frames.bin names the water copy each frame was drawn with; the player loads the
	// copy of the span's first frame here and switches to another at the frame whose record names it (below, before the
	// frame's poll), through WaterScene::loadImage, i.e. the same useImage as the live poll (a same-load copy keeps the clock)
	std::vector<sb::WaterFrameRecord> waterFrames;
	std::map<std::string, std::string> waterPathBySha;   // a water_tap member's sha256 -> its path
	std::string waterSha;                               // the copy in use ("" none)
	const bool waterFull = so.water && so.waterMode == WaterSceneOptions::kFull;
	if(m.members.count("water_frames.bin"))
	{
		std::vector<uint8_t> b;
		sb::CheckResult r;
		if(!member("water_frames.bin", b) || !sb::ParseWaterFrames(b, &waterFrames, &r).ok())
			return Fail(kReplayFailed, "water_frames.bin: " + (why.empty() ? r.line() : why));
		if(waterFrames.size() != frames.size())
			return Fail(kReplayFailed, "water_frames.bin holds " + std::to_string(waterFrames.size()) + " records, frames.bin " +
			                               std::to_string(frames.size()));
		for(const auto& kv : m.members)
			if(kv.second.kind == sb::MemberKind::WaterTap)
				waterPathBySha[kv.second.sha256] = kv.first;
	}
	const bool waterPerFrame = waterFull && !waterFrames.empty();
	// the water image of the field's load (the last accepted copy; format 11: the copy of the span's first frame) and the clock
	// at the first frame (contract 6)
	bool waterLoaded = false;
	if(waterPerFrame)
	{
		const sb::WaterFrameRecord& w0 = waterFrames[0];
		if(w0.flags & sb::kWaterCopyInForce)
		{
			auto p = waterPathBySha.find(sb::Sha256HexOf(w0.wtapSha256));
			if(p == waterPathBySha.end())
				return Fail(kReplayFailed, "water_frames.bin: frame " + std::to_string(w0.frameId) + " names a water copy that is not a member");
			if(!LoadWaterImageMember(scene.water(), dir, m, p->second, &why))
				return Fail(kReplayFailed, "water image: " + why);
			waterSha = p->first;
			Emit("sceneshot: water image %s (copy %u) in use from frame %lld (water_frames.bin)", p->second.c_str(), w0.copySeq,
			     (long long)w0.frameId);
		}
		else
			Emit("sceneshot: NOTE: frame %lld was drawn with no water copy live (water_frames.bin): the scene draws its fallback until a "
			     "record names one", (long long)w0.frameId);
		if(!waterSha.empty() && !m.members.count("water_clock.bin"))
			return Fail(kReplayFailed, "water_clock.bin missing beside the water image: the coast waves cannot be redrawn (contract 6)");
		if(m.members.count("water_clock.bin"))
		{
			if(!LoadWaterClockMember(scene.water(), dir, m, &why))
				return Fail(kReplayFailed, "water clock: " + why);
			Emit("sceneshot: water_clock.bin in use (the clock of the span's first frame)");
		}
		waterLoaded = !waterSha.empty();
	}
	else if(waterFull && !waterMembers.empty())
	{
		std::string pick;
		uint32_t pickCopy = 0;
		for(const std::string& p : waterMembers)
		{
			std::vector<uint8_t> b;
			sb::WaterTapFile wf;
			sb::CheckResult r;
			if(!member(p, b) || !sb::ParseWaterTap(b, &wf, &r).ok())
				return Fail(kReplayFailed, p + ": " + (why.empty() ? r.line() : why));
			if(wf.block.loadSeq == fieldLoad && (pick.empty() || wf.block.copySeq >= pickCopy))
			{
				pick = p;
				pickCopy = wf.block.copySeq;
			}
		}
		if(pick.empty())
			Emit("sceneshot: NOTE: no water image of load seq %u among %zu water members: the scene draws its fallback", fieldLoad, waterMembers.size());
		else
		{
			if(!LoadWaterImageMember(scene.water(), dir, m, pick, &why))
				return Fail(kReplayFailed, "water image: " + why);
			if(!m.members.count("water_clock.bin"))
				return Fail(kReplayFailed, "water_clock.bin missing beside the water image: the coast waves cannot be redrawn (contract 6)");
			if(!LoadWaterClockMember(scene.water(), dir, m, &why))
				return Fail(kReplayFailed, "water clock: " + why);
			waterLoaded = true;
			Emit("sceneshot: water image %s (copy %u) and water_clock.bin in use", pick.c_str(), pickCopy);
		}
	}
	(void)waterLoaded;
	// issue 3911: FOWAnim's clock at the span's first frame (fog_clock.bin), then each frame's recorded nowMs
	if(so.fow)
	{
		if(!LoadFogClockMember(scene.fow(), dir, m, &why))
			return Fail(kReplayFailed, "fog clock: " + why);
		const FowClockState fc = scene.fow().clockState();
		Emit("sceneshot: fog_clock.bin in use: T %.6f s, last fog frame at %.3f ms", fc.clockS, fc.clockLastMs);
		// ticket 0403 (format version 6): the particles' state at the span's first frame (particles.bin); setClockState left
		// them off, setParticleState turns them on from the recorded state
		if(fowParticles && m.members.count("particles.bin"))
		{
			std::vector<uint8_t> b;
			sb::FogParticlesFile pf;
			sb::CheckResult r;
			if(!member("particles.bin", b) || !sb::ParseFogParticles(b, &pf, &r).ok())
				return Fail(kReplayFailed, "particles.bin: " + (why.empty() ? r.line() : why));
			FowParticleState ps;
			ps.spawned = (pf.flags & sb::kFogParticlesSpawned) != 0;
			ps.seed = pf.seed;
			ps.worldW = pf.worldW;
			ps.worldH = pf.worldH;
			ps.sim = std::move(pf.state);
			std::string pw;
			if(!scene.fow().setParticleState(ps, pw))
				return Fail(kReplayFailed, "particles.bin: the fog scene refused the recorded state: " + pw);
			Emit("sceneshot: particles.bin in use: %s, seed %u, world %.0f x %.0f, %u particles in a %d x %d grid", ps.spawned ? "spawned" : "not spawned",
			     ps.seed, ps.worldW, ps.worldH, pf.particles, pf.cellsX, pf.cellsY);
		}
		else if(fowParticles)
			Emit("sceneshot: NOTE: render_fow_particles=1 without particles.bin (the bundle's gap note says why): the replay draws no particles");
		else if(m.members.count("particles.bin"))
			Emit("sceneshot: NOTE: particles.bin not used (render_fow_particles 0 by --set): the replay draws no particles, not P1");
	}
	Targets tg;
	std::vector<ShotResult> results;
	bool anyDiffers = false, outMade = false;
	const std::wstring out = W(a.out);
	auto ensureOut = [&]() -> bool {
		if(outMade)
			return true;
		std::wstring o = out;
		std::replace(o.begin(), o.end(), L'/', L'\\');
		wchar_t full[MAX_PATH];
		if(!GetFullPathNameW(o.c_str(), MAX_PATH, full, nullptr))
			return false;
		const int r = SHCreateDirectoryExW(nullptr, full, nullptr);
		outMade = r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
		return outMade && DirExists(full);
	};
	const size_t lastK = shots.back().k;
	size_t next = 0;
	int64_t mismatches = 0;
	double shotMsSum = 0;
	for(size_t k = 0; k <= lastK; ++k)
	{
		const sb::FrameRecord& fr = frames[k];
		if(const int rc = feedUpTo(fr.loopIter, k > 0))
			return rc;
		if(ovlCtl)
			ovlCtl->seekFrame((uint32_t)k);
		if(shCtl)   // issue 4308
			shCtl->seekFrame((uint32_t)k);
		if(iceCtl)
			iceCtl->seekFrame((uint32_t)k);
		if(liveReplay)   // epic 0465, ticket 4907: the field generation this frame was drawn with (before the trees' restore at k 0)
		{
			const c5r::Status ls = scene.liveTerrainReplayTo(fr.frameId);
			if(!ls.ok())
				return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": live terrain: " + ls.str());
		}
		if(k == 0 && haveForestState && scene.forest())   // ticket 0495: the trees' state before the span's first draw
		{
			ForestSpanState fs;
			fs.sessionSeed = forestState.sessionSeed;
			fs.layoutSeed = forestState.layoutSeed;
			fs.loads = forestState.loads;
			fs.loadSeq = forestState.loadSeq;
			fs.idsResolved = (forestState.flags & sb::kForestIdsResolved) != 0;
			fs.forestId = forestState.forestId;
			fs.jungleId = forestState.jungleId;
			fs.streamState = forestState.streamState;
			fs.tableGen = forestState.tableGen;
			fs.tableW = forestState.tableW;
			fs.tableH = forestState.tableH;
			for(const sb::ForestStateRecord& r : forestState.records)
			{
				ForestSpanRecord o;
				o.x = r.x;
				o.y = r.y;
				o.kind = r.kind;
				o.layout = r.layout;
				fs.records.push_back(o);
			}
			fs.built = forestState.built;
			fs.marks = forestState.marks;
			fs.haveCut = (forestState.flags & sb::kForestHaveCut) != 0;
			fs.cutW = forestState.cutW;
			fs.cutH = forestState.cutH;
			fs.cutOx = forestState.cutOx;
			fs.cutOy = forestState.cutOy;
			fs.cutGeneration = forestState.cutGeneration;
			fs.cutCells = forestState.cutCells;
			std::string fw;
			if(!scene.forest()->restoreSpanState(fs, fw))
				return Fail(kReplayFailed, "forest.bin: the forest scene refused the recorded state: " + fw);
			Emit("sceneshot: forest.bin in use: %zu records, %zu built, %zu marks", fs.records.size(), fs.built.size(), fs.marks.size());
		}
		// ticket 0622 (format version 11): the water copy this frame was drawn with, taken where the live Scene::poll takes it
		// (WaterScene::poll -> useImage), before the frame's poll and draw
		if(waterPerFrame && (waterFrames[k].flags & sb::kWaterCopyInForce))
		{
			const sb::WaterFrameRecord& w = waterFrames[k];
			const std::string sha = sb::Sha256HexOf(w.wtapSha256);
			if(sha != waterSha)
			{
				auto p = waterPathBySha.find(sha);
				if(p == waterPathBySha.end())
					return Fail(kReplayFailed, "water_frames.bin: frame " + std::to_string(w.frameId) + " names a water copy that is not a member");
				if(!LoadWaterImageMember(scene.water(), dir, m, p->second, &why))
					return Fail(kReplayFailed, "frame " + std::to_string(w.frameId) + ": water image: " + why);
				waterSha = sha;
				Emit("sceneshot: water image %s (load %u copy %u) in use from frame %lld, as live (water_frames.bin)", p->second.c_str(),
				     w.loadSeq, w.copySeq, (long long)w.frameId);
			}
		}
		scene.poll();
		if(units && !a.unitsNoHistory && k == 0 && units->ready() != ((hist.flags & sb::kUnitsHistReady) != 0))   // issue 0440: the history's end state
			return Fail(kReplayFailed, std::string("units: ready() before the span's first frame is ") + (units->ready() ? "1" : "0") +
			                               " in the replay, " + ((hist.flags & sb::kUnitsHistReady) ? "1" : "0") + " live");
		const bool isShot = next < shots.size() && shots[next].k == k;
		SceneFrame f;
		f.cam = isShot ? shots[next].cam : cams[k];
		f.frameId = fr.frameId;
		f.nowMs = fr.nowMs;
		// issue 3911: decide()'s fog step (the live server drew this frame, so its fog admitted it)
		if(so.fow && !scene.fowAdmit(f.cam, fr.nowMs))
			return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": the fog held the frame (state " + FowScene::StateName(scene.fow().state()) +
			                               "; render: lines above), the live server drew it");
		if(!tg.ensure(d.dev, fr.width, fr.height, why))
			return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": " + why);
		const int64_t qs = Qpc();
		SettleResult sr;
		if(isShot)
		{
			const c5r::Status st = scene.settle(d.ctx, f, fr.width, fr.height, a.settleMs, &sr);
			if(!st.ok())
				return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": settle: " + st.str());
			if(!sr.settled)
				return Fail(sr.stopped ? kResidencyStopped : kSettleTimeout, "frame " + std::to_string(fr.frameId) + ": " + sr.what + " (no picture)");
		}
		if(k == 0 && haveNearState)   // ticket 0495: the near pairs as the live pool held them before this draw (near.bin)
		{
			ResidencySource* rs = static_cast<ResidencySource*>(scene.source());
			uint32_t pairs = 0;
			std::vector<c5r::bake::Residency::NearPairImage> imgs;   // the pairs whose content was behind their tile live
			for(const sb::NearPairImage& m : nearState.images)
			{
				c5r::bake::Residency::NearPairImage o;
				o.tile = m.tile;
				o.size = m.size;
				o.second = m.second;
				o.texels = m.texels;
				imgs.push_back(std::move(o));
			}
			const c5r::Status ns = rs ? rs->restoreNear(d.ctx, nearState.state.data(), nearState.state.size(), &pairs, &imgs)
			                          : c5r::Status::fail(c5r::Err::NotFound, "no residency");
			if(!ns.ok())
				return Fail(kReplayFailed, "near.bin: the residency refused the recorded state: " + ns.str());
			Emit("sceneshot: near.bin in use: %ux%u tiles, %u pairs (%zu textures from recorded texels, the rest baked)", nearState.tilesX,
			     nearState.tilesY, pairs, imgs.size());
		}
		// ticket 4883: the model module settled before the span's first draw. The player's model layer opens on a worker (the
		// library and its wire index: 18.3 s on the Tiny save) and its renderer is made at a later poll, while the residency's
		// settle can end first (3.8 s); the live server drew the span long after its layer was open, so frame 0 drawn before
		// it lacks every model (the first format-9 bundle: 46,824 colour pixels off in frame 0 only). Wait for the layer and
		// the renderer (bounded), then prepare every library file: the tap's records are the DROPPED ones, files live had made
		// drawable, so preparing them all draws what live drew.
		if(k == 0 && so.models.any() && scene.models())
		{
			srvmodels::ModelsScene* ms = scene.models();
			const int64_t q0 = Qpc();
			const double maxWaitMs = a.settleMs > 120000 ? (double)a.settleMs : 120000.0;
			int64_t polls = 0;
			for(;;)
			{
				std::string mwhy;
				if(ms->failed(mwhy))
					return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": models: " + mwhy);
				const srvmodels::ModelsCounters mc = ms->counters();
				if(mc.layerOpen && mc.deviceOk)
					break;
				if(QpcToMs(Qpc() - q0) > maxWaitMs)
					return Fail(kSettleTimeout, "frame " + std::to_string(fr.frameId) + ": models: the model layer was not open with its renderer made within " +
					                                std::to_string((long long)maxWaitMs) + " ms (layer open " + (mc.layerOpen ? "1" : "0") + ", device " +
					                                (mc.deviceOk ? "1" : "0") + "; no picture)");
				ms->poll(Qpc());
				polls++;
				if(!(ms->counters().layerOpen && ms->counters().deviceOk))
					Sleep(10);
			}
			const double waitedMs = QpcToMs(Qpc() - q0);
			const int64_t q1 = Qpc();
			ms->idle(1e12);   // the layer's prepare() stops only at the budget or after the last file
			Emit("sceneshot: models settled before frame %lld: layer open and renderer made after %.1f ms (%lld polls), every library file "
			     "prepared in %.1f ms (ticket 4883)", (long long)fr.frameId, waitedMs, (long long)polls, QpcToMs(Qpc() - q1));
		}
		const double settleMs = QpcToMs(Qpc() - qs);
		// ticket 4875: the model tap frame in force for this frame (modeltap/frame<NNNNNN>.mtap, the largest NNNNNN <= k)
		if(so.models.any() && !ApplyModelTapFrame(scene, dir, m, modelTaps, (uint32_t)k, &why))
			return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": models: " + why);
		c5r::terrain::FrameStats fs;
		const int64_t waterDraws0 = scene.water().drawsTotal, waveDraws0 = scene.water().waveDrawsTotal;
		const c5r::Status ds = scene.draw(d.ctx, tg.rtv, tg.dsv, fr.width, fr.height, f, fs);
		d.ctx->OMSetRenderTargets(0, nullptr, nullptr);
		if(!ds.ok())
			return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": draw: " + ds.str());
		// the recorded flags against the replay (3904 sets bit 1 when WaterScene::drawsTotal grew in the draw, bit 2 when
		// waveDrawsTotal grew): a frame the live scene drew otherwise cannot be equal. Reported and counted; the run then
		// ends with kReplayFailed after every shot's comparison, so that the numbers are there with their cause.
		const bool module = scene.water().drawsTotal > waterDraws0, waves = scene.water().waveDrawsTotal > waveDraws0;
		auto mismatch = [&](const char* what, bool live, bool here, const char* why2) {
			if(live == here)
				return;
			mismatches++;
			Emit("sceneshot: MISMATCH frame %lld: %s: live %d, replay %d%s%s", (long long)fr.frameId, what, live ? 1 : 0, here ? 1 : 0, why2[0] ? ": " : "",
			     why2);
		};
		mismatch("the water drawn by the module (flag bit 1)", (fr.flags & sb::kFrameWaterModule) != 0, module, WaterWhyName(scene.water().why()));
		mismatch("the coast waves drawn (flag bit 2)", (fr.flags & sb::kFrameWaves) != 0, waves, "");
		mismatch("an overlay bound", fr.overlayBound != 0, scene.lastOverlay().srv != nullptr, "");
		mismatch("the fog drawn (flag bit 3)", (fr.flags & sb::kFrameFog) != 0, scene.fow().terrainInputs().on, "");   // issue 3911
		if(k < shRecs.size())   // issue 4308
		{
			ShadowFrame got;
			const bool live = (shRecs[k].flags & (sb::kShadowBound | sb::kShadowQ0)) != 0;
			mismatch("a shadow triple bound (shadow.bin)", live, scene.lastShadowFrame(got), "");
			mismatch("a shadow map bound (shadow.bin)", (shRecs[k].flags & sb::kShadowBound) != 0, scene.lastShadow().srv != nullptr, "");
			mismatch("the ice drawn (shadow.bin)", (shRecs[k].flags & sb::kIceDrawn) != 0, scene.lastIceDrawn(), "");
		}
		if(!isShot)
			continue;
		const Shot& s = shots[next++];
		ShotResult res;
		res.name = s.name;
		res.frameId = fr.frameId;
		res.w = fr.width;
		res.h = fr.height;
		res.settle = sr.what;
		res.settleMs = settleMs;
		std::vector<uint8_t> colour, depth;
		if(!ReadBack(d.dev, d.ctx, tg.colour, colour, why) || !ReadBack(d.dev, d.ctx, tg.depth, depth, why))
			return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": readback: " + why);
		res.colourSha = sb::Sha256Hex(colour.data(), colour.size());
		res.depthSha = sb::Sha256Hex(depth.data(), depth.size());
		// the comparison before any PNG (contract 2.8)
		std::vector<uint8_t> delta;
		if(a.compare)
		{
			std::vector<uint8_t> bmp, dep;
			const std::string cp = "dumps/" + std::to_string(fr.frameId) + ".bmp", dp = "dumps/" + std::to_string(fr.frameId) + ".depth.f32";
			why.clear();
			if(!member(cp, bmp) || !member(dp, dep))
				return Fail(kNoDump, "frame " + std::to_string(fr.frameId) + ": " + (why.empty() ? "no dumps" : why));
			uint32_t bw = 0, bh = 0;
			sb::CheckResult r;
			if(!sb::ParseDumpColour(bmp, &bw, &bh, &r).ok() || bw != fr.width || bh != fr.height || dep.size() != depth.size())
				return Fail(kReplayFailed, "frame " + std::to_string(fr.frameId) + ": the dumps are not of the frame's size: " + r.line());
			res.compared = true;
			res.cd = CompareColour(colour.data(), bmp.data() + 54, (size_t)bw * bh, a.diff ? &delta : nullptr);
			res.dd = CompareDepth(depth.data(), dep.data(), depth.size() / 4);
			const bool eq = res.cd.equal() && res.dd.equal();
			const bool within = res.dd.equal() && res.cd.differing <= a.tolPixels && res.cd.maxDelta <= a.tolDelta;
			res.withinTolerance = within;
			anyDiffers = anyDiffers || !within;
			if(!eq && within)
				Emit("sceneshot: NOTE: frame %lld differs within --tolerance %llu:%d", (long long)fr.frameId, (unsigned long long)a.tolPixels, a.tolDelta);
			Emit("sceneshot: COMPARE frame %lld %ux%u: colour %s (%llu of %llu pixels differ, %llu in alpha; largest channel difference %d; "
			     "per pixel median %d p95 %d, over the differing median %d p95 %d), depth %s (%llu of %llu values differ, %llu in coverage, max abs %.3g)",
			     (long long)fr.frameId, bw, bh, res.cd.equal() ? "EQUAL" : "DIFFERS", (unsigned long long)res.cd.differing,
			     (unsigned long long)res.cd.pixels, (unsigned long long)res.cd.alphaDiffering, res.cd.maxDelta, res.cd.medianAll, res.cd.p95All,
			     res.cd.medianDiff, res.cd.p95Diff, res.dd.equal() ? "EQUAL" : "DIFFERS", (unsigned long long)res.dd.differing,
			     (unsigned long long)res.dd.values, (unsigned long long)res.dd.coverage, res.dd.maxAbs);
			for(size_t i : res.cd.first)
			{
				const uint8_t* o = &colour[4 * i];
				const uint8_t* l = bmp.data() + 54 + 4 * i;
				float zo, zl;
				memcpy(&zo, &depth[4 * i], 4);
				memcpy(&zl, &dep[4 * i], 4);
				Emit("sceneshot: DIFF frame %lld pixel (%zu, %zu): live BGRA %u %u %u %u, offline %u %u %u %u; depth live %.9g offline %.9g",
				     (long long)fr.frameId, i % bw, i / bw, l[0], l[1], l[2], l[3], o[0], o[1], o[2], o[3], zl, zo);
			}
			if(!sameAdapter)
				Emit("sceneshot: NOTE: frame %lld compared on another adapter than the bundle's: not P1", (long long)fr.frameId);
		}
		if(!ensureOut())
			return Fail(kWriteFailed, "cannot create --out '" + Fwd(a.out) + "'");
		const std::wstring stem = out + L"\\" + W(s.name);
		if(!a.noPictures)
		{
			if(!WritePng(stem + L".png", fr.width, fr.height, colour.data(), why))
				return Fail(kWriteFailed, Fwd(N(stem)) + ".png: " + why);
			if(!WriteFileBytes(stem + L".depth.f32", depth.data(), depth.size()))
				return Fail(kWriteFailed, Fwd(N(stem)) + ".depth.f32");
			res.png = Fwd(N(stem)) + ".png";
			res.depth = Fwd(N(stem)) + ".depth.f32";
		}
		if(a.diff && res.compared && !res.cd.equal())
		{
			const std::vector<uint8_t> img = DiffImage(colour.data(), delta);   // the offline picture's gray where equal
			if(!WritePng(stem + L".diff.png", fr.width, fr.height, img.data(), why))
				return Fail(kWriteFailed, Fwd(N(stem)) + ".diff.png: " + why);
			res.diff = Fwd(N(stem)) + ".diff.png";
		}
		res.shotMs = QpcToMs(Qpc() - qs);
		shotMsSum += res.shotMs;
		Emit("sceneshot: SHOT %s frame %lld %ux%u colour %s depth %s; %s; settle+draw+readback+write %.1f ms (an indication, not a timing: plan 7)",
		     s.name.c_str(), (long long)fr.frameId, fr.width, fr.height, res.colourSha.c_str(), res.depthSha.c_str(), sr.what.c_str(), res.shotMs);
		results.push_back(res);
	}
	// 8. the result file (3906 reads it) and the last line
	if(!ensureOut())
		return Fail(kWriteFailed, "cannot create --out '" + Fwd(a.out) + "'");
	{
		std::string j = "{\n  \"tool\": \"sceneshot\",\n  \"bundle_id\": \"" + JsonEscape(m.bundleId) + "\",\n  \"bundle\": \"" + JsonEscape(Fwd(a.bundle)) +
		                "\",\n  \"adapter\": \"" + JsonEscape(d.description) + "\",\n  \"adapter_luid\": \"" + d.luid + "\",\n  \"same_adapter\": " +
		                (sameAdapter ? "true" : "false") + ",\n  \"install\": \"" + JsonEscape(Fwd(N(install))) + "\",\n  \"shots\": [";
		for(size_t i = 0; i < results.size(); ++i)
		{
			const ShotResult& r = results[i];
			char b[1600];
			sprintf_s(b,
			          "%s\n    {\"name\": \"%s\", \"frame_id\": %lld, \"width\": %u, \"height\": %u, \"colour_sha256\": \"%s\", \"depth_sha256\": \"%s\", "
			          "\"compared\": %s, \"within_tolerance\": %s, \"colour_equal\": %s, \"colour_differing\": %llu, \"colour_alpha_differing\": %llu, \"colour_max_delta\": %d, "
			          "\"colour_median\": %d, \"colour_p95\": %d, \"colour_median_differing\": %d, \"colour_p95_differing\": %d, \"depth_equal\": %s, "
			          "\"depth_differing\": %llu, \"depth_coverage\": %llu, \"depth_max_abs\": %.9g, \"settle_ms\": %.3f, \"shot_ms\": %.3f, "
			          "\"png\": \"%s\", \"depth\": \"%s\", \"diff\": \"%s\"}",
			          i ? "," : "", JsonEscape(r.name).c_str(), (long long)r.frameId, r.w, r.h, r.colourSha.c_str(), r.depthSha.c_str(),
			          r.compared ? "true" : "false", r.compared && r.withinTolerance ? "true" : "false", r.compared && r.cd.equal() ? "true" : "false", (unsigned long long)r.cd.differing,
			          (unsigned long long)r.cd.alphaDiffering, r.cd.maxDelta, r.cd.medianAll, r.cd.p95All, r.cd.medianDiff, r.cd.p95Diff,
			          r.compared && r.dd.equal() ? "true" : "false", (unsigned long long)r.dd.differing, (unsigned long long)r.dd.coverage, r.dd.maxAbs,
			          r.settleMs, r.shotMs, JsonEscape(r.png).c_str(), JsonEscape(r.depth).c_str(), JsonEscape(r.diff).c_str());
			j += b;
		}
		j += "\n  ]\n}\n";
		const std::wstring jp = out + L"\\sceneshot.json";
		if(!WriteFileBytes(jp, j.data(), j.size()))
			return Fail(kWriteFailed, Fwd(N(jp)));
	}
	Emit("sceneshot: replayed %zu frames (%lld drains, %lld field loads) in %.1f ms; %zu shots, %.1f ms in the shots", lastK + 1, (long long)drains,
	     (long long)(baselines ? baselines : 1), QpcToMs(Qpc() - t0), results.size(), shotMsSum);
	if(so.tiles == SceneOptions::kTilesBake)
		if(ResidencySource* rs = static_cast<ResidencySource*>(scene.source()))
		{
			const c5r::bake::ResidencyStats* st = rs->stats();
			Emit("sceneshot: residency: feed plots changed %lld, passed (re-baked) %lld, filtered %lld; bakes %lld, re-bakes %lld, failures %lld",
			     (long long)rs->plotsChanged, (long long)rs->plotsPassed, (long long)rs->plotsFiltered, st ? (long long)st->bakes : -1LL,
			     st ? (long long)st->rebakes : -1LL, st ? (long long)st->failures : -1LL);
		}
	if(scene.roads())   // ticket 4934: the road producer's totals
	{
		char rl[1024];
		scene.roads()->totalsLine(rl, sizeof(rl));
		Emit("sceneshot: %s", rl);
	}
	if(FrameJobs* fj = scene.frameJobs())   // ticket 0711 (R8): which path each forked begin took (the determinism proof's counts)
	{
		const FrameJobs::Counts jc = fj->counts();
		Emit("sceneshot: frame jobs: %d workers, jitter %d: forks %llu, worker runs %llu, inline runs %llu, failures %llu, misuses %llu; "
		     "owner violations %llu",
		     fj->workers(), fj->jitter() ? 1 : 0, (unsigned long long)jc.forks, (unsigned long long)jc.workerRuns,
		     (unsigned long long)jc.inlineRuns, (unsigned long long)jc.failures, (unsigned long long)jc.misuses,
		     (unsigned long long)FrameJobsOwnerViolations());
	}
	{   // ticket 0753 (J3): how many draws executed a list recorded ahead (the rule A arm's proof that the path was taken)
		const SceneRecordAheadCounters& rc2 = scene.recordAheadCounters();
		Emit("sceneshot: record ahead: %s, driver lists %d: lists %lld, not forked %lld, finish failed %lld, dropped %lld, timed %lld; "
		     "terrain lists %lld, mid lists %lld, no prediction %lld, mispredicted %lld",
		     scene.recordAheadOn() ? "on" : "off", scene.recordAheadDriverLists() ? 1 : 0, (long long)rc2.lists, (long long)rc2.notForked,
		     (long long)rc2.finishFailed, (long long)rc2.dropped, (long long)rc2.timedLists, (long long)rc2.terrainLists,
		     (long long)rc2.midLists, (long long)rc2.noPrediction, (long long)rc2.mispredicted);
	}
	if(mismatches)
		return Fail(kReplayFailed, std::to_string(mismatches) + " recorded frame flag(s) the replay did not reproduce (MISMATCH lines above)" +
		                               (anyDiffers ? "; offline does not equal live either (COMPARE lines)" : ""));
	if(anyDiffers)
		return Fail(kDiffers, "offline does not equal live in at least one frame (COMPARE lines above)" +
		                          (a.tolPixels || a.tolDelta ? " beyond --tolerance " + std::to_string(a.tolPixels) + ":" + std::to_string(a.tolDelta) : std::string()));
	{   // ticket 0614: a claim made while the replay read the instance makes its result untrusted
		const std::string w = claimedSources();
		if(!w.empty())
			return Fail(kInstall, "during the replay: " + w);
	}
	bool allEqual = true;
	for(const ShotResult& r : results)
		allEqual = allEqual && r.cd.equal() && r.dd.equal();
	Emit("sceneshot: OK %zu shot(s)%s into %s", results.size(),
	     !a.compare ? "" : allEqual ? ", each byte-equal to the server's own dump" : ", each within --tolerance of the server's own dump", Fwd(a.out).c_str());
	return kOk;
}

}  // namespace sceneshot

#ifndef SCENESHOT_NO_MAIN
static void PrintLine(void*, const char* line)
{
	printf("%s\n", line);
	fflush(stdout);
}

int main(int argc, char** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	std::vector<std::string> args;
	for(int i = 1; i < argc; ++i)
		args.push_back(argv[i]);
	return sceneshot::Run(args, &PrintLine, nullptr);
}
#endif
