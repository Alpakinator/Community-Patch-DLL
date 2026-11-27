// forest_tap.h - the forest tap inside shell_composite.dll (ticket 0420, package T0 of research/05-graphics-composition/
// trees.md section 5: route A's oracle). Written 2026-11-16 for this repository (GPL-3.0-or-later).
//
// A read-only copy of the EXE's Forest system (Forest_System, the static object forest_system) into a file per load, so that
// the server's own trees (route A) can be seeded with the very layouts and plot assignments of the session and compared
// tree for tree: the 20 random layouts (Forest+0x98: a vector of 12-byte vectors of 28-byte trees) and the plot records
// (the indexed list at Forest+0x78: records of 0x8C bytes at +0x80, head +0x90). What the fields mean, and why each byte
// pattern below is checked: trees.md sections 2.2, 2.3, 2.5 and 7 (behaviour in own words, D83). Nothing of the EXE is
// in the tap's files but the patterns' few bytes; the files it writes hold the session's random data, never EXE code.
//
// The composite calls these functions on its render thread. Each is cheap when the tap is off (one test of a flag) and
// never faults into the EXE: forest_tap.cpp checks the EXE by byte patterns before its first read, guards every read of
// EXE memory with SEH (a fault disables the tap for the process), calls no EXE function and writes nothing into the EXE.
// With Forest off in config.ini (the D33 product today) the system's ready flag (+0x13C) stays 0 and the tap writes
// nothing (one 1-byte read per poll).
//
// What it writes (format: forest_tap.cpp, "The file"; reader: shell/tests/test_forest_tap.py parse_file and
// render/evidence/0420/forest_file.py): <outDir>forest_tap\forest_<pid>_<load>_<generation>.bin, written to a .tmp
// name and renamed. A generation is written when the set (layouts, records with their layout index, cube coordinates,
// kind, built flag and decal count) differs from the last file's and has been the same for forest_settle_polls polls;
// at most one file per forest_min_write_ms, at most forest_max_files per process (then one LOUD line).
//
// Keys it reads itself from [composite] of shell_composite.ini (or VPSHELL_<KEY>), once at its first call:
//   forest_poll_ms (500)          the time between two polls (a poll reads the header, the layouts once per load, and
//                                 the record list)
//   forest_settle_polls (2)       polls with the same set before it is written
//   forest_min_write_ms (5000)    at least this long between two files
//   forest_max_files (200)        files per process
//   forest_test=<mode>            test only: a synthetic Forest object in this DLL's memory (forest_tap.cpp, kTestNames)
// The switch is forest_tap=1 (Config::forestTap of shell_composite.cpp; 0, the default, = nothing read, nothing written).
#pragma once
#include <windows.h>
#include <stdint.h>
#include "shell_shared.h"

struct ForestTapEnv
{
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (gridSeq = the load; gridValid = a map is loaded)
	int enabled;                  // forest_tap=1 in the ini
	const wchar_t* outDir;        // the composite's output folder (ends with a backslash); the files go to <outDir>forest_tap
	void (*log)(const char* fmt, ...);
};

// Render thread, every Present of the target swap chain, after the camera block was published.
void ForestTap_OnPresent(const ForestTapEnv& env);

// The composite unloads or releases its resources: nothing depends on the device; frees the test world.
void ForestTap_Shutdown(const char* why);
