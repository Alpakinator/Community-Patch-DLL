// decal_tap.h - the decal tap inside shell_composite.dll (epic 0166, issue 4102; route (a) of terrain-decals.md 0.2).
//
// Copies the EXE's TerrainDecalSystem records (every live decal, its vertex bytes and the names of its three textures)
// into the sections of shell/common/shell_decal_tap.h whenever they change, for server.exe (4103). The composite calls
// these functions; each is cheap when the tap is off (one test of a flag, and the control block's state OFF written
// once) and never throws or faults into the EXE: decal_tap.cpp checks the EXE by byte patterns before its first read,
// guards every read of EXE memory with SEH and disables itself on a fault. It calls no EXE function and writes nothing
// into the EXE (D132 R2). What it reads and why: research/05-graphics-composition/terrain-decals.md sections 1, 5, 6
// and 8 (the names), section 9 (the tap as built).
//
// Keys it reads itself from [composite] of shell_composite.ini (or VPSHELL_<KEY>):
//   decal_budget_ms (2)        the time a Present may spend on reading, building and writing a generation: a step runs
//                              only while its estimated cost still fits (issue 4110; at least one step per Present)
//   decal_settle_frames (2)    Presents with an unchanged change signal before a moved batch is read
//   decal_max_wait_ms (500)    ... but a change waits at most this long (P13: a road reaches the image within ~1 s)
//   decal_net_ms (1000)        the safety net: every this many ms the live records of every batch are hashed
//   decal_max_kb (VPSHELL_DECAL_MAX_BYTES / 1024)  image size limit (tests lower it)
//   decal_test=<mode>          test only: a synthetic EXE in this DLL's memory (decal_tap.cpp, kTestNames; huge = the
//                              huge save's size)
//   decal_test_midbuild=N      test only (huge): one batch changes during each of the next N builds
// The kill switch is decal_tap=0 (Config::decalTap of shell_composite.cpp, re-read when the ini changes): state OFF.
#pragma once
#include <windows.h>
#include <stdint.h>
#include "shell_shared.h"

struct DecalTapEnv
{
	VPShellHeader* hdr;           // the mapped main section (null when not open yet): only its camera block is read
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (gridSeq = the load; gridValid = a map is loaded)
	int enabled;                  // decal_tap=1 in the ini (0 = the kill switch: state VPDTAP_OFF)
	void (*log)(const char* fmt, ...);
};

// Render thread, every Present of the target swap chain, after the camera block was published.
void DecalTap_OnPresent(const DecalTapEnv& env);

// The composite unloads or releases its resources: nothing of the tap depends on the device; the sections stay (the
// last READY image must stay readable). Called for symmetry with the other taps; a no-op except for the test world.
void DecalTap_Shutdown(const char* why);
