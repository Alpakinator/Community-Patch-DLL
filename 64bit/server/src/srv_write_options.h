// srv_write_options.h - server.exe's build-time verbs for the installer (one-off 9081). Hand-written.
//
//   server.exe --write-options <ini path> --write-options-schema <tsv path>     (either verb alone, or both)
//
// --write-options writes exactly Vp64Opt::DefaultFileText (the text the proxy creates a missing VP64Options.ini with:
// CRLF, ASCII). --write-options-schema writes the option table, one row per option with a non-empty key, in table
// order, UTF-8 with LF line ends and no header:
//   id <TAB> section <TAB> key <TAB> default <TAB> min <TAB> max <TAB> category <TAB> needsKey or '-' <TAB> title <TAB>
//   help
// category: 0 none, 1 switch, 2 bugfix, 3 visual (Vp64Opt::Category). title is the row's short title (Def::title, the
// label of the installer's options page). title and help are the table's text with a backslash written as \\, a line
// break as \n, a carriage return as \r and a tab as \t (the help has none of the last three today).
// tools/installer/build_installer.py runs the staged payload's server.exe with both verbs at installer build time, so
// that the installer's options page comes from this build's table and never from a hand-kept list.
//
// Exit codes: 0 written; 1 a file could not be written (one line on stderr; a half-written file is deleted); 2 usage
// (one line on stderr). Nothing else happens: no log, no window, no DLL is loaded, no other file is touched.
// SrvWriteOptionsVerb is called first thing in main() (server/src/srv_main.cpp), before the window guard. The hermetic
// installer tests link this file and options/src/vp64_options.cpp into a stand-in server.exe
// (tools/installer/tests/standin_server.cpp).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// -1 when argv names neither verb (main() goes on as before); else the exit code of the verb run (0, 1 or 2).
int SrvWriteOptionsVerb(int argc, char** argv);
