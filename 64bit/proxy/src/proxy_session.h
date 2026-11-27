// proxy_session.h - what the error report of issue 3521 needs to know about the session, and the popup helper's start
// (decisions D119, D124: research/07-architecture-decisions/architect-2026-11-14-failure-popup-and-report.md and
// architect-2026-11-14-always-boot-stock.md). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// With vpproxy.ini pack=1 (the shipped layout), every failure of the 64-bit path (the pack check, the server's start,
// the handshake or schema hash, a server that dies in a game) writes the local report (proxy_report.h) and starts
// vp64helper.exe from the proxy's folder (issue 3520's helper). Then (D125, issue 3523; replaces D124's "always the
// 32-bit continuation", 3521):
//   - a D33 session (config.ini in the A'' form: a rule bit of the config mask clear) never plays: vp64helper failed
//     --pid <EXE pid>, then the game ends as on_server_death=exit ends it (exit code 3);
//   - a stock session: vp64helper popup, then at the boot the 32-bit DLL with 3504's notice, in a game on_server_death.
// vpproxy.ini failed=1 (the helper writes it after a D33 failure) makes the boot behave as mode=off.
// No engine, no vpipc: proxy/tools/test_proxy_session.py unit-tests this file (rt_tests/session_unit.cpp).
#pragma once
// The STL layout must match the other proxy TUs (see proxy_rt_logic.h).
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
// The one registry of EXE builds (epic 0590, ticket 0588). Relative, so every unit that includes this header compiles
// without the build table's include path; proxy_session.cpp itself includes vp_exe_symbols.h (the generated table).
#include "../../exebuild/include/vp_exe_build.h"

namespace ProxySession
{
// ================================================================================================ keys
enum { kKeyCount = 24 };
// The [DisableSystems] key of a mask bit (Terrain = bit 0 .. MovementPath = bit 23; research/02-exe-internals/
// disable-systems-and-masks.md section 2); NULL past bit 23.
const char* KeyName(unsigned bit);
// The bit of a key (case-insensitive), or -1.
int BitOfKey(const char* key);
// "OverlayDebug,ColorKeyEdit": the keys whose bit is clear in mask ("" when none).
std::string ClearedKeys(uint32_t mask);

// ================================================================================================ the EXE build
// Ticket 0588 (epic 0590 child 4961, decision D202): which EXE build this process is, from the one registry
// exebuild/include/vp_exe_build.h (PE TimeDateStamp and SizeOfImage together), never from a stamp of this file's own.
// The build id of the image at base (kVpExeBuild* or kVpExeBuildUnknown); *stamp and *sizeOfImage (each may be NULL) get
// the header's values (0 when the image is not a readable 32-bit PE). Never faults (VirtualQuery before every read).
int ExeBuildOfImage(const void* base, uint32_t* stamp, uint32_t* sizeOfImage);
// True when the image at base is one of the registry's builds; *stamp as above.
bool IsKnownExe(const void* base, uint32_t* stamp);
// "Supported" = the product may run the 64-bit path on the build (vp_exe_build.h VpExeBuildProductReady: the lead of
// epic 0590 flips a build once its carry and live smokes passed; today 4390913 only) AND this binary's table for it is
// complete (vp_exe_symbols.h VpExeBuildSupported). A known build that is not supported takes ticket 0588's path, named
// ("Steam build 26577625 ... does not support yet").
bool ExeBuildSupported(int id);
// The DEVELOPER switch VP64_DEV_READY_BUILD=<Steam build id> (exactly, e.g. 26577625): a known build that is not product
// ready counts as ready, so the carriers' tests and live smokes (tickets 4963-4967) can run the 64-bit path on it before
// the lead flips it. Read from the EXE's environment (the harness sets it for an instance's launch; nothing of the patch's
// install, its options files or the installer sets it); every boot that uses it says so in the log and the report. False
// for an unknown build, a build already ready, or any other value.
bool DevReadyBuild(int id);
// True for a Civilization V game EXE's file name (CivilizationV*.exe, any case: DX11, DX9, Tablet); false for a test or
// development host (the rt_tests loader, a stand-in), where the build gate does not apply.
bool IsGameExeName(const std::wstring& baseName);
// The test switch VPPROXY_TEST_EXE_BUILD=<stamp>,<SizeOfImage> (numbers as strtoul base 0, e.g. "0x12345678,0x06519000"):
// the build gate and the masks then take these values instead of the process image's, in any process (the process counts
// as a game EXE). Tests only: a player never sets it; the boot log says when it is set. False when s is not that form.
bool ParseTestExeBuild(const char* s, uint32_t* stamp, uint32_t* sizeOfImage);
struct ExeBuild
{
	int id;                // kVpExeBuild* or kVpExeBuildUnknown
	uint32_t stamp;        // PE TimeDateStamp (0 if unreadable)
	uint32_t sizeOfImage;  // SizeOfImage (0 if unreadable)
	bool gameExe;          // a Civilization V EXE (IsGameExeName), or the test switch is set
	bool test;             // the values came from VPPROXY_TEST_EXE_BUILD
	bool supported;        // ExeBuildSupported(id)
	bool devReady;         // DevReadyBuild(id): supported only through the developer switch
	std::string exeName;   // the process image's file name (UTF-8)
	ExeBuild() : id(-1), stamp(0), sizeOfImage(0), gameExe(false), test(false), supported(false), devReady(false) {}
};
// The process's build: GetModuleHandleW(NULL) (or the test switch).
ExeBuild ReadExeBuild();
// "build 26577625 (stamp 0x6AF3837D, SizeOfImage 0x063A2000)" or "an unknown build (stamp 0x..., SizeOfImage 0x...)".
std::string ExeBuildText(const ExeBuild& b);

// ================================================================================================ the masks
enum MaskSource
{
	kMaskNone = 0,   // nothing could be read
	kMaskExe,        // [g_Civ5App+0x940] (and the GameViewState's view mask) of a build whose mask addresses this binary has
	kMaskTest,       // VPPROXY_TEST_CONFIG_MASK / VPPROXY_TEST_VIEW_MASK (honoured only when the EXE's masks are not read)
	kMaskConfigIni   // ticket 0588: a game EXE whose masks are not read (an unsupported build, or no mask addresses for
	                 // it): config.ini's [DisableSystems] keys, which the EXE read at its start (W3c.2), as a mask
};
const char* MaskSourceName(MaskSource s);
struct Masks
{
	MaskSource src;
	uint32_t config;
	bool haveView;
	uint32_t view;
	std::string why;  // one line: where the values came from, or why nothing could be read
	bool knownExe;    // the process is a build whose mask addresses this binary has (its masks must be readable)
	bool gameExe;     // ReadExeBuild().gameExe
	Masks() : src(kMaskNone), config(0), haveView(false), view(0), knownExe(false), gameExe(false) {}
};
// Reads the masks; never faults on an unreadable pointer (VirtualQuery before every read). The EXE's own masks on the
// real image of a supported build (ReadExeMasks, ticket 4963); else the test masks; else, in a game EXE, config.ini's
// [DisableSystems] (ConfigMaskOfIni).
Masks ReadMasks();
// The EXE's own masks of the image at base, of this build: [cam_app]+0x940 and, when [[cam_app]+0xC0]'s vptr is
// ses_gvs_vtbl, [GameViewState+0x1B4] (symbols from the build table, rebased on base). knownExe is set: an unreadable
// mask or a symbol the build does not carry leaves src kMaskNone (SessionKind: D33, fail loud).
Masks ReadExeMasks(uintptr_t base, int build);
// Ticket 0588: config.ini's text as the EXE's config mask: every bit set, minus the bit of each [DisableSystems] key whose
// value is a non-zero number (section and key names in any case; ';' and '#' comments; a UTF-8 BOM; CRLF or LF). False
// (and *why says so) when the text has no [DisableSystems] section: the EXE then uses its defaults (*mask = all set).
bool ConfigMaskOfIni(const std::string& text, uint32_t* mask, std::string* why);
// Reads config.ini (ConfigIniPath) into a mask: 1 read, 0 no such file (the EXE's defaults, every system on: *mask =
// 0xFFFFFFFF), -1 the file exists but cannot be read (*why says why).
int ReadConfigIniMask(uint32_t* mask, std::string* why);

// ================================================================================================ the player's folder
// The game's config.ini: Documents\My Games\Sid Meier's Civilization 5\config.ini (SHGetFolderPathW(CSIDL_PERSONAL),
// shell32 loaded by name), or VPPROXY_TEST_CONFIG_INI (tests only), as proxy_rt_logic.cpp reads it for server.cfg.
// Only its folder is used (the reports root, the mods database); the file is never written. False when no path can
// be formed.
bool ConfigIniPath(std::wstring* out);

// ================================================================================================ the helper
#define VP64HELPER_FILE L"vp64helper.exe"
// "\"<helper>\" <verb> --report \"<dir>\" --options \"<vpproxy.ini>\"" (the helper's interface, issue 3520:
// vp64helper popup --report <dir> [--options <ini>] (a stock session) and vp64helper failed --pid <EXE pid> --report <dir>
// --options <ini> (a D33 session, D125: it waits for the EXE to end, restores config.ini, sets failed=1, shows the
// popup); --options names vpproxy.ini, whose handin= replaces the default hand-in line, D122). verb = L"popup" or
// L"failed --pid <pid>" (VerbFor). No --options when optionsIni is empty.
std::wstring HelperCommandLine(const std::wstring& helper, const std::wstring& verb, const std::wstring& reportDir,
                               const std::wstring& optionsIni);
// The verb for a session: L"popup" (stock) or L"failed --pid <pid> [--config \"<config.ini>\"]" (D33: the config.ini the
// proxy resolved inside the EXE, which the helper restores; 3520's final CLI, 2026-11-14 05:0x).
std::wstring VerbFor(bool d33, unsigned long pid, const std::wstring& configIni = std::wstring());
// Starts <proxyDir>\vp64helper.exe with that command line, detached (no inherited handles, a new process group, out of
// the EXE's job when the job allows it); the game does not wait for it. *line = one log line (what was started, or why
// not).
bool StartHelper(const std::wstring& proxyDir, const std::wstring& verb, const std::wstring& reportDir,
                 const std::wstring& optionsIni, std::string* line);

// ================================================================================================ the session rule
// Decisions D119 and D125 (issue 3523; the Architect's section 1): a failure of the 64-bit path in a D33 session (any
// rule bit clear in the config mask) never plays: report, vp64helper failed, the end of the game. A stock session (every
// rule bit set) keeps the stock-session behaviour: report, vp64helper popup, the 32-bit fallback.
// "Our keys": proxy_session_keys.h VP64_D33_KEYS_A2. The rule bits: our keys minus the stock-off pair (OverlayDebug,
// ColorKeyEdit), which the stock config.ini already has off.
const std::vector<const char*>& OurKeys();
// The bits of a key list; names that are not [DisableSystems] keys are appended to *unknown (comma-separated).
uint32_t BitsOfKeys(const std::vector<const char*>& keys, std::string* unknown);
uint32_t StockOffBits();
uint32_t RuleBits();  // BitsOfKeys(OurKeys()) & ~StockOffBits() = 0x0003C090 (0x0003F890 until Particle, Combat and Projectile left the list, issue 0368, 2026-11-15; 0x0083F890 until MovementPath left it, issue 3803; 0x008BF890 until Minimap left it, issue 0192; 0x00ABF890 until Waves left it; all 2026-11-14)
enum Kind
{
	kStock = 0,
	kD33 = 1
};
Kind Classify(uint32_t configMask, uint32_t ruleBits);
const char* KindName(Kind k);
// The session's kind from the masks: the mask's (kMaskExe, kMaskTest, kMaskConfigIni); with no mask: D33 when the process
// is a build whose masks must be readable, or a game EXE whose config.ini cannot be read (fail loud, D125; ticket 0588:
// never "stock (assumed)" in a game EXE), stock only when it is not a game EXE (development and test processes, which
// never had a D33 config). *why = one line.
Kind SessionKind(const Masks& m, std::string* why);

// ================================================================================================ the build gate
// Ticket 0588 (D118, D125; research/02-exe-internals/new-exe-build-26577625.md B.4 and "0588 behaviour as built"): at the
// boot, before the server is started, the proxy decides from the EXE build whether the 64-bit path may start.
//   - mode=off or failed=1: not asked (the 32-bit DLL as configured).
//   - not a game EXE (a test or development host): go, as before.
//   - a known and supported build: go, unchanged (supported: ExeBuildSupported, product ready and the table complete).
//   - an unknown build, or a known one this binary has no table for: the server is NOT started, and
//       config.ini in the A'' form (or unreadable): no play (D125: a 32-bit game would draw no terrain): the report, then
//         vp64helper failed (it waits for the EXE's end, restores config.ini, writes failed=1, shows its popup), then the
//         end of the game (exit code 3); every time, whether a notice for the build was shown before or not;
//       config.ini stock: the 32-bit DLL (an ordinary stock-drawn VP game), and the first time for this build (stamp and
//         SizeOfImage) the report and vp64helper popup; later starts on the same build only the log line and the title
//         notice.
// The summary line (popup-summary.txt, the popup's second paragraph) says that Civilization V was updated, the build found,
// that the 64-bit patch is off until a version that supports it is installed, and where to look for one.
enum ConfigForm
{
	kFormStock = 0,  // every rule key on (Classify kStock)
	kFormA2 = 1,     // a rule key off: config.ini in the A'' form (vp64helper on wrote it)
	kFormUnknown = 2 // the form cannot be told (config.ini unreadable): treated as A'' (never a game without terrain)
};
const char* ConfigFormName(ConfigForm f);
enum BootAction
{
	kBootGo = 0,     // start the 64-bit path as before
	kBootOff32 = 1,  // the 32-bit DLL, no server
	kBootNoPlay = 2  // no server, no 32-bit DLL: the report, vp64helper failed, then Fatal(3)
};
const char* BootActionName(BootAction a);
struct BootGateIn
{
	bool pathOn;          // mode=on and not failed=1
	bool gameExe;         // ExeBuild.gameExe
	int build;            // ExeBuild.id
	bool supported;       // ExeBuild.supported
	bool devReady;        // ExeBuild.devReady (named in the go line)
	ConfigForm form;      // config.ini's form (from the masks: ReadMasks + Classify)
	bool noticeShown;     // NoticeShown() for this stamp and SizeOfImage
	uint32_t stamp;       // for the texts
	uint32_t sizeOfImage;
	BootGateIn()
		: pathOn(true), gameExe(true), build(-1), supported(false), devReady(false), form(kFormStock), noticeShown(false),
		  stamp(0), sizeOfImage(0)
	{
	}
};
struct BootGate
{
	BootAction action;
	bool report;          // write the D119 report and start the helper (popup for kBootOff32, failed for kBootNoPlay)
	bool remember;        // after the helper started: RememberNotice for this build
	int exitCode;         // kBootNoPlay: 3 (as on_server_death=exit); else 0
	std::string code;     // the report's code under stage "exe-build": "unknown-build" or "unsupported-build" ("" on go)
	std::string why;      // one log line
	std::string summary;  // the player's summary line (popup-summary.txt); "" unless report
	std::string notice;   // the title notice of the 32-bit fallback (kBootOff32)
	BootGate() : action(kBootGo), report(false), remember(false), exitCode(0) {}
};
BootGate DecideBoot(const BootGateIn& in);
// The record of builds whose notice was shown: <config.ini's folder>\VP64\exe-build-notices.txt, one line per build
// "0x<stamp> 0x<SizeOfImage>". "" when configIni has no folder.
std::wstring NoticeFilePath(const std::wstring& configIni);
// True when the file holds this build's line (a missing or unreadable file: false, so the notice is shown).
bool NoticeShown(const std::wstring& file, uint32_t stamp, uint32_t sizeOfImage);
// Appends this build's line (creating the VP64 folder); a line already there is not written twice. *line = one log line.
bool RememberNotice(const std::wstring& file, uint32_t stamp, uint32_t sizeOfImage, std::string* line);

// ================================================================================================ text
std::wstring Widen(const std::string& s);   // CP_ACP
std::string Utf8(const std::wstring& s);
}  // namespace ProxySession
