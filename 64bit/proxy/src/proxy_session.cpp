// proxy_session.cpp - the session's masks, the session rule and the helper's start (issues 3521, 3523; decisions D119,
// D125): see proxy_session.h. Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#include "proxy_session.h"
#include "proxy_session_keys.h"
#include "vp_exe_symbols.h"  // ticket 4963: VpExeBuildSupported, the symbols (exebuild/include + the emitted header)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ProxySession
{
namespace
{
// research/02-exe-internals/disable-systems-and-masks.md section 2 (bit order of the EXE's name table).
const char* const kKeys[kKeyCount] = {"Terrain",    "LandmarkSystem", "Overlay",      "OverlayDebug", "Decal",
                                      "City",       "Unit",           "Forest",       "River",        "LeaderHead",
                                      "Audio",      "Combat",         "Particle",     "Projectile",   "FOW",
                                      "FluidFOW",   "YieldIcons",     "DistanceFog",  "StrategicView", "Minimap",
                                      "ColorKeyEdit", "Waves",        "Arrows",       "MovementPath"};

// The EXE's layout: g_Civ5App (cam_app) and the GameViewState vtable (ses_gvs_vtbl) come from the build table (ticket
// 4963, exebuild/symbols); the structure offsets are the same in both builds (0582 B.2) and stay here, the same values
// as proxy_shell.cpp's Mask namespace.
const DWORD APP_OFF_GVS = 0xC0;
const DWORD APP_OFF_CFG = 0x940;
const DWORD GVS_OFF_VIEW = 0x1B4;

bool Readable(const void* p, size_t n)
{
	const uintptr_t a = (uintptr_t)p;
	if (a < 0x10000 || a + n < a) return false;
	uintptr_t at = a;
	while (at < a + n)
	{
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery((const void*)at, &mi, sizeof(mi)) != sizeof(mi)) return false;
		if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((mi.Protect & readable) == 0) return false;
		const uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize;
		if (end <= at) return false;
		at = end;
	}
	return true;
}

bool ReadDword(uintptr_t a, DWORD* v)
{
	if (!Readable((const void*)a, 4)) return false;
	*v = *(const DWORD*)a;
	return true;
}

bool ParseNumber(const char* s, uint32_t* v)
{
	if (!s || !*s) return false;
	char* end = NULL;
	const unsigned long x = strtoul(s, &end, 0);
	if (!end || *end) return false;
	*v = (uint32_t)x;
	return true;
}

std::string Hex8(uint32_t v)
{
	char b[16];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%08X", v);
	return b;
}
}  // namespace

// ================================================================================================ keys
const char* KeyName(unsigned bit) { return bit < (unsigned)kKeyCount ? kKeys[bit] : NULL; }

int BitOfKey(const char* key)
{
	if (!key) return -1;
	for (int i = 0; i < kKeyCount; ++i)
		if (_stricmp(key, kKeys[i]) == 0) return i;
	return -1;
}

std::string ClearedKeys(uint32_t mask)
{
	std::string s;
	for (unsigned b = 0; b < (unsigned)kKeyCount; ++b)
		if (!(mask & (1u << b)))
		{
			if (!s.empty()) s += ",";
			s += kKeys[b];
		}
	return s;
}

// ================================================================================================ the masks
const char* MaskSourceName(MaskSource s)
{
	switch (s)
	{
	case kMaskExe: return "the EXE";
	case kMaskTest: return "VPPROXY_TEST_CONFIG_MASK (test only)";
	case kMaskConfigIni: return "config.ini";
	default: return "none";
	}
}

// ================================================================================================ the EXE build
int ExeBuildOfImage(const void* base, uint32_t* stamp, uint32_t* sizeOfImage)
{
	if (stamp) *stamp = 0;
	if (sizeOfImage) *sizeOfImage = 0;
	// The registry's reader never reads past the byte count it is given; give it only what VirtualQuery says is readable:
	// the DOS header first, then up to the end of the optional header's SizeOfImage field.
	if (!Readable(base, 0x40)) return kVpExeBuildUnknown;
	DWORD e = 0;
	memcpy(&e, (const char*)base + 0x3C, 4);
	if (e == 0 || e > 0x10000) return kVpExeBuildUnknown;
	const uint32_t need = e + 4 + 20 + 60;
	if (!Readable(base, need)) return kVpExeBuildUnknown;
	return VpExeBuildOfImage(base, need, stamp, sizeOfImage);
}

bool IsKnownExe(const void* base, uint32_t* stamp)
{
	return ExeBuildOfImage(base, stamp, NULL) != kVpExeBuildUnknown;
}

bool DevReadyBuild(int id)
{
	const VpExeBuildInfo* info = VpExeBuildInfoOf(id);
	if (!info || VpExeBuildProductReady(id)) return false;
	char v[32];
	const DWORD n = GetEnvironmentVariableA("VP64_DEV_READY_BUILD", v, sizeof(v));
	return n > 0 && n < sizeof(v) && strcmp(v, info->steamBuild) == 0;
}

bool ExeBuildSupported(int id)
{
	// The product may run the 64-bit path on the build (vp_exe_build.h VpExeBuildProductReady, the lead's flag; or the
	// developer switch VP64_DEV_READY_BUILD naming it) and this binary's table for it is complete (VpExeBuildSupported).
	return (VpExeBuildProductReady(id) || DevReadyBuild(id)) && VpExeBuildSupported(id);
}

bool IsGameExeName(const std::wstring& baseName)
{
	static const wchar_t kHead[] = L"civilizationv";
	const size_t nh = sizeof(kHead) / sizeof(kHead[0]) - 1;
	if (baseName.size() < nh + 4 || _wcsnicmp(baseName.c_str(), kHead, nh) != 0) return false;
	return _wcsicmp(baseName.c_str() + baseName.size() - 4, L".exe") == 0;
}

bool ParseTestExeBuild(const char* s, uint32_t* stamp, uint32_t* sizeOfImage)
{
	if (!s) return false;
	const char* comma = strchr(s, ',');
	if (!comma || comma == s) return false;
	const std::string a(s, comma - s);
	uint32_t ts = 0, soi = 0;
	if (!ParseNumber(a.c_str(), &ts) || !ParseNumber(comma + 1, &soi)) return false;
	*stamp = ts;
	*sizeOfImage = soi;
	return true;
}

ExeBuild ReadExeBuild()
{
	ExeBuild b;
	wchar_t path[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
	std::wstring base = (n > 0 && n < MAX_PATH * 2) ? std::wstring(path, n) : std::wstring();
	const size_t slash = base.find_last_of(L"\\/");
	if (slash != std::wstring::npos) base.erase(0, slash + 1);
	b.exeName = Utf8(base);
	char env[96];
	const DWORD ne = GetEnvironmentVariableA("VPPROXY_TEST_EXE_BUILD", env, sizeof(env));
	if (ne > 0 && ne < sizeof(env) && ParseTestExeBuild(env, &b.stamp, &b.sizeOfImage))
	{
		b.test = true;
		b.gameExe = true;
		b.id = VpExeBuildFromHeader(b.stamp, b.sizeOfImage);
	}
	else
	{
		b.gameExe = IsGameExeName(base);
		b.id = ExeBuildOfImage(GetModuleHandleW(NULL), &b.stamp, &b.sizeOfImage);
	}
	b.devReady = DevReadyBuild(b.id);
	b.supported = ExeBuildSupported(b.id);
	return b;
}

std::string ExeBuildText(const ExeBuild& b)
{
	char line[200];
	const VpExeBuildInfo* info = VpExeBuildInfoOf(b.id);
	if (info)
		_snprintf_s(line, sizeof(line), _TRUNCATE, "build %s (stamp 0x%08X, SizeOfImage 0x%08X)", info->steamBuild, b.stamp,
		            b.sizeOfImage);
	else
		_snprintf_s(line, sizeof(line), _TRUNCATE, "an unknown build (stamp 0x%08X, SizeOfImage 0x%08X)", b.stamp,
		            b.sizeOfImage);
	return line;
}

// ================================================================================================ config.ini as a mask
bool ConfigMaskOfIni(const std::string& text, uint32_t* mask, std::string* why)
{
	*mask = 0xFFFFFFFFu;
	size_t pos = (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
	              (unsigned char)text[2] == 0xBF)
	                 ? 3
	                 : 0;
	bool in = false, seen = false;
	std::string off;
	while (pos < text.size())
	{
		size_t e = text.find('\n', pos);
		if (e == std::string::npos) e = text.size();
		std::string ln = text.substr(pos, e - pos);
		pos = e + 1;
		const size_t c = ln.find_first_of(";#");
		if (c != std::string::npos) ln.erase(c);
		const size_t b0 = ln.find_first_not_of(" \t\r");
		if (b0 == std::string::npos) continue;
		ln = ln.substr(b0, ln.find_last_not_of(" \t\r") - b0 + 1);
		if (ln[0] == '[')
		{
			in = _stricmp(ln.c_str(), "[DisableSystems]") == 0;
			seen = seen || in;
			continue;
		}
		if (!in) continue;
		const size_t eq = ln.find('=');
		if (eq == std::string::npos) continue;
		std::string key = ln.substr(0, eq), val = ln.substr(eq + 1);
		const size_t ke = key.find_last_not_of(" \t");
		key = ke == std::string::npos ? std::string() : key.substr(0, ke + 1);
		const size_t vb = val.find_first_not_of(" \t");
		val = vb == std::string::npos ? std::string() : val.substr(vb);
		const int bit = BitOfKey(key.c_str());
		if (bit < 0) continue;
		const long v = strtol(val.c_str(), NULL, 0);
		if (v != 0)
			*mask &= ~(1u << bit);
		else
			*mask |= 1u << bit;  // a later line of the same key decides
	}
	if (!seen)
	{
		*why = "config.ini has no [DisableSystems] section (the EXE's defaults: every system on)";
		return false;
	}
	*why = "config.ini's [DisableSystems]";
	return true;
}

int ReadConfigIniMask(uint32_t* mask, std::string* why)
{
	*mask = 0xFFFFFFFFu;
	std::wstring path;
	if (!ConfigIniPath(&path))
	{
		*why = "the Documents folder (and so config.ini) cannot be found";
		return -1;
	}
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		char b[120];
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
		{
			*why = "no config.ini (the EXE's defaults: every system on)";
			return 0;
		}
		_snprintf_s(b, sizeof(b), _TRUNCATE, "config.ini cannot be opened (Windows error %lu)", err);
		*why = b;
		return -1;
	}
	std::string text;
	char buf[8192];
	DWORD got = 0;
	bool ok = true;
	while (text.size() < (1u << 20))
	{
		if (!ReadFile(h, buf, sizeof(buf), &got, NULL))
		{
			ok = false;
			break;
		}
		if (got == 0) break;
		text.append(buf, got);
	}
	CloseHandle(h);
	if (!ok)
	{
		*why = "config.ini cannot be read";
		return -1;
	}
	ConfigMaskOfIni(text, mask, why);
	return 1;
}

Masks ReadExeMasks(uintptr_t base, int build)
{
	Masks m;
	char line[300];
	m.knownExe = true;
	m.gameExe = true;
	const uintptr_t appAt = VpExeAddr(build, VPX_cam_app, base);
	const uintptr_t vtbl = VpExeAddr(build, VPX_ses_gvs_vtbl, base);
	if (!appAt || !vtbl)
	{
		// a supported build carries every symbol; this is a table without them: fail loud (D33 assumed)
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: symbol %s not carried to this build (no mask read)",
		            VpExeBuildLabel(build), !appAt ? VpExeSymName(VPX_cam_app) : VpExeSymName(VPX_ses_gvs_vtbl));
		m.why = line;
		return m;
	}
	DWORD app = 0, cfg = 0;
	if (!ReadDword(appAt, &app) || app == 0 || !ReadDword(app + APP_OFF_CFG, &cfg))
	{
		_snprintf_s(line, sizeof(line), _TRUNCATE,
		            "the known EXE build, but g_Civ5App (0x%08lX) or its config mask cannot be read", app);
		m.why = line;
		return m;
	}
	m.src = kMaskExe;
	m.config = cfg;
	DWORD gvs = 0, vt = 0, view = 0;
	if (ReadDword(app + APP_OFF_GVS, &gvs) && gvs && ReadDword(gvs, &vt) && vt == (DWORD)vtbl &&
	    ReadDword(gvs + GVS_OFF_VIEW, &view))
	{
		m.haveView = true;
		m.view = view;
	}
	_snprintf_s(line, sizeof(line), _TRUNCATE, "g_Civ5App 0x%08lX [+0x940]; view mask %s", app,
	            m.haveView ? "[GameViewState+0x1B4]" : "not readable (no GameViewState yet)");
	m.why = line;
	return m;
}

Masks ReadMasks()
{
	Masks m;
	char line[300];
	uint32_t stamp = 0;
	const void* exe = GetModuleHandleW(NULL);
	const ExeBuild eb = ReadExeBuild();
	m.gameExe = eb.gameExe;
	stamp = eb.stamp;
	// The EXE's own masks: on the real image of a supported build, through its table (ticket 4963). The test switch never
	// reads the EXE (its values are not this image's).
	if (!eb.test && eb.supported)
	{
		Masks e = ReadExeMasks((uintptr_t)exe, eb.id);
		e.gameExe = eb.gameExe;
		return e;
	}
	// Why the EXE's masks were not read, in one clause.
	std::string notRead;
	if (!eb.gameExe)
	{
		_snprintf_s(line, sizeof(line), _TRUNCATE, "not a Civilization V EXE (%s, timestamp 0x%08X)", eb.exeName.c_str(),
		            stamp);
		notRead = line;
	}
	else if (!eb.supported)
		notRead = "unsupported EXE build 0x" + std::string(Hex8(eb.stamp)) + " (" + ExeBuildText(eb) +
		          (eb.id == kVpExeBuildUnknown ? ", not a build the patch knows" : ", not supported by this version of the patch yet") +
		          (eb.test ? ", VPPROXY_TEST_EXE_BUILD" : "") + ")";
	else
		notRead = "EXE " + ExeBuildText(eb) + ": no mask addresses for this build in this binary" +
		          (eb.test ? " (VPPROXY_TEST_EXE_BUILD)" : "");
	char buf[64];
	const DWORD n = GetEnvironmentVariableA("VPPROXY_TEST_CONFIG_MASK", buf, sizeof(buf));
	if (n > 0 && n < sizeof(buf) && ParseNumber(buf, &m.config))
	{
		m.src = kMaskTest;
		const DWORD nv = GetEnvironmentVariableA("VPPROXY_TEST_VIEW_MASK", buf, sizeof(buf));
		if (nv > 0 && nv < sizeof(buf) && ParseNumber(buf, &m.view)) m.haveView = true;
		m.why = notRead + ": the test masks";
		return m;
	}
	if (eb.gameExe)
	{
		// Ticket 0588: never "stock (assumed)" in a game EXE: config.ini's keys, as the EXE read them at its start.
		std::string cw;
		uint32_t cm = 0;
		const int r = ReadConfigIniMask(&cm, &cw);
		if (r >= 0)
		{
			m.src = kMaskConfigIni;
			m.config = cm;
			m.why = notRead + ": the config mask from " + cw;
			return m;
		}
		m.why = notRead + ": no masks read (" + cw + ")";
		return m;
	}
	m.why = notRead + ": no masks read";
	return m;
}

// ================================================================================================ the player's folder
bool ConfigIniPath(std::wstring* out)
{
	wchar_t env[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableW(L"VPPROXY_TEST_CONFIG_INI", env, MAX_PATH * 2);
	if (n > 0 && n < MAX_PATH * 2)
	{
		*out = std::wstring(env, n);
		return true;
	}
	// A harness test instance names its Documents in C5_DOCUMENTS (the in-process shim redirects SHGetFolderPathW to
	// it anyway; this makes the path the same as 3520's helper resolves outside the EXE).
	const DWORD nd = GetEnvironmentVariableW(L"C5_DOCUMENTS", env, MAX_PATH * 2);
	if (nd > 0 && nd < MAX_PATH * 2)
	{
		*out = std::wstring(env, nd) + L"\\My Games\\Sid Meier's Civilization 5\\config.ini";
		return true;
	}
	typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
	HMODULE sh = LoadLibraryW(L"shell32.dll");
	PFN f = sh ? (PFN)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
	wchar_t w[MAX_PATH];
	if (!f || f(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, w) != S_OK) return false;
	*out = std::wstring(w) + L"\\My Games\\Sid Meier's Civilization 5\\config.ini";
	return true;
}

// ================================================================================================ the session rule
const std::vector<const char*>& OurKeys()
{
	static const char* const kOur[] = {VP64_D33_KEYS_A2};
	static const std::vector<const char*> v(kOur, kOur + sizeof(kOur) / sizeof(kOur[0]));
	return v;
}

uint32_t BitsOfKeys(const std::vector<const char*>& keys, std::string* unknown)
{
	uint32_t bits = 0;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		const int b = BitOfKey(keys[i]);
		if (b >= 0)
			bits |= 1u << b;
		else if (unknown)
		{
			if (!unknown->empty()) *unknown += ",";
			*unknown += keys[i] ? keys[i] : "(null)";
		}
	}
	return bits;
}

uint32_t StockOffBits()
{
	static const char* const kOff[] = {VP64_STOCK_OFF_KEYS};
	return BitsOfKeys(std::vector<const char*>(kOff, kOff + sizeof(kOff) / sizeof(kOff[0])), NULL);
}

uint32_t RuleBits() { return BitsOfKeys(OurKeys(), NULL) & ~StockOffBits(); }

Kind Classify(uint32_t configMask, uint32_t ruleBits) { return (configMask & ruleBits) == ruleBits ? kStock : kD33; }

const char* KindName(Kind k) { return k == kStock ? "stock" : "D33"; }

Kind SessionKind(const Masks& m, std::string* why)
{
	char line[300];
	const uint32_t rule = RuleBits();
	if (m.src == kMaskExe || m.src == kMaskTest || m.src == kMaskConfigIni)
	{
		const Kind k = Classify(m.config, rule);
		std::string off;
		for (unsigned b = 0; b < (unsigned)kKeyCount; ++b)
			if ((rule & (1u << b)) && !(m.config & (1u << b)))
			{
				if (!off.empty()) off += ",";
				off += kKeys[b];
			}
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s session: config mask 0x%08X, rule bits 0x%08X, %s", KindName(k),
		            m.config, rule, off.empty() ? "every rule key on" : ("rule keys off: " + off).c_str());
		*why = line;
		if (m.src == kMaskConfigIni) *why += " (" + m.why + ")";  // ticket 0588: names the build and why
		return k;
	}
	if (m.knownExe)
	{
		*why = "D33 session (assumed): the known EXE build, but its config mask cannot be read (" + m.why + "); fail loud";
		return kD33;
	}
	if (m.gameExe)
	{
		// Ticket 0588: a game EXE whose masks are not read and whose config.ini cannot be read either: never assumed stock
		// (a 32-bit game in the A'' form would draw no terrain).
		*why = "D33 session (assumed): " + m.why + "; fail loud";
		return kD33;
	}
	*why = "stock session (assumed): not a game EXE and no test mask (" + m.why + ")";
	return kStock;
}

// ================================================================================================ the build gate
const char* ConfigFormName(ConfigForm f)
{
	switch (f)
	{
	case kFormStock: return "stock";
	case kFormA2: return "A''";
	default: return "unknown (unreadable)";
	}
}

const char* BootActionName(BootAction a)
{
	switch (a)
	{
	case kBootGo: return "go";
	case kBootOff32: return "the 32-bit DLL, no server";
	default: return "no play";
	}
}

BootGate DecideBoot(const BootGateIn& in)
{
	BootGate g;
	char line[600];
	ExeBuild eb;
	eb.id = in.build;
	eb.stamp = in.stamp;
	eb.sizeOfImage = in.sizeOfImage;
	const std::string build = ExeBuildText(eb);
	if (!in.pathOn)
	{
		g.action = kBootOff32;
		g.why = "exe build: mode=off or failed=1: the 32-bit DLL as configured; the EXE build is not checked";
		return g;
	}
	if (!in.gameExe)
	{
		g.why = "exe build: not a Civilization V EXE (a test or development host): the build gate does not apply";
		return g;
	}
	const bool known = VpExeBuildInfoOf(in.build) != NULL;
	if (known && in.supported)
	{
		g.why = "exe build: " + build + ": known and supported: the 64-bit path starts";
		if (in.devReady)
			g.why += " (DEVELOPER switch VP64_DEV_READY_BUILD names this build, which the product does not support yet: "
			         "treated as ready; never in a player's install)";
		return g;
	}
	g.code = known ? "unsupported-build" : "unknown-build";
	const char* what = known ? "a build this version of the 64-bit patch does not support yet"
	                         : "not a build the 64-bit patch knows";
	char head[300];
	_snprintf_s(head, sizeof(head), _TRUNCATE,
	            "Civilization V was updated: this game's EXE is %s (stamp 0x%08X, SizeOfImage 0x%08X), which this version "
	            "of the 64-bit patch does not support yet. The 64-bit patch is off until a version that supports this "
	            "build is installed",
	            known ? ("Steam build " + std::string(VpExeBuildInfoOf(in.build)->steamBuild)).c_str() : "a new build",
	            in.stamp, in.sizeOfImage);
	// D263 (C), ticket 0775: the update link names the fork's releases page until upstream merges the pull request
	// (amends D260 for this link only; the error popup's bug-report link stays upstream's issues)
	static const char kWhere[] =
		" Look for an update of the patch on its page, https://github.com/artemigkh/Community-Patch-DLL/releases, or where you got it.";
	if (in.form != kFormStock)
	{
		g.action = kBootNoPlay;
		g.report = true;
		g.remember = true;
		g.exitCode = 3;
		_snprintf_s(line, sizeof(line), _TRUNCATE,
		            "exe build: %s: %s; config.ini %s: no play (D125: the 32-bit game would draw no terrain): the report, "
		            "vp64helper failed (it restores config.ini after the game's end), the end of the game (exit code 3)",
		            build.c_str(), what, ConfigFormName(in.form));
		g.why = line;
		g.summary = std::string(head) +
		            ". This start was ended because the patch's settings in config.ini would have left the game without "
		            "terrain; the next start is the normal 32-bit Vox Populi game." +
		            kWhere;
		return g;
	}
	g.action = kBootOff32;
	g.report = !in.noticeShown;
	g.remember = !in.noticeShown;
	g.notice = "the 64-bit patch is off: Civilization V was updated to a build it does not support yet";
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "exe build: %s: %s; config.ini stock: the server is not started, the 32-bit DLL (a normal 32-bit game); %s",
	            build.c_str(), what,
	            in.noticeShown ? "the notice for this build was shown before: no report, no popup"
	                           : "the first start on this build: the report and vp64helper popup");
	g.why = line;
	if (g.report) g.summary = std::string(head) + "; until then the game runs as the normal 32-bit Vox Populi game." + kWhere;
	return g;
}

std::wstring NoticeFilePath(const std::wstring& configIni)
{
	const size_t k = configIni.find_last_of(L"\\/");
	if (k == std::wstring::npos) return std::wstring();
	return configIni.substr(0, k) + L"\\VP64\\exe-build-notices.txt";
}

namespace
{
std::string NoticeLine(uint32_t stamp, uint32_t sizeOfImage)
{
	char b[40];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "0x%08X 0x%08X", stamp, sizeOfImage);
	return b;
}

bool ReadSmall(const std::wstring& file, std::string* out)
{
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	char buf[4096];
	DWORD got = 0;
	bool ok = true;
	while (out->size() < 65536)
	{
		if (!ReadFile(h, buf, sizeof(buf), &got, NULL))
		{
			ok = false;
			break;
		}
		if (got == 0) break;
		out->append(buf, got);
	}
	CloseHandle(h);
	return ok;
}
}  // namespace

bool NoticeShown(const std::wstring& file, uint32_t stamp, uint32_t sizeOfImage)
{
	std::string t;
	if (file.empty() || !ReadSmall(file, &t)) return false;
	const std::string want = NoticeLine(stamp, sizeOfImage);
	size_t pos = 0;
	while (pos < t.size())
	{
		size_t e = t.find('\n', pos);
		if (e == std::string::npos) e = t.size();
		std::string ln = t.substr(pos, e - pos);
		pos = e + 1;
		while (!ln.empty() && (ln[ln.size() - 1] == '\r' || ln[ln.size() - 1] == ' ')) ln.erase(ln.size() - 1);
		if (_stricmp(ln.c_str(), want.c_str()) == 0) return true;
	}
	return false;
}

bool RememberNotice(const std::wstring& file, uint32_t stamp, uint32_t sizeOfImage, std::string* line)
{
	const std::string rec = NoticeLine(stamp, sizeOfImage);
	if (file.empty())
	{
		*line = "exe build: the notice record has no path (no Documents folder): not remembered; the notice comes again";
		return false;
	}
	if (NoticeShown(file, stamp, sizeOfImage))
	{
		*line = "exe build: " + rec + " already in " + Utf8(file);
		return true;
	}
	const size_t k = file.find_last_of(L"\\/");
	if (k != std::wstring::npos) CreateDirectoryW(file.substr(0, k).c_str(), NULL);  // VP64 (its parent is config.ini's)
	HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		char b[160];
		_snprintf_s(b, sizeof(b), _TRUNCATE, " cannot be written (Windows error %lu): not remembered; the notice comes again",
		            GetLastError());
		*line = "exe build: " + Utf8(file) + b;
		return false;
	}
	const std::string data = rec + "\r\n";
	DWORD put = 0;
	const BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, NULL);
	CloseHandle(h);
	if (!ok || put != data.size())
	{
		*line = "exe build: writing " + Utf8(file) + " failed: not remembered; the notice comes again";
		return false;
	}
	*line = "exe build: " + rec + " remembered in " + Utf8(file) + " (no second notice for this build)";
	return true;
}

// ================================================================================================ the helper
std::wstring HelperCommandLine(const std::wstring& helper, const std::wstring& verb, const std::wstring& reportDir,
                               const std::wstring& optionsIni)
{
	// A trailing backslash before the closing quote would escape it (CommandLineToArgvW): the folder has none.
	std::wstring dir = reportDir;
	while (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')) dir.erase(dir.size() - 1);
	std::wstring cmd = L"\"" + helper + L"\" " + verb + L" --report \"" + dir + L"\"";
	if (!optionsIni.empty()) cmd += L" --options \"" + optionsIni + L"\"";
	return cmd;
}

std::wstring VerbFor(bool d33, unsigned long pid, const std::wstring& configIni)
{
	if (!d33) return L"popup";
	wchar_t b[48];
	_snwprintf_s(b, 48, _TRUNCATE, L"failed --pid %lu", pid);
	std::wstring v = b;
	if (!configIni.empty()) v += L" --config \"" + configIni + L"\"";
	return v;
}

bool StartHelper(const std::wstring& proxyDir, const std::wstring& verb, const std::wstring& reportDir,
                 const std::wstring& optionsIni, std::string* line)
{
	const std::wstring helper = proxyDir + L"\\" + VP64HELPER_FILE;
	const std::string helperU8 = Utf8(helper);
	char m[200];
	const DWORD attr = GetFileAttributesW(helper.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		*line = "helper: " + helperU8 + " does not exist: not started (no popup; the report and the log line remain)";
		return false;
	}
	const std::wstring cmd = HelperCommandLine(helper, verb, reportDir, optionsIni);
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	const DWORD base = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
	// Out of the EXE's job when the job allows it (a job that ends its processes with the EXE would take the helper).
	BOOL ok = CreateProcessW(helper.c_str(), &buf[0], NULL, NULL, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, NULL,
	                         proxyDir.c_str(), &si, &pi);
	DWORD err = ok ? 0 : GetLastError();
	const bool breakaway = ok != FALSE;
	if (!ok && err == ERROR_ACCESS_DENIED)
	{
		ok = CreateProcessW(helper.c_str(), &buf[0], NULL, NULL, FALSE, base, NULL, proxyDir.c_str(), &si, &pi);
		err = ok ? 0 : GetLastError();
	}
	if (!ok)
	{
		_snprintf_s(m, sizeof(m), _TRUNCATE, "CreateProcessW error %lu", err);
		*line = "helper: " + helperU8 + " could not be started: " + m;
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	_snprintf_s(m, sizeof(m), _TRUNCATE, "pid %lu (%s)", pi.dwProcessId,
	            breakaway ? "out of the EXE's job" : "the EXE's job allows no breakaway: inside it");
	*line = "helper: started " + Utf8(cmd) + ": " + m;
	return true;
}

// ================================================================================================ text
std::wstring Widen(const std::string& s)
{
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

std::string Utf8(const std::wstring& s)
{
	if (s.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
	std::string u(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &u[0], n, NULL, NULL);
	return u;
}
}  // namespace ProxySession
