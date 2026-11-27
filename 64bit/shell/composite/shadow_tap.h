// shadow_tap.h - the model shadow map tap inside shell_composite.dll (epic 0169, issue 4304, ticket 0312; C1 accepted by
// the MC 2026-11-15, #1804: data reads and D3D copies only, guarded byte patterns, no EXE code called or written).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Model: overlay_tap.h. Layout of what it publishes:
// shell/common/shell_shadow_tap.h (its own section). What it reads and why: research/05-graphics-composition/
// terrain-surface.md sections 9, 11 and 11.9.
//
// The composite calls these functions; each is cheap when the tap is off (state OFF published once per Present) and
// never faults into the EXE: shadow_tap.cpp checks the EXE by byte patterns before its first read, guards every read of
// EXE memory with SEH, and calls no EXE function: only reads of [0xDA12BC], [0xDA12B8], [0xD90F9C], [0xD90FA8],
// [0xD90FA4], 0xDA1790 + 0x40 v and the view table 0x414C348, and D3D11 calls on the EXE's own device.
//
// Failures (D125, D132): a tap that cannot install (pattern, handle, description, quality, shared texture, section)
// publishes VPSHD_FAILED with its reason and a text, LOUD once; the server turns that into D125's route (4308). A fault in
// play (the texture gone or with an unexpected description, an exception) is LOUD (rate-limited), counted, and the last
// good frame stays; a copy missed because the previous one is still in flight is a counted lag (the first 3 logged).
// The kill switch is shadow_tap=0 (Config of shell_composite.cpp, re-read when the ini changes).
// Ticket 0385: while no reader acks the published frames for shadow_idle_ms (2000; no server, render_shadow=0), the copies
// pause to one probe copy per shadow_probe_ms (250); the reader's next ack resumes them (shadow_tap.cpp, the reader gate).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"
#include "shell_shadow_tap.h"

struct ShadowTapEnv
{
	ID3D11Device* dev;            // the EXE's device (the composite's target device)
	ID3D11DeviceContext* ctx;     // its immediate context
	VPShellHeader* hdr;           // the mapped main section (null when it is not open yet: no copies then)
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (presentCount, gridSeq)
	int enabled;                  // shadow_tap=1 in the ini (0 = the kill switch: state VPSHD_OFF)
	void (*log)(const char* fmt, ...);
	int camOk;                    // the camera read of this Present was OK (VPCAM_OK)
	int sceneThisFrame;           // this frame executed the scene-clear list (a map frame, not the load screen)
	uint32_t bbWidth, bbHeight;   // the target swap chain's back buffer (quality 1: ShadowRaw has its size); 0 = unknown
};

// Render thread, every Present of the target swap chain, after the camera block was published: publishes the slot whose
// copy has completed, then on a map frame copies this Present's ShadowBlur with its matrices and strength into a free slot.
void ShadowTap_OnPresent(const ShadowTapEnv& env);

// The composite releases its device resources (resize, retarget, unload): drop the slots' textures (the section stays).
void ShadowTap_Shutdown(const char* why);
