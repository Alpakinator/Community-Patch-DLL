// proxy_shell.h - the shell's in-process jobs on the proxy route: the composite DLL's load and the view-mask write
// (one-off 9040; product form issue 3305). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The proxy is the only module of the patch inside the EXE on the shipped route, so it does the two things the shell
// needs from inside the EXE's process (research/05-graphics-composition/proxy-route-compositor.md section 4):
//   - loads the composite DLL from the patch's own folder (the proxy's folder, beside VP64\), switched by
//     VP64Options.ini [Renderer] ServerRenderer, after checking its SHA-256 and the proxy's own SHA-256 against the
//     manifest shell_composite.sha256 beside it; hands the product names to both sides: server.cfg render=1 and
//     render_section=Local\VP64Shell_<EXE pid>, and the composite VPSHELL_SECTION=<the same> in the EXE's environment,
//     and VPSHELL_OUT_DIR=<user folder>\Logs, outside the MODS tree the EXE watches (issue 0148);
//   - writes the EXE's per-view systems mask: view &= config | 0xFF000000, and the Terrain bit (D7 c1) cleared while the
//     composite is loaded, with every check of the origin; re-checked at the top of each ICvGame1::Update and by a
//     watcher thread every 10 ms, so that the Terrain bit that GVS::StartupGame sets back at each load does not stay
//     set until the first Update;
//   - issue 0172, switched by VP64Options.ini [Renderer] WhiteFogMask (1 = on, the default since D136; 0 = off): with
//     FluidFOW off, loads the map editor's White.dds once with the EXE's texture loader, writes its handle into the EXE's
//     FOW mask global and into the mask slot of the EXE's draw objects built before the proxy loaded (and once a second
//     from the Update hook), so that buildings, wonders, improvements, resources and trees keep their textures
//     (proxy_shell.cpp, White).
// Test switches only (9040's markers, unchanged): crashlogs\shell.helper names a DLL to load instead of the product
// composite (no hash check; log crashlogs\shell.log); crashlogs\viewmask.enabled turns the mask write on with its
// keep= / clear= values re-read every 2 s (log crashlogs\viewmask.log, a read-back line once a minute). Every change
// is one line in vpproxy.log ("shell:").
#pragma once
#include <string>

namespace ProxyShell
{
// The product file names, in the proxy's own folder.
#define VP64SHELL_COMPOSITE_FILE L"shell_composite.dll"
#define VP64SHELL_MANIFEST_FILE L"shell_composite.sha256"
#define VP64SHELL_BASE_PREFIX "Local\\VP64Shell_"  // + the EXE's pid in decimal (+ "_<boot>" from the second boot on)
// Issue 3308: per-boot names. The proxy can boot more than once in one EXE process (unloaded and loaded again); the
// first boot keeps 3305's names, every later one gets <base> = Local\VP64Shell_<pid>_<boot> and server.cfg
// render_texture=Local\VP64ShellWorld_<pid>_<boot> (the server's version-3 texture base). The boot count lives in the EXE
// process's environment (it outlives the proxy's image); the composite, which stays loaded, follows VPSHELL_SECTION's
// change (shell_composite.cpp ControlThread re-reads its configuration when the variable changes).
#define VP64SHELL_TEXTURE_PREFIX "Local\\VP64ShellWorld_"
#define VP64SHELL_BOOT_ENV "VP64SHELL_BOOT"

// From the mode=on boot after the options file was read and before server.cfg is written (proxy_rt.cpp Boot): reads
// the option, checks the composite against the manifest (the file is then held open without write sharing until the
// load) and returns the server.cfg lines ("render=1\nrender_section=<base>\nrender_handover=ring\n" plus, from the second
// boot on, "render_texture=<texture base>\n" (issue 3308); and, with [Renderer] ServerUnits = 1,
// "render_units=1\nrender_units_passes=1\nrender_units_positions=exe\n" together with a request for the Lua bridge's
// positions-only install (issue 4759, epic 0040); or "" when the composite is not going to be loaded).
// writesServerCfg = false when vpproxy.ini server_config= names the server's config (nothing is added; logged). Logs
// one line, two with ServerUnits = 1.
std::string Configure(bool writesServerCfg);
// Ticket 0624: after Configure, why the product composite was refused ("composite <path> REFUSED: <reason>"), or ""
// (not refused: accepted, the test switch, ServerRenderer = 0, or Configure not run).
const char* RefusedComposite();
// From DllGetGameContext after a successful mode=on boot (the EXE's thread that called it): loads the composite (or
// the test switch's DLL), installs the view-mask write and, with WhiteFogMask = 1, the white fog mask. mode=off never
// calls it: the fallback DLL does its own.
void OnBoot();
// Ticket 0635 (D225): from BootOff (mode=off, failed=1, the pack fallback) when [Proxy] GuardsWhenOff = 1, after the
// 32-bit DLL's first DllGetGameContext: the farm builder guard with rule 2 (proxy_farm_guard.h), without a composite.
// Nothing else of OnBoot runs then. OnUnload undoes it as after OnBoot.
void OnBootOff();
// From ProxyRtInt::BeforeUpdate (the game-core thread, top of each ICvGame1::Update): the re-check, and the white mask's
// pool pass at most once a second. Cheap.
void OnUpdate();
// From DetachOnUnload (DLL_PROCESS_DETACH by FreeLibrary, under the loader lock): stops the watcher thread, which must
// not outlive the proxy's image. Logs nothing (the log may be closed).
void OnUnload();

// Ticket 0535: the farm builder guard's entry points (proxy_farm_guard.h), set by proxy_farm_guard.cpp's static initialiser,
// so that a program linking this file without the guard (the offline unit tests of proxy/tools/test_proxy_rt.py) has
// none. OnBoot calls install before the composite's LoadLibrary and setComposite after it; OnUnload calls unload.
struct FarmGuardHooks
{
	void (*install)(const wchar_t* outDir, const wchar_t* iniPath);
	void (*setComposite)(bool loaded);
	void (*unload)();
};
extern const FarmGuardHooks* g_farmGuardHooks;

// Ticket 0691 (D242): the composite's hung-server report. OnBoot passes the composite (its export VPShell_SetHungSink)
// a sink; the composite's control thread calls it once when its exact lockstep wait ended because the server drew no
// frame for lockstep_hung_ms. The sink logs one line and calls g_serverHung: the existing failure route of a server that
// failed in a game (proxy_rt.cpp ProxyRtInt::ServerHung, set by its static initialiser; a program linking this file
// without proxy_rt.cpp has none, the sink then only logs).
extern void (*g_serverHung)(const char* reason);

// Ticket 0697: from Fatal (proxy_rt.cpp) before its TerminateProcess, which skips every DLL's process-detach: the
// composite's export VPShell_FlushTotals(why, detail, timeoutMs) (found at OnBoot's LoadLibrary) writes its last pacing
// lines ("pacing totals: why=<why>"). Called on a thread of its own; the caller waits at most waitMs, so a stuck composite
// cannot hold the process's end. Once per process (a second call returns at once); nothing without a loaded composite;
// one line without the export (an older composite) or when the thread cannot be made; one LOUD line on a timeout.
void FlushCompositeTotals(const char* why, const char* detail, unsigned long waitMs);

// ---- for the offline tests (proxy/tools/rt_tests/shell_unit.cpp); the product calls none of these ------------------
namespace Test
{
// Parses a manifest's text. True when it names both hashes (64 hex digits each); else *why says what is wrong.
bool ParseManifest(const char* text, size_t len, unsigned char proxySha[32], unsigned char compositeSha[32],
                   const char** why);
// The EXE checks against the image at `base` (a fake image in the tests). NULL = passed, else the reason.
const char* VerifyExe(const void* base);
// Ticket 4963: the code-pattern rows (group 0 the view mask, 1 the white mask, 2 the cloud base), so that the tests
// build their fake image from the product's own templates (vp_exe_patterns.h syntax: R(symbol) operands). False past
// the last row.
size_t PatternRows();
bool PatternRow(size_t i, int* group, int* sym, const char** name, const char** bytes);
// Installs the view-mask write against the image at `base` with the given rule bits (terrain = clear the Terrain bit,
// keep/clear = the test switch's values) without reading any marker. False when VerifyExe refused.
bool InstallAt(const void* base, bool terrain, unsigned long keep, unsigned long clear, bool watcher);
// One re-check as the Update hook makes it.
void Update();
// Stops and forgets everything (the watcher is stopped as at an unload); clears VP64SHELL_BOOT (a first boot follows).
void Reset();
// Issue 3308: the boot number the last Configure gave (0: no names given), and the names of (pid, boot).
unsigned long Boot();
// Issue 0148: the composite's output folder for a DLL path (<user>\MODS\...\x.dll -> <user>\Logs); false when no folder of
// the path is named MODS.
bool OutDir(const wchar_t* dllPath, wchar_t* out, size_t cap);
void Names(unsigned long pid, unsigned long boot, char* base, size_t baseCap, char* texture, size_t textureCap);
// Counters: watcher polls, polls ended by an access violation, writes made.
unsigned long WatcherPolls();
unsigned long WatcherFaults();
unsigned long Writes();
// Issue 0172, the white fog mask. A stand-in for the EXE's texture loader (__thiscall through __fastcall: this, unused
// EDX, the path); returns the handle or 0.
typedef unsigned long(__fastcall* WhiteLoaderFn)(void* self, void* unusedEdx, const char* path);
// The white mask's guards against the image at `base` (VerifyExe's included). NULL = passed, else the reason.
const char* WhiteVerify(const void* base);
// What OnBoot does with [Renderer] WhiteFogMask = option, against the image at `base`, with `loader` in place of the
// EXE's loader. True when the handle is in the mask global and the pool pass ran.
bool WhiteBoot(const void* base, int option, WhiteLoaderFn loader);
// Ticket 0339: as WhiteBoot with the model fog on (the cloud base layer loaded and written too). WhiteBase: its handle
// (0 = not installed or failed: then VP64SHELL_FOWBASE_WHY says why); WhiteBasePatched: base slots written, all passes.
bool WhiteBootModelFog(const void* base, int option, WhiteLoaderFn loader);
unsigned long WhiteBase();
unsigned long WhiteBasePatched();
// One Update-hook step of the white mask; force = without the once-a-second limit.
void WhiteUpdate(bool force);
// Counters: pool passes, mask slots written (all passes); the handle in use (0 = not active).
unsigned long WhitePasses();
unsigned long WhitePatched();
unsigned long WhiteHandle();
// Ticket 0691: the sink OnBoot passes to the composite (__cdecl, reason text).
typedef void(__cdecl* HungSinkFn)(const char* reason);
HungSinkFn HungSink();
}  // namespace Test
}  // namespace ProxyShell
