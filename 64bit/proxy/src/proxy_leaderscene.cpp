// proxy_leaderscene.cpp - 3D leader scenes for majors above slot 21 (ticket 0381). See proxy_leaderscene.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Written from the knowledge base's description of the EXE's behaviour (research/02-exe-internals/
// leader-scene-player-limit.md; decision D83): no EXE code byte is read, compared or written; the EXE's scene cache is
// read as data and the EXE's own engine-utility method is called through its interface.
// Toolchain: clang-cl -m32, VC9 headers (the proxy's); offline test: the same and MSVC /fsanitize=address, with
// VP_LEADERSCENE_OFFLINE defined (no fork header, no engine glue). x86 only (the cache's pointers are 32 bits).
#ifndef VP_LEADERSCENE_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way
#include "proxy_ifaces.gen.h"   // EngHook_/EngFwd_ of GameplayDiplomacyAILeaderMessage (generated declarations)
#include "proxy_rt_logic.h"
#include "proxy_local.h"        // ProxyIniSettings (proxy_update.cpp)
#include "vp64_options.h"       // ticket 0745: [Proxy] LeaderScenes (ProxyRtInt::ProxyOptions)
#endif
#include "proxy_leaderscene.h"
#include "proxy_exe.h"  // ticket 4963: the build table (lsc_scene_map)
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace ProxyLeaderScene
{
namespace
{
// ================================================================================================ the cache read
// POD only: the function runs inside __try (no unwinding objects).
int WalkRaw(uintptr_t mapObj, uint32_t* pSize, uint64_t* pKeys)
{
	const uint32_t head = *reinterpret_cast<const volatile uint32_t*>(mapObj + kMapHeadOff);
	const uint32_t size = *reinterpret_cast<const volatile uint32_t*>(mapObj + kMapSizeOff);
	*pSize = size;
	*pKeys = 0;
	if (head < 0x10000 || size > (uint32_t)kMaxKeys) return kReadBad;
	if (*reinterpret_cast<const volatile uint8_t*>(head + 0x15) != 1) return kReadBad;  // the head node is the nil node
	uint32_t stack[kMaxKeys];
	int sp = 0;
	uint32_t node = *reinterpret_cast<const volatile uint32_t*>(head + 4);  // the root
	uint32_t count = 0, steps = 0;
	uint64_t keys = 0;
	for (;;)
	{
		while (node != head)
		{
			if (node < 0x10000 || sp >= kMaxKeys || ++steps > 4u * kMaxKeys) return kReadBad;
			if (*reinterpret_cast<const volatile uint8_t*>(node + 0x15) != 0) return kReadBad;
			stack[sp++] = node;
			node = *reinterpret_cast<const volatile uint32_t*>(node);  // left
		}
		if (sp == 0) break;
		node = stack[--sp];
		const int32_t key = *reinterpret_cast<const volatile int32_t*>(node + 0xC);
		if (key < 0 || key >= kMaxKeys) return kReadBad;
		const uint64_t bit = (uint64_t)1 << key;
		if ((keys & bit) != 0 || ++count > (uint32_t)kMaxKeys) return kReadBad;
		keys |= bit;
		node = *reinterpret_cast<const volatile uint32_t*>(node + 8);  // right
	}
	if (count != size) return kReadBad;
	*pKeys = keys;
	return kReadOk;
}

// POD only (runs inside __try). *pBuild, *pStamp, *pSoi: what the header says; *pRva: the cache's RVA in that build.
int CheckExeRaw(uintptr_t base, int* pBuild, uint32_t* pStamp, uint32_t* pSoi, uint32_t* pRva)
{
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return 1;
	const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return 1;
	*pStamp = nt->FileHeader.TimeDateStamp;
	*pSoi = nt->OptionalHeader.SizeOfImage;
	// ticket 4963: the build from the registry (stamp and SizeOfImage), the cache's address from its table
	*pBuild = VpExeBuildFromHeader(*pStamp, *pSoi);
	if (*pBuild == kVpExeBuildUnknown) return 2;
	const uint32_t rva = ProxyExe::Rva(*pBuild, VPX_lsc_scene_map);
	if (rva == 0) return 6;
	*pRva = rva;
	const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	const unsigned n = nt->FileHeader.NumberOfSections;
	if (n > 96) return 1;
	for (unsigned i = 0; i < n; ++i)
	{
		const uint32_t va = sec[i].VirtualAddress;
		const uint32_t len = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
		if (rva < va || rva + 12 > va + len) continue;
		const DWORD c = sec[i].Characteristics;
		if ((c & IMAGE_SCN_MEM_WRITE) == 0 || (c & IMAGE_SCN_MEM_READ) == 0 || (c & IMAGE_SCN_MEM_EXECUTE) != 0) return 4;
		return 0;
	}
	return 5;
}

// "1-21 30 33-35" for a key set (into buf).
void FormatKeys(uint64_t keys, char* buf, size_t cap)
{
	size_t at = 0;
	buf[0] = '\0';
	for (int k = 0; k < kMaxKeys; ++k)
	{
		if (((keys >> k) & 1) == 0) continue;
		int e = k;
		while (e + 1 < kMaxKeys && ((keys >> (e + 1)) & 1) != 0) ++e;
		char part[16];
		if (e > k)
			_snprintf_s(part, sizeof(part), _TRUNCATE, "%s%d-%d", at ? " " : "", k, e);
		else
			_snprintf_s(part, sizeof(part), _TRUNCATE, "%s%d", at ? " " : "", k);
		const size_t pl = strlen(part);
		if (at + pl + 1 >= cap) break;
		memcpy(buf + at, part, pl + 1);
		at += pl;
		k = e;
	}
	if (!at) _snprintf_s(buf, cap, _TRUNCATE, "none");
}
}  // namespace

int ReadCache(uintptr_t mapObj, CacheRead& out)
{
	uint32_t size = 0;
	uint64_t keys = 0;
	int rc;
	__try
	{
		rc = WalkRaw(mapObj, &size, &keys);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		rc = kReadFault;
	}
	out.size = size;
	out.keys = rc == kReadOk ? keys : 0;
	return rc;
}

uint32_t SceneMapRva(int build)
{
	return ProxyExe::Rva(build, VPX_lsc_scene_map);
}

const char* CheckExe(uintptr_t base, uint32_t* sceneMapRva)
{
	static char s_why[200];
	if (base < 0x10000) return "off: no EXE image";
	int rc, build = kVpExeBuildUnknown;
	uint32_t stamp = 0, soi = 0, rva = 0;
	__try
	{
		rc = CheckExeRaw(base, &build, &stamp, &soi, &rva);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		rc = 1;
	}
	switch (rc)
	{
	case 0:
		if (sceneMapRva) *sceneMapRva = rva;
		return NULL;
	case 2: return ProxyExe::UnknownBuild(s_why, sizeof(s_why), "off: ", stamp, soi);
	case 6: return ProxyExe::NotCarried(s_why, sizeof(s_why), "off: ", build, VPX_lsc_scene_map);  // logged LOUD
	case 4: return "off: the scene cache's address is not in a writable data section of the EXE";
	case 5: return "off: no section of the EXE holds the scene cache's address";
	default: return "off: no EXE image";
	}
}

// ================================================================================================ the mode
namespace
{
std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
	return s.substr(a, b - a);
}

// 0, 1, 2 from "0", "1", "2" (surrounding blanks allowed); -1 otherwise.
int ModeValue(const std::string& v)
{
	const std::string t = Trim(v);
	if (t == "0") return kModeOff;
	if (t == "1") return kModeDemand;
	if (t == "2") return kModeAll;
	return -1;
}

const char* ModeName(int m)
{
	return m == kModeDemand ? "1 (on demand)" : m == kModeAll ? "2 (all at load)" : "0 (off)";
}

// vpproxy.ini's leader_scenes: -2 when no line that is not a comment sets it, -1 for a value other than 0, 1, 2 (raw = the
// value as written), else the mode. The last occurrence wins, as in ParseSettings. Ticket 0745: DecideOnce asks it to
// know whether the ini's test switch is there before it takes [Proxy] LeaderScenes of VP64Options.ini.
int IniValue(const char* iniText, std::string& raw)
{
	int found = -2;
	raw.clear();
	if (!iniText) return found;
	const std::string text(iniText);
	size_t at = 0;
	while (at <= text.size())
	{
		size_t nl = text.find('\n', at);
		if (nl == std::string::npos) nl = text.size();
		const std::string line = Trim(text.substr(at, nl - at));
		at = nl + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos || Trim(line.substr(0, eq)) != "leader_scenes") continue;
		raw = line.substr(eq + 1);
		found = ModeValue(raw);
	}
	return found;
}
}  // namespace

int DecideMode(const char* envValue, const char* iniText, std::string& why)
{
	if (envValue)
	{
		const int m = ModeValue(envValue);
		if (m < 0)
		{
			why = "VPPROXY_LEADER_SCENES='" + std::string(envValue) + "' is not 0, 1 or 2: off";
			return kModeOff;
		}
		why = std::string("VPPROXY_LEADER_SCENES (test switch): ") + ModeName(m);
		return m;
	}
	std::string raw;
	const int found = IniValue(iniText, raw);
	if (found == -2)
	{
		why = "vpproxy.ini has no leader_scenes key: 0 (off, the default)";
		return kModeOff;
	}
	if (found < 0)
	{
		why = "vpproxy.ini leader_scenes='" + Trim(raw) + "' is not 0, 1 or 2: off";
		return kModeOff;
	}
	why = std::string("vpproxy.ini leader_scenes = ") + ModeName(found);
	return found;
}

bool DemandWants(int player, int readResult, uint64_t keys)
{
	if (player < kFirstSlot || player >= kCivPlayers) return false;  // 0-21: the EXE's own; 63: the barbarians
	if (readResult == kReadOk && ((keys >> player) & 1) != 0) return false;  // loaded already
	return true;
}

// ================================================================================================ the driver
Driver::Driver()
{
	m_off = false;
	m_walked = false;
	m_evaluated = false;
	m_badLogged = false;
	m_lastWalk = 0;
	m_lastEval = 0;
	m_lastKeys = 0;
	m_lastSize = 0;
	m_evalKeys = 0;
	m_generation = 1;
	m_faults = 0;
	m_notifies = 0;
	m_evals = 0;
	for (int i = 0; i < kMaxKeys; ++i)
	{
		m_attempts[i] = 0;
		m_lastNotify[i] = 0;
		m_gaveUp[i] = false;
		m_minorKnown[i] = false;
		m_minor[i] = false;
		m_minorCiv[i] = -1;
	}
}

void Driver::Logf(Env& env, const char* fmt, ...)
{
	char line[600];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	env.Log(line);
}

void Driver::NewGeneration(Env& env, const CacheRead& c)
{
	char was[200], now[200];
	FormatKeys(m_lastKeys, was, sizeof(was));
	FormatKeys(c.keys, now, sizeof(now));
	++m_generation;
	for (int i = 0; i < kMaxKeys; ++i)
	{
		m_attempts[i] = 0;
		m_lastNotify[i] = 0;
		m_gaveUp[i] = false;
		m_minorKnown[i] = false;
	}
	m_evaluated = false;
	Logf(env, "leaderscene: the EXE's scene cache lost entries (players %s -> %s): a new game session, generation %u",
	     was, now, (unsigned)m_generation);
}

void Driver::Tick(Env& env)
{
	if (m_off) return;
	const double now = env.NowMs();
	if (m_walked && now - m_lastWalk < kWalkEveryMs) return;
	m_walked = true;
	m_lastWalk = now;

	CacheRead c;
	const int rc = env.ReadCache(c);
	if (rc == kReadFault)
	{
		++m_faults;
		Logf(env, "leaderscene: reading the EXE's scene cache faulted (%u of %d allowed)", (unsigned)m_faults, kMaxFaults);
		if (m_faults >= (uint32_t)kMaxFaults)
		{
			m_off = true;
			Logf(env, "leaderscene: off for this process: the scene cache could not be read");
		}
		return;
	}
	if (rc != kReadOk)
	{
		if (!m_badLogged)
		{
			m_badLogged = true;
			Logf(env, "leaderscene: the EXE's scene cache did not pass the checks (size %u); skipped until it does (logged "
			     "once)", (unsigned)c.size);
		}
		return;
	}
	if ((m_lastKeys & ~c.keys) != 0) NewGeneration(env, c);
	m_lastKeys = c.keys;
	m_lastSize = c.size;
	if (c.size == 0) return;  // the EXE has loaded no scene in this session (yet): nothing to complete

	const bool changed = !m_evaluated || c.keys != m_evalKeys;
	if (!changed && now - m_lastEval < kEvalEveryMs) return;
	if (!env.BeginEval()) return;
	m_evaluated = true;
	m_lastEval = now;
	m_evalKeys = c.keys;
	++m_evals;

	const int active = env.ActivePlayer();
	uint64_t asked = 0, gaveUp = 0;
	for (int i = kFirstSlot; i < kCivPlayers; ++i)
	{
		if (((c.keys >> i) & 1) != 0) continue;  // loaded (by us, by the DLL's own call, or by the EXE)
		if (m_gaveUp[i]) continue;
		if (m_attempts[i] > 0 && now - m_lastNotify[i] < kRetryAfterMs) continue;  // asked; the EXE may still be loading
		SlotFacts f;
		f.exists = f.everAlive = f.human = false;
		f.civ = -1;
		if (!env.Facts(i, f)) continue;
		if (!f.exists || !f.everAlive || f.human || i == active) continue;
		bool minor;
		if (m_minorKnown[i] && m_minorCiv[i] == f.civ)
			minor = m_minor[i];
		else
		{
			minor = env.IsMinor(i);
			m_minorKnown[i] = true;
			m_minor[i] = minor;
			m_minorCiv[i] = f.civ;
		}
		if (minor) continue;
		if (m_attempts[i] >= kMaxAttempts)
		{
			m_gaveUp[i] = true;
			gaveUp |= (uint64_t)1 << i;
			continue;
		}
		env.Notify(i);
		++m_attempts[i];
		m_lastNotify[i] = now;
		++m_notifies;
		asked |= (uint64_t)1 << i;
	}
	env.EndEval();
	if (asked)
	{
		char cache[200], ask[200];
		FormatKeys(c.keys, cache, sizeof(cache));
		FormatKeys(asked, ask, sizeof(ask));
		Logf(env, "leaderscene: the EXE's scene cache holds %u scenes (players %s); asked the EXE to load players %s "
		     "(NotifySpecificAILeaderInGame); generation %u", (unsigned)c.size, cache, ask, (unsigned)m_generation);
	}
	if (gaveUp)
	{
		char g[200];
		FormatKeys(gaveUp, g, sizeof(g));
		Logf(env, "leaderscene: players %s still have no scene after %d requests; not asked again in generation %u", g,
		     kMaxAttempts, (unsigned)m_generation);
	}
}
}  // namespace ProxyLeaderScene

#ifndef VP_LEADERSCENE_OFFLINE
// ================================================================================================ the engine glue
extern "C" ICvGameContext1* DllGetGameContext();  // proxy_rt.cpp: after the boot, the context wrapper

static_assert(MAX_CIV_PLAYERS == ProxyLeaderScene::kCivPlayers, "MAX_CIV_PLAYERS is the same in both DLL builds");

namespace
{
class EngineEnv : public ProxyLeaderScene::Env
{
public:
	EngineEnv() : m_base(0), m_g(NULL), m_pg(NULL) {}
	void SetBase(uintptr_t base) { m_base = base; }
	double NowMs() { return ProxyRtInt::Now() * 1000.0; }
	int ReadCache(ProxyLeaderScene::CacheRead& out)
	{
		return ProxyLeaderScene::ReadCache(m_base, out);  // m_base: the cache (ticket 4963)
	}
	bool BeginEval()
	{
		if (!ProxyRtInt::EngineUtility()) return false;
		ICvGameContext1* ctx = DllGetGameContext();
		if (!ctx) return false;
		m_g = ctx->GetGame();
		if (!m_g) return false;
		if (!m_g->IsFinalInitialized())
		{
			m_g->Destroy();
			m_g = NULL;
			return false;
		}
		m_pg = ctx->GetPreGame();
		return true;
	}
	int ActivePlayer() { return (int)m_g->GetActivePlayer(); }
	bool Facts(int slot, ProxyLeaderScene::SlotFacts& f)
	{
		ICvPlayer1* p = m_g->GetPlayer((PlayerTypes)slot);
		f.exists = p != NULL;
		if (!p) return true;
		f.everAlive = p->IsEverAlive();
		f.human = f.everAlive && p->IsHuman();
		f.civ = m_pg ? (int)m_pg->civilization((PlayerTypes)slot) : -2;
		p->Destroy();
		return true;
	}
	bool IsMinor(int slot)
	{
		ICvPlayer1* p = m_g->GetPlayer((PlayerTypes)slot);
		if (!p) return true;  // no player: never asked
		const bool minor = p->IsMinorCiv() || p->IsBarbarian();
		p->Destroy();
		return minor;
	}
	void Notify(int slot)
	{
		ICvEngineUtility1* eu = ProxyRtInt::EngineUtility();
		if (eu) eu->NotifySpecificAILeaderInGame((PlayerTypes)slot);
	}
	void EndEval()
	{
		if (m_pg) m_pg->Destroy();
		if (m_g) m_g->Destroy();
		m_pg = NULL;
		m_g = NULL;
	}
	void Log(const char* line) { ProxyRtInt::Log("%s", line); }

private:
	uintptr_t m_base;
	ICvGame1* m_g;
	ICvPreGame1* m_pg;
};

EngineEnv g_env;
ProxyLeaderScene::Driver g_driver;
volatile LONG g_decideState = 0;  // 0 undecided, 1 deciding, 2 decided (the hook can come on the UI or the core thread)
int g_mode = ProxyLeaderScene::kModeOff;
uintptr_t g_base = 0;
volatile LONG g_demandAsks = 0;

bool ReadText(const std::string& path, std::string& out)
{
	FILE* f = NULL;
	if (path.empty() || fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	char buf[4096];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

void DecideOnce()
{
	if (g_decideState == 2) return;
	if (InterlockedCompareExchange(&g_decideState, 1, 0) != 0)
	{
		while (g_decideState != 2) Sleep(0);  // another thread is deciding (a few file reads)
		return;
	}
	char env[16] = {};
	const DWORD n = GetEnvironmentVariableA("VPPROXY_LEADER_SCENES", env, sizeof(env));
	const ProxyLogic::Settings& s = ProxyIniSettings();
	std::string ini;
	const bool haveIni = s.iniFound && ReadText(s.iniPath, ini);
	std::string why;
	const char* envValue = n > 0 && n < sizeof(env) ? env : NULL;
	std::string raw;
	int mode;
	if (!envValue && ProxyLeaderScene::IniValue(haveIni ? ini.c_str() : NULL, raw) == -2)
	{
		// ticket 0745 (D255 G4): neither test switch is set: [Proxy] LeaderScenes of VP64Options.ini decides (0 = off,
		// the default, as before the key; its range 0..2 is the table's, vp64_options.cpp)
		const Vp64Opt::Options& o = ProxyRtInt::ProxyOptions();
		mode = o.LeaderScenes();
		char b[160];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "[Proxy] LeaderScenes = %d (%s; vpproxy.ini has no leader_scenes key)", mode,
		            o.fromFile[Vp64Opt::kLeaderScenes] ? "from the file" : "default");
		why = b;
		if (mode != ProxyLeaderScene::kModeDemand && mode != ProxyLeaderScene::kModeAll) mode = ProxyLeaderScene::kModeOff;
	}
	else
		mode = ProxyLeaderScene::DecideMode(envValue, haveIni ? ini.c_str() : NULL, why);
	if (mode != ProxyLeaderScene::kModeOff)
	{
		const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
		uint32_t rva = 0;  // ticket 4963: the scene cache's RVA in the running build (lsc_scene_map)
		const char* refused = ProxyLeaderScene::CheckExe(base, &rva);
		if (refused)
		{
			// ticket 4963: a symbol the build's table does not carry is LOUD; the other refusals keep their quiet line
			ProxyRtInt::Log("%sleaderscene: %s; %s; majors above slot 21 get no 3D leader scene (the EXE's own loop)",
			                strstr(refused, "not carried") ? "LOUD: " : "", why.c_str(), refused);
			mode = ProxyLeaderScene::kModeOff;
		}
		else
		{
			g_base = base + rva;  // the cache itself
			g_env.SetBase(base + rva);
			ProxyRtInt::Log("leaderscene: on: %s; the EXE's scene cache at 0x%08lx; %s (ticket 0381)", why.c_str(),
			                (unsigned long)(base + rva),
			                mode == ProxyLeaderScene::kModeDemand
			                    ? "a major from slot 22 up gets its 3D scene when its leader screen opens"
			                    : "every AI major from slot 22 up is asked for its 3D scene at load");
		}
	}
	else
		ProxyRtInt::Log("leaderscene: off: %s", why.c_str());
	g_mode = mode;
	InterlockedExchange(&g_decideState, 2);
}
}  // namespace

void ProxyLeaderScene::OnUpdate()
{
	DecideOnce();
	if (g_mode == kModeAll) g_driver.Tick(g_env);
}

// Mode 1. ICvEngineUtility1::GameplayDiplomacyAILeaderMessage is how the DLL opens every AI leader screen: the AI's own
// messages and the human's (Lua Players[i]:DoBeginDiploWithHuman from the diplomacy list, the city banner, the
// notifications, the relationship and league screens; the espionage and discussion variants); the trade screen with an
// AI opens inside that leader view (research 7.10). The hook asks the EXE for the player's scene first, then forwards the
// message unchanged: both are posts on the EXE's game-to-UI table, handled in order on its UI thread, so the scene is in
// the cache when the message's handler (0x6EC820) looks it up [I: the order is checked live, research 7.10].
// Asking makes nested calls (the EXE's NotifySpecificAILeaderInGame asks the DLL about the player), which invalidate the
// channel payload, so the rest of the arguments is copied first (ProxyRtInt::CopyRest, as proxy_lua.cpp does).
bool EngHook_ICvEngineUtility4_GameplayDiplomacyAILeaderMessage(ICvEngineUtility4* self, vpipc::Reader& args,
                                                                vpipc::Writer& ret, vpipc::Channel& ch)
{
	DecideOnce();
	if (g_mode == ProxyLeaderScene::kModeDemand)
	{
		vpipc::Reader peek = args;
		const int32_t player = peek.i32();
		if (peek.ok())
		{
			ProxyLeaderScene::CacheRead c;
			const int rc = ProxyLeaderScene::ReadCache(g_base, c);
			if (ProxyLeaderScene::DemandWants(player, rc, c.keys))
			{
				vpipc::Reader copy;
				uint8_t* buf = ProxyRtInt::CopyRest(args, copy);
				static_cast<ICvEngineUtility1*>(self)->NotifySpecificAILeaderInGame((PlayerTypes)player);
				const LONG k = InterlockedIncrement(&g_demandAsks);
				ProxyRtInt::Log("leaderscene: player %d opens a leader screen without a scene (cache %u, read %d): asked "
				                "the EXE for it first (on demand, request %ld)", (int)player, (unsigned)c.size, rc, k);
				const bool ok = EngFwd_ICvEngineUtility4_GameplayDiplomacyAILeaderMessage(self, copy, ret, ch);
				free(buf);
				return ok;
			}
		}
	}
	return EngFwd_ICvEngineUtility4_GameplayDiplomacyAILeaderMessage(self, args, ret, ch);
}
#endif  // VP_LEADERSCENE_OFFLINE
