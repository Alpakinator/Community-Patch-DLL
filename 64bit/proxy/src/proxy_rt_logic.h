// proxy_rt_logic.h - the parts of the x86 proxy runtime that do not need the engine, vpipc or the generated code
// (issue 2502): vpproxy.ini settings and the D28 layout, per-boot run folders, the strict allow-list, the thread
// binding rule and its checks, method roles, the frame tracker and the traffic counters with their snapshot format.
// Hand-written. proxy_rt.cpp uses them; proxy/tools/test_proxy_rt.py compiles this file alone into a unit test
// (proxy/tools/test_proxy_rt.cpp) with the proxy's toolchain (clang-cl -m32, VC9 headers, C++11 language at most).
// Design: research/03-dll-engine-boundary/proxy-design.md sections 11-14.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The STL layout of every proxy TU must match the fork's: CvGameCoreDLLPCH.h turns the VC9 iterator checks off, which
// changes the size of std::string and std::vector. A TU that does not include that header must do the same before any
// STL header, or a std::string crossing between TUs is read with the wrong layout (found 2026-11-10: a crash in
// IsRunFolderName called from proxy_rt.cpp).
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

namespace ProxyLogic
{
// ================================================================================================ paths and text
std::string Trim(const std::string& s);
bool IsAbsolutePath(const std::string& p);
// Directory of a path ("." when it has none). Accepts both separators.
std::string DirOf(const std::string& p);
// p made absolute against base (base\p); absolute and empty paths unchanged; '/' becomes '\'.
std::string ResolvePath(const std::string& base, const std::string& p);

// ================================================================================================ settings
// vpproxy.ini (next to the proxy DLL, or the file VPPROXY_INI names). One key=value per line, '#' or ';' comments.
// Relative paths are resolved against the proxy DLL's own folder (never the EXE's working directory). Defaults give the
// D28 layout (decision D28, research/06-tooling-and-workflows/installer-notes.md section 5):
//   <proxy dir>/CvGameCore_Expansion2.dll        the proxy (x86)
//   <proxy dir>/CvGameCore_Expansion2_x86.dll    the original 32-bit DLL (mode=off loads it)
//   <proxy dir>/vpproxy.ini
//   <proxy dir>/VP64/                            server.exe, CvGameCore_Expansion2_x64.dll (the x64 DLL; see
//                                                X64DllName), lua51_x64.dll, CvGameDatabase_x64.dll,
//                                                CvLocalization_x64.dll
//   <proxy dir>/VP64/run/<yyyymmdd-hhmmss>-<pid>-<n>/   one folder per boot (logs, server.cfg, server outputs); in an
//                                                installed game (both VP forms) <My Games>/Logs/VP64/run instead
//                                                (UserLogsDir, issues 0148 and 2606)
// Issue 2606 (MC #3403, 2026-11-17): the x64 DLL is CvGameCore_Expansion2_x64.dll in installer packs. Under its old name
// CvGameCore_Expansion2.dll the EXE's DLC scan took it for a DLC's game DLL in the modpack form (VP inside
// Assets\DLC), loaded it into the 32-bit process and crashed (error 193, then 0xC0000005). The old name is still read
// when the new one is absent, so every development and test layout keeps working.
#define VP64_X64_DLL_FILE "CvGameCore_Expansion2_x64.dll"
#define VP64_X64_DLL_FILE_OLD "CvGameCore_Expansion2.dll"
struct Settings
{
	// where the settings came from
	std::string iniPath, proxyDir, exeDir;
	bool iniFound;
	// mode and the off switch
	bool modeOn;              // mode=on|off; a missing ini is off
	std::string fallbackDll;  // fallback_dll= (default <proxy dir>/CvGameCore_Expansion2_x86.dll)
	// the D28 layout
	std::string x64Dir;       // x64_dir= (default <proxy dir>/VP64)
	std::string serverExe;    // server_exe= (default <x64 dir>/server.exe)
	std::string x64Dll;       // the x64 DLL server.cfg's dll= names: <x64 dir>\X64DllName(x64 dir, "") after
	                          // ParseSettings; with pack=1 the proxy replaces it by the checked manifest's file (issue 2606)
	std::string serverConfig; // server_config= (default: written by the proxy into the boot's run folder)
	std::string runRoot;      // run_root= (default <x64 dir>/run; in an installed game the proxy then sets
	                          // UserLogsDir(x64 dir)\run; a run_root= of the ini is kept as written)
	uint32_t keepRuns;        // keep_runs= (default 10, at least 1): run folders kept, this boot's included. Ticket 0747:
	                          // a test switch now; Boot replaces it by EffectiveKeepRuns ([Logs] KeepRuns set in the
	                          // options file wins)
	bool keepRunsSet;         // ticket 0747: the ini named keep_runs
	std::string logPath;      // log= (default <run folder>/vpproxy.log; without a run folder <proxy dir>/vpproxy.log)
	// what the generated server.cfg carries (ignored with server_config=)
	std::string gameDir;      // game_dir= (default the EXE's folder)
	std::string locDb;        // loc_db= (bug 0123: not set = the game's own cache\Localization-Merged.db beside its
	                          // config.ini, found when server.cfg is written; "none" or empty = no server localization)
	bool locDbSet;            // the ini named loc_db (a path, "none" or empty)
	std::string serverAllow;  // server_allow= (default ""): the server's unexpected_allow= list
	bool dllLogs;             // dll_logs= (default 0)
	std::string serverCfgExtra; // server_cfg_extra= (default ""; issue 0307, TEST ONLY, never in the shipped ini): a file
	                          // of key=value lines appended to the written server.cfg after every other line (a later
	                          // key wins in the server's reader); see ServerConfigFile
	// the rest (epic 2300 keys)
	std::string db, serverArgs;
	bool onDeathExit;         // on_server_death=exit|continue (default exit)
	bool strict;              // strict= (default 0)
	std::string strictAllow;  // strict_allow= (default "": no list)
	uint32_t connectTimeoutMs;
	uint32_t replySpinUs, idleSpinUs;
	bool singleThreadHost;    // host=exe|headless (default exe): headless = one engine thread calls everything
	bool counters;            // counters= (default 1): per-interface counters, frames, snapshots
	// issue 2700 (proxy-design section 16): local answers
	bool localAnswers;        // local_answers= (default 1): canonical handles, the lock-want word, the epoch mirror; 0 =
	                          // every call crosses (the uncached reference proxy of epics 2300-2500)
	bool localVerify;         // local_verify= (default 0): every mirror hit is also asked of the server and compared
	bool keepTemp;            // keep_temp= (default 0): issue 2700 item 4, temporary wrappers the engine QueryInterfaces are
	                          // kept (KeepIfTemp). Off by default: the first live run with it on (c5h-a 2026-11-10 18:05,
	                          // build d77602a) ended with an access violation in the EXE 0.45 s after the first kept
	                          // wrapper (a unit, GameplayUnitCreated); not diagnosed
	std::string localTest;    // local_test= (default ""): "frozen" = no engine -> DLL call invalidates the mirror (the
	                          // live negative test: in the real EXE other impure calls surround Update, so "stale"
	                          // alone was not caught there, 2026-11-10 19:33); "stale" = ICvGame1::Update leaves the mirror valid (the
	                          // negative test: a deliberately stale mirror the verifier must catch)
	uint32_t testSlotPoll;    // test_slot_poll= (default 0; issue 2805): N > 0 = at the top of every ICvGame1::Update the
	                          // proxy calls the 24 mirrored getters of the EXE's slot poll N times for every player slot,
	                          // through the proxy's own wrappers (proxy_update.cpp), so a headless verify run checks a
	                          // counted number of mirror answers. Honoured only with host=headless in a process whose EXE
	                          // is not the game's (CivilizationV*.exe): otherwise logged and ignored
	bool luaMirror;           // lua_mirror= (default 0; issue 2805): 1 = the UI-Lua mirror answers pure Lua reads
	                          // (proxy_local.h); 0 = every Lua call crosses as before, the mirror only counts would-hits.
	                          // local_test=luastale is its negative test (Lua entries answered whatever their epoch)
	bool pack;                // pack= (default 0; issue 3504): 1 = x64_dir is a DLL pack (proxy_pack.h, tools/release/
	                          // README.md section 5): manifest.json is checked before the server starts, and a missing
	                          // pack, a pack for another 32-bit DLL, a hash mismatch or a server that does not start or
	                          // connect boots fallback_dll instead (the mode=off path), said once in the log and in the
	                          // window title. 0 = development and test layouts: no check, failures return NULL as before.
	                          // Issues 3521, 3523 (D119, D125): with pack=1 every such failure, and a server that fails in
	                          // a game, also writes the local error report and starts the helper; in a D33 session the
	                          // game then ends instead of the fallback (proxy_session.h).
	                          // handin= is the helper's (the popup's hand-in line, D122): accepted and ignored here
	bool failed;              // failed= (default 0; issue 3523): 1 = written by vp64helper failed after a D33-session
	                          // failure: the boot behaves as mode=off until vp64helper on clears it (no automatic retry)
	bool uiBridge;            // ui_bridge= (default 0; issue 0178): 1 = the Lua bridge (proxy_ui_bridge.h) installs its
	                          // listeners, the per-frame drain and the UnitMoving replacement into each engine Lua state
	                          // the DLL's libraries are registered into, and posts UI records on the ui channel
	bool uiBridgeTest;        // ui_bridge_test= (default 0; issue 0178): with ui_bridge=1 also the Lua table
	                          // VP64UiBridgeTest (QueueRaise, Stats) and a record per UnitMoving call; tests only
	std::string problems;  // parse problems, one per line, logged once the log is open
	Settings();
};
// Issue 2606: the x64 DLL's file name in x64Dir. manifestFile (the pack manifest's x64_dll.file, checked with pack=1)
// wins when not empty; otherwise VP64_X64_DLL_FILE when that file exists in x64Dir, else VP64_X64_DLL_FILE_OLD.
std::string X64DllName(const std::string& x64Dir, const std::string& manifestFile);
// Issues 0148 and 2606: where the patch's run-time files go in an installed game: <the player's user folder>\Logs\VP64,
// the user folder being the folder of the game's config.ini, where the error report goes too (ProxyReport::Root). Both
// VP forms, matched on path by folder names in any case:
//   modpack form, path under Assets\DLC (<game>\Assets\DLC\VP_MODPACK\Mods\...): <folder of configIni>\Logs\VP64
//     (configIni: ProxySession::ConfigIniPath); "" when configIni has no folder. Checked first: the DLC has a Mods
//     folder of its own, which the 0148 rule took for the user's (a log under the game folder, 2604, 2026-11-17).
//   mods form, a folder named MODS (<user>\MODS\...): <user>\Logs\VP64, the MODS folder's parent (issue 0148). In an
//     installed game this is the folder of config.ini as well; it is kept so that a test layout under a temporary
//     <user>\MODS never writes into the real player's folder.
// Any other path (development and test layouts): "" (the caller keeps its own default).
std::string UserLogsDir(const std::string& path, const std::string& configIni);
// Fills s from the ini text (found=false: no ini; text ignored). proxyDir: the proxy DLL's folder; exeDir: the EXE's.
void ParseSettings(const std::string& text, bool found, const std::string& iniPath, const std::string& proxyDir,
                   const std::string& exeDir, Settings& s);
// The text of the server.cfg the proxy writes for a boot (server_config= not set): run_dir, the x64 files, game_dir,
// loc_db, dll_logs, fail_on_unexpected (= strict), unexpected_allow (= server_allow), connect_timeout_ms.
// Issue 0117: Language (ll_CC) and LoggingEnabled (0|1, else -1) from the text of the game's config.ini.
void ReadGameConfig(const std::string& text, std::string& language, int& logging);
// ServerConfigText also writes language= (the game's config.ini, when it holds a valid code) and dll_logs=1 when the
// game's LoggingEnabled = 1 (issue 0117); VPPROXY_TEST_CONFIG_INI=<path> (tests only) replaces the game's config.ini.
// Bug 0123: without loc_db in the ini, loc_db= is <the config.ini's folder>\cache\Localization-Merged.db when that file
// exists (the database the EXE builds for the game's language and active mods), else empty; a comment line says which.
std::string ServerConfigText(const Settings& s, const std::string& runDir);
// Issue 0307 (TEST ONLY): the file server_cfg_extra= names. One key=value per line; blank lines and lines whose first
// character is '#' or ';' are skipped; key and value are trimmed and written back as "key=value" (the server's reader
// does not trim). A line without '=' or with an empty key makes the whole file invalid: false, `why` names the line.
// `lines` gets the lines in file order.
bool ParseServerCfgExtra(const std::string& text, std::vector<std::string>& lines, std::string& why);
// Reads `path` and parses it; a file that cannot be read is false with `why` (a boot failure in Boot, never a skip).
bool LoadServerCfgExtra(const std::string& path, std::vector<std::string>& lines, std::string& why);
// The whole server.cfg the proxy writes: `base` (ServerConfigText), "options_file=<optionsFile>" when optionsFile is
// set, `shellCfg` (ProxyShell::Configure), then, only when extraPath is set, one "# TEST ONLY" comment line naming
// extraPath and the `extra` lines. extraPath empty = the text of before issue 0307, byte for byte.
std::string ServerConfigFile(const std::string& base, const std::string& optionsFile, const std::string& shellCfg,
                             const std::string& extraPath, const std::vector<std::string>& extra);

// ================================================================================================ run folders
// "<yyyymmdd>-<hhmmss>-<pid>-<n>"
std::string RunFolderName(const SYSTEMTIME& t, uint32_t pid, uint32_t n);
bool IsRunFolderName(const std::string& name);
// Of the existing folder names, those to delete so that `keep` remain after one more is created (oldest first;
// names that are not run folder names are never returned).
std::vector<std::string> RunFoldersToPrune(const std::vector<std::string>& existing, uint32_t keep);
// Ticket 0747 (D255 G5): the number of run folders (and of the compositor's kept logs) to keep. [Logs] KeepRuns when the
// options file sets it (optFromFile; optValue 1..100 by the options table), else vpproxy.ini keep_runs when the ini names
// it (iniSet; a test switch, never in the shipped ini), else optValue (the table's default, 10). Always at least 1 and at
// most 100. *why (may be NULL): where the number came from, for the boot log.
uint32_t EffectiveKeepRuns(bool optFromFile, int optValue, bool iniSet, uint32_t iniValue, std::string* why);
// Ticket 0747 (D255 G5, options-file.md 10.4 (a)): the server's copies of the game database in a run folder's db\, as
// the server names them (server/src/remote/srv_remote_main.cpp): "Civ5DebugDatabase.<n>.db" (SetGameDatabase) and
// "units-<n>.<g>.db" (the render side's copy), each also with SQLite's "-journal", "-wal" or "-shm" after it; n and g
// one or more digits; the names compared without regard to case. Nothing else in db\ is one.
bool IsRunDatabaseName(const std::string& name);
// Of the file names in a run folder's db\, those IsRunDatabaseName accepts (in the given order).
std::vector<std::string> RunDatabasesToDelete(const std::vector<std::string>& names);
// Ticket 0747: whether the run folder's database copies are deleted at this shutdown: only when the server ended by
// itself after kRt_Shutdown (waited: it exited within the wait) with exit code 0 (the server exits 2 when it recorded a
// failure, srv_remote_main.cpp ServiceThread) and the proxy wrote no error report this load (reported). Otherwise they are
// kept for the root cause, and *why (may be NULL) says why.
bool DeleteRunDatabases(bool waited, uint32_t exitCode, bool reported, std::string* why);

// ================================================================================================ allow-list
// strict_allow=<file>: one entry per line, the key then the reason (required), '#' comments. A key matches a method
// key as the proxy logs it ("ICvGame1::Update()"), the key without its signature, or with a trailing '*' a prefix of
// either (the forms of proxy/tools/remote-allowlist.txt).
struct AllowList
{
	std::vector<std::string> keys, reasons;
	std::string problems;  // entries not honoured (no reason), one per line
	void Parse(const std::string& text);
	// The matching entry's key, or NULL.
	const char* Match(const std::string& key) const;
};

// ================================================================================================ method roles
// Roles of the methods the runtime watches, found by model key (the same key for every kind of the chain).
enum Role
{
	kRoleNone = 0,
	kRoleUpdate,           // ICvGame1::Update(): the game-core thread's call
	kRoleGetGame,          // ICvGameContext1::GetGame(): the core thread wraps each Update in GetGame/Destroy
	kRoleGetActivePlayer,  // ICvGame1::GetActivePlayer(): arms the frame marker
	kRoleIsHuman,          // ICvPlayer1::IsHuman() const: disarms it (the slot loop of the per-frame poll)
	kRoleTurnTimer         // ICvPlayer2::HasTurnTimerExpired(): a frame when armed
};
Role RoleOfKey(const char* key);

// ================================================================================================ thread binding
enum
{
	kChanUi = 0,
	kChanCore = 1,
	kChanAux = 2
};
// The channel of a thread at its first call (never changed afterwards): the boot thread (the one that called
// DllGetGameContext) -> ui; the first other thread whose first call is GetGame or Update -> core (claims *coreClaimed);
// every other thread -> aux. *reason: a short text for the log.
uint32_t ChooseChannel(bool isBootThread, Role firstRole, volatile LONG* coreClaimed, const char** reason);
// Violations of the binding found at a call (bit set):
enum
{
	kViolUpdateOffCore = 1,  // ICvGame1::Update on a channel other than core (host=exe only)
	kViolUiForeign = 2       // a top-level call on ui from a thread other than the boot thread
};
uint32_t CheckCall(uint32_t chan, bool isBootThread, bool topLevel, Role role, bool singleThreadHost);
const char* ChannelName(uint32_t chan);

// ================================================================================================ frames
// A frame = one run of the EXE's per-frame slot poll FUN_006A1DC0 (research/03-dll-engine-boundary/
// interfaces-and-traffic.md section 5): GetActivePlayer, then on the active player's wrapper HasTurnTimerExpired, then
// the 64-slot loop (IsHuman per slot, HasTurnTimerExpired again on qualifying slots). The marker is the first
// HasTurnTimerExpired after a GetActivePlayer with no IsHuman in between, on the ui thread.
struct FrameTracker
{
	enum { kCap = 1u << 16 };  // frame intervals kept per window (the newest)
	bool armed;
	uint64_t frames, timerCalls;
	int64_t lastQpc;
	std::vector<uint32_t> intervalsUs;  // ring of the window's frame intervals (microseconds)
	uint64_t windowFrames;              // frame intervals recorded in the window (may exceed kCap)
	uint32_t ringNext;
	FrameTracker();
	// A call on the ui thread with a watched role; returns true when it is a frame marker. qpcFreq: ticks per second.
	bool OnUiCall(Role role, int64_t qpc, int64_t qpcFreq);
	struct Stats
	{
		uint64_t intervals;  // in the window
		uint64_t dropped;    // older than the newest kCap (not in the percentiles)
		double meanMs, p50Ms, p99Ms, maxMs;
	};
	// Statistics of the frame intervals since the last call (nearest-rank: p = sorted[floor(q * (n - 1))]); clears the
	// window.
	Stats TakeWindow();
};

// ================================================================================================ traffic counters
// Calls per (row, direction, channel). Rows: 0 = unknown method type, 1..kinds = interface kinds, kinds+1 = runtime
// calls the proxy makes (rt:), kinds+2 = runtime calls the server makes (rtE:). dir 0 = out (engine -> DLL and the
// proxy's runtime calls), 1 = in (served: DLL -> engine), 2 = local (issue 2700: answered by the proxy, no crossing;
// not in the "*" total of a snapshot, which counts crossings; a second "*" row with direction local totals them). Plain increments: the ui and core channels have one writer
// thread each; aux threads may lose an increment when two count the same cell at once (counters, not accounting).
struct Traffic
{
	uint32_t rows;
	std::vector<uint64_t> cells, last;
	Traffic() : rows(0) {}
	void Init(uint32_t kinds);
	inline void Add(uint32_t row, uint32_t dir, uint32_t chan)
	{
		if (row < rows && chan < 3u && dir < 3u) ++cells[(row * 3u + dir) * 3u + chan];
	}
};

// One snapshot: appends its rows to out (TSV, the header first when header is true). Row names: rowName(row) (NULL ->
// "row<n>"). The deltas are cells - last; last becomes cells. Columns:
//   snapshot label time_s window_s frames fps frame_ms_mean frame_ms_p50 frame_ms_p99 frame_ms_max frame_intervals
//   timer_calls interface direction channel calls calls_per_frame
// The first row of a block has interface "*" (all calls of the window); then one row per non-zero cell.
typedef const char* (*RowNameFn)(uint32_t row);
void FormatSnapshot(std::string& out, bool header, uint32_t seq, const std::string& label, double timeS,
                    double windowS, uint64_t frames, uint64_t timerCalls, const FrameTracker::Stats& st, Traffic& t,
                    RowNameFn rowName);
// ================================================================================================ engine handle slots
// Issue 9036 (bug 0102): the engine-object table gave every engine object a new slot and never reused a dead one. The
// DLL creates an ICvEngineScriptSystemArgs1 for every Lua hook call and destroys it after the call, so on the huge save
// the table grew by about 50,000 dead 12-byte entries per AI turn (3,543,306 at turn 420, 42.5 MB in one vector) and
// its next reallocation (64 MB contiguous) was the allocation that failed when the EXE's address space ran out.
// Slots are now reused, but only after kQuarantine later frees (FIFO): a freed id is not handed out again while the
// server may still hold it (it forgets the handle right after the call that freed it, SrvRt::EngineForget), and a
// stale handle keeps being refused as dead for that long. Ids are 1-based (id = slot + 1), as before.
class SlotAllocator
{
public:
	enum { kQuarantine = 4096 };
	explicit SlotAllocator(uint32_t quarantine = kQuarantine) : m_quarantine(quarantine), m_head(0), m_slots(0) {}
	// A slot index (0-based): the oldest freed slot once more than `quarantine` slots wait, else a new one.
	uint32_t Alloc();
	// Gives slot back; it becomes reusable after `quarantine` further frees.
	void Free(uint32_t slot);
	uint32_t Slots() const { return m_slots; }  // slots ever made (the size the table must have)
	uint32_t Waiting() const { return (uint32_t)(m_free.size() - m_head); }
private:
	uint32_t m_quarantine, m_head, m_slots;
	std::vector<uint32_t> m_free;  // FIFO: [m_head, size) wait; compacted when the consumed prefix is large
};

// ================================================================================================ RETURN payload ring
// A RETURN payload lives in the channel's data area until this side's next send, and decoding it may make a nested call,
// so ProxyRt::KeepReturn copies it into the next of kSlots per-thread buffers used in turn (2026-11-10, integration).
// Issue 9041 (bug 0102): the buffers were std::vector::assign targets, which never shrink, so every slot that once held a
// save body (ICvMap1::Write, 12.4 MB on the huge save) kept that capacity for the life of the thread, and a slot that had
// to grow by a little grew 1.5x (the 18.5 MB group). Dump of turn 361: two threads' rings kept 188 and 214 MB; the EXE grew
// about 22 MB per AI turn until its address space ran out at turn 420 (proxy-design section 18.7). Now a slot whose buffer
// is above keepMax is released before it is reused, and a payload above keepMax gets a buffer of exactly its size. A
// payload stays valid until kSlots - 1 later Keep calls of the same ring (the contract callers rely on), not longer.
class ReturnRing
{
public:
	enum { kSlots = 64, kKeepMax = 64 * 1024 };
	// keepMax = 0xFFFFFFFF keeps every buffer (the behaviour before 9041; unit tests use it as the negative control).
	explicit ReturnRing(uint32_t keepMax = kKeepMax) : m_keepMax(keepMax), m_next(0) {}
	// Copies [p, p + n) into the next slot; returns the copy (NULL when n is 0).
	const uint8_t* Keep(const uint8_t* p, uint32_t n);
	size_t Capacity() const;  // bytes held by all slots
private:
	uint32_t m_keepMax;
	uint32_t m_next;
	std::vector<uint8_t> m_bufs[kSlots];
};
}  // namespace ProxyLogic
