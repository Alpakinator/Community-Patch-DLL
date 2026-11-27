// srv_remote_savemods.h - ticket 0681: a save loaded with a different set of mods, told to the player.
// Knowledge base: research/03-dll-engine-boundary/save-mod-mismatch.md.
//
// Why: a save made with a mod that changes the game's rules tables (VernsTweaks adds a promotion) and loaded without it
// makes the DLL misread the save and end in a read past the stream's end; the patch's report called that an engine
// failure, and the stock game's crash box blames memory. A mod with AffectsSavedGames = 0 is never written into the
// save's header, so the EXE cannot see the difference. The DLL can: CvGame::Write stores a 128-bit game data hash
// (FNV-1a over the info tables' counts and Type names) at body offset 4, and CvGame::Read compares it with the running
// one and only logs "WARNING - Save data hash mismatch!" to CustomMods.log before it reads on.
//
// What this module does: the server's log service hands it every CustomMods.log line (srv_ifaces.cpp); on the DLL's
// mismatch line it logs one LOUD server.log line naming the save's header mods, the active mods and the difference, and
// stays armed for the session. A later deliberate end of the server (SrvRemote::ReportAndExit -> WriteFatalRecord,
// srv_exe_crash.cpp) then gets the code save-mod-mismatch, the player's words and that detail. Nothing is refused before
// the DLL itself fails to read: a load that survives the mismatch plays on (stock allows it). Direct mode (srv_save.cpp)
// logs the same lines.
//
// Ticket 0744 (K31, the MC's ruling (c) of 2026-11-25): the DLL's first "Deserialization Error" line of a load (its
// CheckSentinel: a sentinel of the save did not match, so its read is already out of step) ends the server at once, on
// the reading thread inside the log call, through the end hook (remote mode: SrvRemote::ReportAndExit, exit 6): the
// record says save-mod-mismatch, before the misread can damage the server's memory (K31: the render thread faulted
// 0.107 s after that line). Direct mode sets no hook and reads on as before (a development tool; an ASan run of a
// misread needs it). A save made with the same mods never logs that line, so it loads exactly as before (D239).
//
// Inputs, all read-only: the save file's header (its path from the DLL's pregame loadFileName, or the direct-mode load
// path), the first 20 bytes of the body (the hash), and <Documents>\My Games\Sid Meier's Civilization 5\cache\
// Civ5ModsDatabase.db (Mods.Activated, ModProperties Name / AffectsSavedGames; through System32's winsqlite3.dll, as
// srv_feed_dbcopy.cpp; C5_DOCUMENTS overrides the Documents folder as for the render units).
#pragma once
#include <string>
#include <vector>

namespace SrvSaveMods
{
struct Mod
{
	std::string id;            // lower case
	unsigned int version = 0;
	std::string name;
	int affectsSaves = -1;     // 1, 0, or -1 unknown (the header's mods are save-affecting by definition)
};

// ---- pure parts (the "BEGIN 0681 PURE" block of the .cpp; server/tests/tools/test_save_mod_mismatch.py compiles that
// block on its own with a test main) ----
// The DLL's CustomMods.log text of CvGame::Read's mismatch warning -> the two hashes as written ("0x9b1c10f9-..."). False
// for any other text.
bool ParseHashLine(const std::string& text, std::string* saveHash, std::string* gameHash);
// The 16 hash bytes at body offset 4..19 in the DLL's text form ("0x%08x-0x%08x-0x%08x-0x%08x", little-endian words).
std::string HashText(const unsigned char* p16);
// One line naming the save header's mods, the active mods, the difference between the save's list and the active
// save-affecting mods, and the active mods that saves never list.
std::string DescribeMods(const std::vector<Mod>& header, bool headerKnown, const std::vector<Mod>& active, bool activeKnown);
// The player's summary line for a failed load after a mismatch (one sentence; the popup's summary).
std::string PlayerSummary();

// ---- the module ----
// A save's read begins: szSaveFile its path (NULL or "" when unknown), pHash16 the body's bytes 4..19 (NULL when not
// read), version the body's gamecore version string, bodyPrefix the body's first bytes (up to 4 KiB; "" when not read).
// Reads the header's mods and the active mods now (a few ms). Remote mode has no file name (the DLL's pregame
// loadFileName is empty there: the EXE keeps it, 0681 arm 2 [L]); on a mismatch the module then looks for the save
// among <Documents>\My Games\Sid Meier's Civilization 5\ModdedSaves and Saves (newest first, at most 400 files and
// 6 s): the file whose inflated body begins with bodyPrefix (only each file's first zlib piece is inflated).
void BeginLoad(const char* szSaveFile, const unsigned char* pHash16, const std::string& version,
               const std::string& bodyPrefix);
// A CustomMods.log line from the DLL (srv_ifaces.cpp, every line of that log). Cheap for every other line.
void OnDllLog(const char* szLogName, const char* szText);
// A load ended (direct mode: ok, or the exception's text). Logs one line when a mismatch was seen.
void EndLoad(bool bOk, const char* szWhy);
// The server ends on purpose (WriteFatalRecord): when armed (the DLL's game data mismatch) or after a deserialization
// error of the load, *pToken, *pSummary and *pReason become the mismatch's (the reason keeps the original one at its end,
// "what ended the server (code <token>)"). Not for an empty token, nor for a render module's (render-*) while the load
// has had no deserialization error (a renderer's own failure after a mismatched load that read in step); ticket 0744
// (a): after a deserialization error a render-* end is the misread's damage too. The mismatch stays armed for the whole
// session: the DLL itself says a mismatched load may corrupt the game, and the server has no "load ended" mark in remote
// mode. Returns true when it changed them.
bool OverrideFatal(std::string* pToken, std::string* pSummary, std::string* pReason);
// Ticket 0744: the end at the DLL's first deserialization error of a load (one that BeginLoad began). The hook must not
// return (remote mode: SrvRemote::ReportAndExit); NULL (the default, direct mode): no end, the load reads on as before.
typedef void (*EndFn)(int code, const char* szWhy);
void SetEndHook(EndFn pfn);
const int kMismatchExitCode = 6;   // the exit code of that end (FatalWords' engine-exception, overridden to save-mod-mismatch)
// Armed: the DLL reported a game data mismatch in this session. *pDetail: the logged detail line.
bool Armed(std::string* pDetail);
}  // namespace SrvSaveMods
