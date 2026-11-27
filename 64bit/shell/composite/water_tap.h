// water_tap.h - the water mesh tap inside shell_composite.dll (epic 3600, issue 3603; decision 1 route a).
//
// Interface committed by the team-3600 lead; filled in by issue 3603 (owner of water_tap.h / water_tap.cpp). Model:
// bake_tap.h. The composite calls these functions; each is cheap when the tap is off (one test of a flag, and one write
// of state OFF) and never throws or faults into the EXE: water_tap.cpp checks the EXE by byte patterns before its first
// read, guards every read of EXE memory with SEH, and disables itself on a fault. It calls no EXE function (question
// 0098 not touched): only reads and D3D11 calls on the EXE's own device. Layout of what it publishes: VPShellWaterTap,
// VPShellWaterImage, VPShellWaterRecord in shell/common/shell_shared.h. What it reads and why:
// research/05-graphics-composition/water-system.md section 9.
//
// Keys it reads itself from [composite] of shell_composite.ini (or VPSHELL_<KEY>), at each copy start:
//   water_delay_frames (60)  Presents with camera OK and an unchanged grid seq before a copy starts
//   water_scene_frames (30)  frames with the scene-clear list since the grid seq changed (a reload keeps the camera OK
//                            through the load screen, which has no scene clear). Neither applies when the composite's
//                            load gate says first load (WaterTapEnv::firstLoadOpen, ticket 0408)
//   water_ack_timeout_ms (VPSHELL_WATERTAP_ACK_TIMEOUT_MS)
//   water_max_kb (VPSHELL_WATER_MAX_BYTES / 1024)  image size limit (tests lower it)
//   water_test=<mode>        test only: a synthetic EXE in this DLL's memory (water_tap.cpp, kTestNames)
// The kill switch is water_tap=0 (Config::waterTap of shell_composite.cpp, re-read when the ini changes).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"

struct WaterTapEnv
{
	ID3D11Device* dev;            // the EXE's device (the composite's target device)
	ID3D11DeviceContext* ctx;     // its immediate context
	VPShellHeader* hdr;           // the mapped main section (null when the section is not open yet)
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (gridSeq, map size)
	int enabled;                  // water_tap=1 in the ini (0 = the kill switch: state VPWTAP_OFF)
	void (*log)(const char* fmt, ...);
	int camOk;                    // the camera read of this Present was OK (VPCAM_OK)
	int sceneThisFrame;           // this frame executed the scene-clear list (a map frame, not the load screen)
	int firstLoadOpen;            // ticket 0408: the composite's load gate says this grid is a process's first load whose
	                              // camera came up after the grid and has been OK for tap_first_delay_frames Presents:
	                              // a copy of this load starts now, without water_delay_frames / water_scene_frames
};

// Render thread, every Present of the target swap chain, after the camera block was published. Starts one copy per
// load when the load is over; answers the server's requestSeq; closes the section on ack, timeout or a new load.
void WaterTap_OnPresent(const WaterTapEnv& env);

// The composite releases its device resources (resize, retarget, unload): drop staging buffers and the section.
void WaterTap_Shutdown(const char* why);
