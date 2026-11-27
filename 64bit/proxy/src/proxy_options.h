// proxy_options.h - the parts of proxy_options.cpp that ticket 0624 tests offline (proxy/tests/options_boot/): the
// release's option set (VP64\release-options.txt of an i3 payload, ticket 0574), the text of an options file created
// with it, and the renderer guard's rule. Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// Why (ticket 0624, research/07-architecture-decisions/options-file.md section 7): a player who deleted VP64Options.ini
// got it re-created at the table's defaults, ServerRenderer = 0, while config.ini still held the patch's configuration
// (a D33 session): the EXE's fog of war, forests, decals and yield icons off and the server drawing nothing (the
// EXE's terrain itself is drawn: Terrain is no D33 key; live 2026-11-20). Now, in a D33 session, a missing file is
// created with the release's option set and ServerRenderer = 1; without a usable set it is not created, and the guard
// (OptionsGuard, called once by Boot in proxy_rt.cpp) refuses every D33 boot whose options say ServerRenderer = 0:
// the D125 route (no play, the D118 report and popup, vp64helper failed).
#pragma once
// The STL layout must match the other proxy TUs (see proxy_rt_logic.h).
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include <stddef.h>
#include <string>
#include <vector>

namespace ProxyOptions
{
// The set's file name in the pack's folder (vpproxy.ini x64_dir; the payload's VP64\, ticket 0574).
#define VP64_RELEASE_SET_FILE "release-options.txt"
enum { kMaxSetBytes = 64 * 1024 };

struct SetRow
{
	int id;               // Vp64Opt::Id
	std::string section;  // as the table spells it
	std::string key;      // as the table spells it
	int value;
};

// Parses the option set: one row "Section<TAB>Key<TAB>Value" per option (the format build_installer.py writes and
// install.iss reads); empty lines and lines whose first non-blank character is '#' are skipped; a UTF-8 byte order
// mark is skipped; LF or CRLF. Refused (false, *why one clause): a row that is not three tab-separated fields of a name,
// a name and a whole number (optional '-', 1 to 9 digits); [Renderer] ServerRenderer (vp64helper on sets it, as
// install.iss refuses it); a key given twice; no row at all; and, beyond install.iss (W31 of epic 0042), a key the
// options table does not have, a key in another section than its home section, or a value outside its range.
bool ParseSet(const char* text, size_t len, std::vector<SetRow>* rows, std::string* why);

// Reads and parses <dir>\release-options.txt. 1 = a usable set; 0 = no such file; -1 = unreadable, too large or
// refused by ParseSet. *path gets the file's path, *why the reason when not 1.
int ReadSet(const std::string& dir, std::vector<SetRow>* rows, std::string* path, std::string* why);

// The text of a new options file: the default file's text (Vp64Opt::DefaultFileText) with, for every row, the option's
// commented default line "; Key = <default>" replaced by "Key = <value>", and the same for ServerRenderer = 1 when
// serverRendererOn. False (*why) when an option's commented default line is not found exactly once.
bool TextWithSet(const std::vector<SetRow>& rows, bool serverRendererOn, std::string* text, std::string* why);

// Writes text to path with CREATE_NEW (never replaces a file); a half-written file is deleted. 0 = written, 1 = the
// file exists (nothing written), 2 = failed (*error = GetLastError()).
int CreateWithText(const wchar_t* path, const std::string& text, unsigned long* error);

// The guard's rule (ticket 0624): "" = the boot may go on; else one clause naming why it must not. A D33 session that
// writes server.cfg (no vpproxy.ini server_config=, which is a test's own config) and has ServerRenderer = 0 would show a
// degraded world (the game's fog of war, forests, decals and yield icons off, nothing of the server's), unless the test
// switch crashlogs\shell.helper loads the composite.
std::string GuardWhy(bool d33, const std::string& sessionWhy, bool writesServerCfg, bool helperMarker, int serverRenderer,
                     bool serverRendererFromFile, const std::string& createNote);

// The composite guard's rule (ticket 0624 item 5, MC #8762): "" = the boot may go on; else one clause naming why it
// must not. A D33 session with ServerRenderer = 1 whose product composite was refused (refused = ProxyShell::
// RefusedComposite(), "" when not) shows the same degraded world as ServerRenderer = 0: nothing would draw the server's
// picture into the game's. The same two test hooks skip it (writesServerCfg false = server_config=; helperMarker).
std::string CompositeGuardWhy(bool d33, const std::string& sessionWhy, bool writesServerCfg, bool helperMarker,
                              int serverRenderer, const std::string& refused);
}  // namespace ProxyOptions
