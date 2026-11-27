// overlay_tap.h - the Terrain Overlay target tap inside shell_composite.dll (epic 0167, issue 3703; decision 1 route a,
// approved by the MC 2026-11-14, MC #696).
//
// Interface committed by the team-0167 lead (E2); filled in by issue 3703 (owner of overlay_tap.h / overlay_tap.cpp;
// 3703 added OverlayTap_OnOMSet for the verify mode). Keys and test modes: the head of overlay_tap.cpp.
// Model: bake_tap.h. The composite calls these functions; each is cheap when the tap is off (one test of a flag, and one
// write of state OFF) and never throws or faults into the EXE: overlay_tap.cpp checks the EXE by byte patterns before
// its first read, guards every read of EXE memory with SEH, and disables itself on a fault. It calls no EXE function
// (question 0098 not touched; the Architect's rule R2): only reads of [0xDA12A4], the view table 0x414C348 and
// 0xDA1690 + 0x40 v, and D3D11 calls on the EXE's own device. Layout of what it publishes: VPShellOverlayTap,
// VPShellOverlaySection, VPShellOverlaySlot in shell/common/shell_shared.h. What it reads and why:
// research/05-graphics-composition/overlay-system.md sections 3, 4, 7.
//
// Failures (decision 5, D132): a tap that cannot install at game start (pattern, handle, description, shared texture)
// publishes VPOVL_FAILED with its reason and logs LOUD once; the server turns that into D125's route. A fault during play
// (the target lost, an exception) is logged LOUD, counted, and the tap goes on; a copy missed because the previous one
// is still in flight is a counted lag (the first 3 logged, never LOUD; issue 0194, MC 2026-11-14 20:08).
//
// The kill switch is overlay_tap=0 (Config of shell_composite.cpp, re-read when the ini changes).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"

struct OverlayTapEnv
{
	ID3D11Device* dev;            // the EXE's device (the composite's target device)
	ID3D11DeviceContext* ctx;     // its immediate context
	VPShellHeader* hdr;           // the mapped main section (null when the section is not open yet)
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (presentCount, gridSeq)
	int enabled;                  // overlay_tap=1 in the ini (0 = the kill switch: state VPOVL_OFF)
	void (*log)(const char* fmt, ...);
	int camOk;                    // the camera read of this Present was OK (VPCAM_OK)
	int sceneThisFrame;           // this frame executed the scene-clear list (a map frame, not the load screen)
};

// Render thread, every Present of the target swap chain, after the camera block was published: publishes the slot
// whose copy has completed, then copies this Present's target with its OverlayProj array into a free slot.
void OverlayTap_OnPresent(const OverlayTapEnv& env);

// Any context's OMSetRenderTargets, after the original call (the composite's own calls filtered out). Used only with
// overlay_verify=N: a bind of the target's view records OverlayProj at that moment, which the next Present compares with
// its own read (open question 3 of overlay-system.md 8). One flag test otherwise.
void OverlayTap_OnOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs);

// The composite releases its device resources (resize, retarget, unload): drop the slots' textures and the section.
void OverlayTap_Shutdown(const char* why);
