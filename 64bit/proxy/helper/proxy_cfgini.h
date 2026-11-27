// The config.ini routine of decision D119 (issue 3520): puts the D33 keys of the game's config.ini into the A'' form
// for the 64-bit path ("on"), gives the player's own values back ("off") and says which state the file is in
// ("status"). Compiled into the helper vp64helper.exe only (D125; the proxy itself never writes config.ini: it starts the
// helper, issue 3523).
//
// "Our keys" are the D33 set in the A'' form: shell/tools/darkred.py D33_OFF minus River and Arrows (those two stay on
// in config.ini; the proxy clears only their view bits). The list here is a copy; proxy/tools/test_cfgini.py compares
// it with darkred.py so the two cannot drift.
//
// The player's own values are kept in vp64-config-side.ini beside config.ini (written atomically: temp file + rename).
// Nothing else in config.ini is touched: other sections and keys, key order, comments, line ends (CRLF or LF as found)
// and bytes are kept as they are.
//
// Knowledge base: research/07-architecture-decisions/architect-2026-11-14-failure-popup-and-report.md section 1,
// research/06-tooling-and-workflows/release-driver.md (W3c).
#pragma once

#include <string>
#include <vector>

namespace cfgini
{
// Off: every one of our keys holds the player's value and there is no side file.
// On: every one of our keys holds our value and the side file holds the player's values.
// Partial: anything between (some keys ours, a side file without our values, a key the player changed after On).
// NoFile: config.ini cannot be read or has no [DisableSystems] section.
enum class State
{
	Off,
	On,
	Partial,
	NoFile
};

struct Result
{
	bool ok;
	std::string error;              // empty when ok; one line otherwise
	std::vector<std::string> keys;  // On/Off: the keys written (Off also lists keys left alone, prefixed "kept:", and,
	                                // without a usable side file, the keys left at ours, prefixed "left:");
	                                // Status: the keys that differ from the state returned
	std::string note;               // Off (issue 0378): one line when there was no usable side file (config.ini left as
	                                // it is); empty otherwise. RendererOff (issue 0030): the same, or that the options
	                                // file was edited after On and only its ServerRenderer line was put back
};

// Stores the player's values of our keys in the side file, then writes ours. Idempotent: a second On with the side file
// in place keeps the side file's values (they are the player's) and only makes sure our values are written.
Result On(const std::wstring& configIni);

// Puts the player's values back, only for our keys and only where the key still holds our value (a key the player
// changed after On is left alone and named in keys as "kept:<key>"); then removes the side file. Idempotent: Off with no
// side file and no key of ours set is ok and writes nothing.
// Without a usable side file (issue 0378, 2026-11-15) config.ini is left as it is: no key is written (a key at 1 may be
// the player's own value, which only the side file could tell apart from ours); such keys are named "left:<key>", and
// note says why. An unusable side file is moved aside to vp64-config-side.ini.unusable (kept for a report; `On` would
// refuse it). Ok in both cases.
Result Off(const std::wstring& configIni);

// Reads config.ini and the side file; writes nothing. detail (may be null) gets the keys that differ.
State Status(const std::wstring& configIni, Result* detail);

// Our keys, in darkred.py's order.
const std::vector<const char*>& OurKeys();

// The former keys (issue 3805): keys of an earlier build's list (Waves, Minimap, MovementPath; Particle, Combat,
// Projectile since issue 0368). An older side file's lines
// for them are accepted; On and Off put such a key back to the side file's value where config.ini still holds ours
// (Result.keys "former:<key>"), and Status calls that state partial. Never written as ours.
const std::vector<const char*>& FormerKeys();

// The game's config.ini, in this order: VPPROXY_TEST_CONFIG_INI=<path> (tests only, as proxy_rt_logic.cpp); else
// C5_DOCUMENTS=<folder> + \My Games\Sid Meier's Civilization 5\config.ini (a test instance: the harness sets it for
// the EXE, whose shim redirects Documents there, and the helper inherits it but is not under the shim, so without this
// it would reach the real Documents folder); else Documents through SHGetFolderPathW(CSIDL_PERSONAL), the call the EXE
// itself uses. False when no path can be formed (the file itself need not exist).
bool ConfigIniPath(std::wstring* out);

// The side file's path for a config.ini path: the same folder, vp64-config-side.ini.
std::wstring SidePath(const std::wstring& configIni);

// ---- VP64Options.ini's [Renderer] ServerRenderer (issue 0030; the MC's one-switch ruling of 2026-11-16) -------------
// vp64helper on / off is the one switch of the shell configuration: on writes our config.ini keys (above) and
// ServerRenderer = 1 into the patch's options file; off puts both back. The player's file as it was before On, and the
// bytes On left, are kept in vp64-options-side.ini beside the options file.
//
// On: ServerRenderer already 1 with a usable side file: nothing written (idempotent). Otherwise the record is written
//     first (the whole file as hex; "existed=0" when there was none), then the file: the deciding line (the last
//     "ServerRenderer = ..." line, the one the options parser obeys) is replaced by "ServerRenderer = 1"; else the
//     commented default line the proxy writes ("; ServerRenderer = 0"); else the line goes in after [Renderer]; else
//     "[Renderer]" and the line are appended; no file: a new one with a comment, [Renderer] and the line. A 1 already
//     there without a side file is the player's own: recorded, nothing written. Refused (nothing changed): the file is
//     read-only, UTF-16, holds NUL bytes, is larger than 64 KB (the proxy would not read it), or the side file exists but
//     is unusable. keys: "ServerRenderer" when written.
// Off: the file still byte for byte as On left it: On's record comes back byte for byte (the file removed when there was
//     none). The player edited the file after On: where the deciding line is still On's line it is put back as it was
//     (removed when On added it), every other edit kept (note says so); ServerRenderer changed by the player after On:
//     nothing written, keys "kept:ServerRenderer". Then the side file goes. Without a usable side file nothing is
//     written (note says why; an unusable one is moved aside to vp64-options-side.ini.unusable). keys: "ServerRenderer"
//     when restored.
// Status: On = side file and ServerRenderer 1; Off = no side file and ServerRenderer 0 (or no file); Partial otherwise
//     (the player changed it after On, an unusable side file, or 1 set by hand without the switch).
Result RendererOn(const std::wstring& optionsIni);
Result RendererOff(const std::wstring& optionsIni);
State RendererStatus(const std::wstring& optionsIni, Result* detail);

// The scanner behind these three takes the key and section as parameters since ticket 4931 (SetOptionLine below); the
// ServerRenderer functions call it with [Renderer] ServerRenderer, so their behaviour is unchanged.
// The value of [Renderer] ServerRenderer that the options parser (options/src/vp64_options.cpp Parse) takes from the
// text: the last line of the key decides; no line, an empty, invalid or out-of-range value give the default 0.
// deciding (may be null): set when a line of the key exists.
int RendererValue(const std::string& text, bool* deciding);

// ---- One option's line in VP64Options.ini (ticket 4931; docs/extras/flags-and-messages-design.md 1.7) -----------------
// The value the options parser takes for any key: the last line of the key decides (in any section, as the parser);
// a whole number in [minV, maxV] is taken, an empty, invalid or out-of-range value and no line give defV. deciding (may
// be null): set when a line of the key exists. RendererValue is OptionValue(text, "ServerRenderer", 0, 1, 0, deciding).
int OptionValue(const std::string& text, const char* key, int minV, int maxV, int defV, bool* deciding);

// Writes "Key = value" with RendererOn's placement rules: the deciding line (the last "Key = ..." line, in any section)
// replaced; else the commented default line ("; Key = d") replaced; else the line put in after the first [section];
// else "[section]" and the line appended; no file: a new one with a comment, [section] and the line. A deciding line
// that already holds value: nothing written (keys empty). Refused, nothing changed: the file is read-only, UTF-16,
// holds NUL bytes, or is larger than 64 KB; a key or section that is not plain. Written atomically (temp + rename).
// No side file: this is the player's choice, which nothing puts back (vp64helper off never touches it; the patch's
// removal deletes the whole file). keys: the key when written.
Result SetOptionLine(const std::wstring& optionsIni, const char* section, const char* key, int value);

// [Extras] EnableVisualAdditions (ticket 4931): the one line the installer writes (vp64helper visual-additions on|off).
// A copy of options/src/vp64_options.cpp's row (the helper does not link the loader); cfgini_unit.cpp checks the copy.
const char* const kExtrasSection = "Extras";
const char* const kVisualAdditionsKey = "EnableVisualAdditions";
const int kVisualAdditionsDefault = 0;
const int kVisualAdditionsMin = 0;
const int kVisualAdditionsMax = 1;

// The options file for a config.ini path: VP64_OPTIONS=<path> when set (as the proxy, options/src/vp64_options.cpp
// DefaultPath), else VP64Options.ini beside config.ini (the proxy's Documents folder is config.ini's).
std::wstring OptionsIniPath(const std::wstring& configIni);

// The side file's path for an options file: the same folder, vp64-options-side.ini.
std::wstring OptionsSidePath(const std::wstring& optionsIni);
}  // namespace cfgini
