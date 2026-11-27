// fow_models.h - the server's fog-of-war mask behind the white handle, for the EXE's model draws (ticket 0339, follow-up
// of epic 0168, decision 3 (ii); MC answer #2058). Written 2026-11-15 for this repository (GPL-3.0-or-later).
// Layout and protocol: shell/common/shell_fowmask.h; producer: server/src/render/srv_fow_mask_pub.cpp; behaviour and the
// measurements: research/05-graphics-composition/fog-of-war.md section 9.
//
// What it does, at every Present of the target (the EXE's main thread, between frames):
//   - the install (the first map frame): the EXE's byte patterns (the view table, the model passes' copies of OverlayProj
//     and FOWAnim, the building builder's mask and base copies), the white handle from the proxy (VP64SHELL_WHITE, held in
//     [pxw_fow_mask]) and its view-table entry (pxw_tex_views + 12 h, a D3D11 view: the expected old value of the swap), the cloud
//     base from the proxy (VP64SHELL_FOWBASE, held in [pxw_fow_base]; VP64SHELL_FOWBASE_WHY = the proxy's refusal), and
//     FOWAnim at fow_anim (expected old value: zero, or FluidFOW's start value 0.002 x 4);
//   - every Present: opens the server's section `Local\VPShellFowMask.<own pid>`, says `wanted`, copies the newest slot
//     (all mips) into a texture of its own on the EXE's device, and after the first copy swaps the white handle's view
//     pointer for that texture's view (compare-and-swap against the white view, AddRef'd for the table); writes the
//     newest mask's FOWAnim into fow_anim (checked against the value written last); measures the window lag (the
//     EXE's OverlayProj of the frame against the one the bound mask was built with).
//   - ticket 0394 (fog-of-war.md 9.5): the server's mask is copied as delivered (g_src); the texture the models read is
//     g_src moved to the frame's window (the EXE snaps origin and size to 64 world units): a whole-texel offset per mip
//     for a pure translation with a constant fill, else the resample pass (a bilinear draw into mip 0 through the two
//     windows' uv map, then GenerateMips; programs built in since ticket 0407: fow_models_resample.hlsl compiled by fxc in
//     shell/build.cmd, no d3dcompiler_47 at run time); the strip that comes into
//     view holds the fill. Made at the Present for the frame just presented's window and again at the next frame's
//     first ExecuteCommandList when its window changed (FowModels_OnFrameStart). A jump over half the mask or views that
//     do not move together leave the mask as built (0339's lag).
// Failures (MC #2058: every write checked against its expected old value; a mismatch is the D118 / D125 route with a
// named reason, never a silent skip): a pattern refusal, no usable white view, the proxy's base-layer refusal, a write
// whose old value does not match, a slot texture that cannot be opened or a local texture that cannot be made, an
// access violation, no mask within fow_models_wait_ms of map frames (ticket 0491: and at least 60 map frames presented, else
// within the slow-load bound fow_models_wait_ms x 6, for an EXE still loading): the view and FOWAnim are put back, consumerState
// FAILED with failReason and failText in the section (the server's fog fails its install: the report, the popup, the
// end of the game), one LOUD line. Without the white mask (VP64SHELL_WHITE not set: WhiteFogMask = 0) the model fog is
// OFF (today's dark-red models), not a failure. The server stopping (STOPPED) puts the white view back (counted, LOUD once
// per episode) until masks come again. Ticket 0415: the server's heartbeat still for fow_models_stale_ms of map frames (its
// render thread held up: a decal apply, a field on a loaded board) is a late episode: the last mask stays bound (it is the
// fog the player has), one line at its start and one at its end, LOUD once when it lasts fow_models_late_loud_ms; a new map
// load (the camera block's gridSeq) puts the white view back until a mask the server built after the load is copied.
// Shutdown (a resize, a retarget) and the process's end put the view and FOWAnim back and log it.
//
// Keys ([composite] of shell_composite.ini, or VPSHELL_<KEY>): model_fow (FOW_MODELS_DEFAULT; the proxy reads the same
// switch to load the cloud base), fow_models_wait_ms (30000), fow_models_stale_ms (2000), fow_models_late_loud_ms (30000;
// 0 = never LOUD; ticket 0415), fow_models_trace (0: 1 = one
// CSV row per map frame into shell_composite.fowmodels.csv), fow_models_test (test only: a synthetic EXE in this DLL),
// fow_models_section (test only: another section name); ticket 0394: fow_models_shift (1; 0 = the mask's own window, the
// comparison arm), fow_models_fill (edge | zero | unseen | seen: the strip's value; edge = the nearest known texel past
// the server's rim), fow_models_verify (0; 1 = diagnostic: the moved mask read back against the mask built for that
// window, at most every fow_models_verify_ms (1000); the frame waits for it).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"

// The model fog's default; proxy_shell.cpp VPSHELL_MODEL_FOW_DEFAULT must say the same.
#define FOW_MODELS_DEFAULT 1

struct FowModelsEnv
{
	ID3D11Device* dev;            // the EXE's device
	ID3D11DeviceContext* ctx;     // its immediate context
	uint32_t exeBase;             // module base of the EXE
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (presentCount)
	void (*log)(const char* fmt, ...);
	int camOk;                    // the camera read of this Present was OK
	int sceneThisFrame;           // this frame executed the scene-clear list (a map frame)
};

// Render thread (the EXE's main thread), every Present of the target, after the camera block was published.
void FowModels_OnPresent(const FowModelsEnv& env);
// Ticket 0394: the frame's first ExecuteCommandList on the immediate context, before the original call (the EXE's main
// thread; the frame's OverlayProj is final by then). Moves the local mask to the frame's window when it changed.
void FowModels_OnFrameStart(ID3D11DeviceContext* ctx);
// The composite releases its device resources (resize, retarget): the view and FOWAnim put back, our textures released.
void FowModels_Shutdown(const char* why);
// DLL_PROCESS_DETACH at the process's end: the view entry and FOWAnim put back (no D3D call); log may be null.
void FowModels_ProcessExit(void (*log)(const char* fmt, ...));
