// bake_tap.h - the tile-cache tap inside shell_composite.dll (epic 3200, issue 3204; decision D58 point 1a).
//
// Interface committed by the team lead; issue 3204 (owner of bake_tap.h / bake_tap.cpp) fills it in. The composite DLL
// calls these three functions; each must be cheap when the tap is off (one test of a flag) and must never throw or
// fault into the EXE: bake_tap.cpp guards its own reads of EXE memory with SEH and disables itself on a fault.
// Layout of what it publishes: VPShellBakeTap, VPShellBakeManifest, VPShellBakeSlot in shell/common/shell_shared.h.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"

struct BakeTapEnv
{
	ID3D11Device* dev;            // the EXE's device (the composite's target device)
	ID3D11DeviceContext* ctx;     // its immediate context
	VPShellHeader* hdr;           // the mapped main section (null when the section is not open yet)
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	const VPShellCamera* cam;     // the camera block just published (grid constants, gridSeq)
	int enabled;                  // bake_tap=1 in the ini
	void (*log)(const char* fmt, ...);
};

// Render thread, every Present of the target swap chain, after the camera block was published.
void BakeTap_OnPresent(const BakeTapEnv& env);

// Any thread that calls OMSetRenderTargets on the EXE's immediate context, after the original call returned
// (the composite's own calls are filtered out before this). Lets the tap see a bake's render targets being bound.
void BakeTap_OnOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv);

// The composite releases its device resources (resize, retarget, unload): drop every shared texture and the manifest.
void BakeTap_Shutdown(const char* why);
