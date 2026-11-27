// ice_tap.h - the ice tap inside shell_composite.dll (epic 0169, issue 4307; route (a) of polar-ice.md 5, accepted at
// the C1 review, MC #1804: data reads and D3D copies of the EXE's resources only, guarded byte patterns, no EXE code
// called or written, D125's route on a mismatch). Written 2026-11-15 for this repository (GPL-3.0-or-later). Model:
// water_tap.h. Layout of what it publishes: shell/common/shell_ice_tap.h (its own sections). What it reads and why:
// research/05-graphics-composition/polar-ice.md sections 3, 5, 7 and 9.
//
// The composite calls these functions; each is cheap when the tap is off (state OFF published once) and never faults
// into the EXE: ice_tap.cpp checks the EXE by byte patterns before its first read, guards every read of EXE memory with
// SEH, and calls no EXE function: only reads of the Ice object 0xD68F9C (+0xFE80 .. +0x1001F), its chunk array, the
// draw records, the renderer's buffer tables, and D3D11 calls on the EXE's own device. Once per load (and after an
// Ice_Reload) it copies the ice VB and IB through staging buffers, never per frame; per Present it reads 20 bytes of the
// object to see a new mesh.
//
// Failures (D125, D132): a pattern mismatch or a refused buffer description publishes VPICE_FAILED with its reason and a
// text, LOUD once; the server turns that into D125's route (4308). A fault while reading the EXE disables the tap for
// the process (EXCEPTION). A layout failure fails this load only.
//
// Ticket 0385 (with 0384): the copy is made only when something can read it (the reader gate), and its heavy part runs
// off the render thread:
//   - the gate: a copy waits while the server that was offered the last image (the same hdr->serverPid, 0 when the
//     section names none, as the ring hand-over leaves it) has not acked that image for ice_unread_ms (a server running
//     without the ice reader, render_ice=0); the unread image stays published. The copy starts as soon as the offered
//     image is acked or the section names another server. Logged once per object state ("deferred", "deferred copy ...
//     starts"). No server at all needs no rule: without the main section no camera is published and no copy is asked for.
//   - the finish: at the Present whose maps succeed, the render thread re-reads the object (CHANGED as before) and hands the
//     mapped staging memory to a worker thread (below-normal priority) that checks every index, builds the image, hashes
//     it and writes the section; a later Present unmaps and publishes READY or the worker's failure. Measured cost before:
//     one Present of 7 ms (Tiny) to 18-21 ms (huge) (polar-ice.md 9).
// Keys it reads itself from [composite] of shell_composite.ini (or VPSHELL_<KEY>), at each copy start:
//   ice_settle_frames (30)   map Presents with an unchanged object (built flag, handles, chunk array) before a copy
//   ice_max_kb (VPSHELL_ICE_MAX_BYTES / 1024)   image size limit (tests lower it)
//   ice_unread_ms (10000)    0 = never defer for an unread image (the gate's second part off)
//   ice_worker (1)           0 = the image built on the render thread in the map Present (the cost before 0385; the
//                            before/after measurement's base arm, shell/evidence/0385/)
//   ice_test=<mode>          test only: a synthetic EXE in this DLL's memory (ice_tap.cpp, kTestNames)
//   ice_test_pattern=<k>     test only (ice_test=patterns): pattern k of the list is made not to match (-1: none)
// The kill switch is ice_tap=0 (Config::iceTap of shell_composite.cpp, re-read when the ini changes; the shipped ini
// holds 0 until 4308 binds the ice in the scene).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"
#include "shell_ice_tap.h"

struct IceTapEnv
{
	ID3D11Device* dev;            // the EXE's device (the composite's target device)
	ID3D11DeviceContext* ctx;     // its immediate context
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (gridSeq, gridValid)
	int enabled;                  // ice_tap=1 in the ini (0 = the kill switch: state VPICE_OFF)
	void (*log)(const char* fmt, ...);
	int camOk;                    // the camera read of this Present was OK (VPCAM_OK): a map frame
	const VPShellHeader* hdr;     // ticket 0385: the mapped main section (null: no server section yet), for the reader gate
};

// Render thread, every Present of the target swap chain, after the camera block was published.
void IceTap_OnPresent(const IceTapEnv& env);

// The composite releases its device resources (resize, retarget, unload): a copy in flight is dropped (staging buffers
// die with the device) and started again; a READY image section does not depend on the device and stays.
void IceTap_Shutdown(const char* why);
