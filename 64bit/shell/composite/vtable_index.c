// Origin (rule 13, decision D22): the project's fork of Community-Patch-DLL, branch claude/shell-gates-d8e422,
// path experiments/shell-gates/compositor/src/vtable_index.c, copied at branch head c60138b82 (file last changed in a9d8328e2).
// Copied into shell/composite/vtable_index.c on 2026-11-10 (issue 3001); later changes are in this repository's history.

/* vtable_index.c - vtable slot numbers taken from the SDK's C interface
 * declarations, so shell_composite.cpp never hard-codes a slot number.
 * Issue 3006: the slots are only used to READ implementation addresses from real objects (the targets of the
 * inline hooks); no vtable is written any more. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <stddef.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#define SLOT(vtbl, member) ((unsigned)(offsetof(vtbl, member) / sizeof(void*)))

const unsigned VT_SwapChain_Present = SLOT(IDXGISwapChainVtbl, Present);
const unsigned VT_SwapChain_GetBuffer = SLOT(IDXGISwapChainVtbl, GetBuffer);
const unsigned VT_SwapChain_ResizeBuffers = SLOT(IDXGISwapChainVtbl, ResizeBuffers);
const unsigned VT_SwapChain1_Present1 = SLOT(IDXGISwapChain1Vtbl, Present1);
const unsigned VT_Factory_CreateSwapChain = SLOT(IDXGIFactoryVtbl, CreateSwapChain);
const unsigned VT_Factory2_CreateSwapChainForHwnd = SLOT(IDXGIFactory2Vtbl, CreateSwapChainForHwnd);
const unsigned VT_Device_CreateDeferredContext = SLOT(ID3D11DeviceVtbl, CreateDeferredContext);
const unsigned VT_Device1_CreateDeferredContext1 = SLOT(ID3D11Device1Vtbl, CreateDeferredContext1);
const unsigned VT_Device2_CreateDeferredContext2 = SLOT(ID3D11Device2Vtbl, CreateDeferredContext2);
const unsigned VT_Device3_CreateDeferredContext3 = SLOT(ID3D11Device3Vtbl, CreateDeferredContext3);
const unsigned VT_Context_ClearRenderTargetView = SLOT(ID3D11DeviceContextVtbl, ClearRenderTargetView);
const unsigned VT_Context_ClearDepthStencilView = SLOT(ID3D11DeviceContextVtbl, ClearDepthStencilView);
const unsigned VT_Context_OMSetRenderTargets = SLOT(ID3D11DeviceContextVtbl, OMSetRenderTargets);
const unsigned VT_Context_ExecuteCommandList = SLOT(ID3D11DeviceContextVtbl, ExecuteCommandList);
const unsigned VT_Context_FinishCommandList = SLOT(ID3D11DeviceContextVtbl, FinishCommandList);
const unsigned VT_Context_OMSetRenderTargetsAndUAVs = SLOT(ID3D11DeviceContextVtbl, OMSetRenderTargetsAndUnorderedAccessViews);
/* epic 0040, issue 4752: the unit-draw filter (unit_filter.cpp) */
const unsigned VT_Device_CreateVertexShader = SLOT(ID3D11DeviceVtbl, CreateVertexShader);
const unsigned VT_Device_CreatePixelShader = SLOT(ID3D11DeviceVtbl, CreatePixelShader);
const unsigned VT_Context_VSSetShader = SLOT(ID3D11DeviceContextVtbl, VSSetShader);
const unsigned VT_Context_PSSetShader = SLOT(ID3D11DeviceContextVtbl, PSSetShader);
const unsigned VT_Context_DrawIndexedInstanced = SLOT(ID3D11DeviceContextVtbl, DrawIndexedInstanced);
const unsigned VT_Context_DrawIndexed = SLOT(ID3D11DeviceContextVtbl, DrawIndexed);
const unsigned VT_Context_Draw = SLOT(ID3D11DeviceContextVtbl, Draw);
const unsigned VT_Context_DrawInstanced = SLOT(ID3D11DeviceContextVtbl, DrawInstanced);
const unsigned VT_Context_DrawIndexedInstancedIndirect = SLOT(ID3D11DeviceContextVtbl, DrawIndexedInstancedIndirect);
const unsigned VT_Context_DrawInstancedIndirect = SLOT(ID3D11DeviceContextVtbl, DrawInstancedIndirect);
const unsigned VT_Context_DrawAuto = SLOT(ID3D11DeviceContextVtbl, DrawAuto);
/* epic 0561, ticket 4874: the model tap (unit_filter.cpp): resource identity at creation, the Unmap copy */
const unsigned VT_Device_CreateBuffer = SLOT(ID3D11DeviceVtbl, CreateBuffer);
const unsigned VT_Device_CreateTexture2D = SLOT(ID3D11DeviceVtbl, CreateTexture2D);
const unsigned VT_Context_Map = SLOT(ID3D11DeviceContextVtbl, Map);
const unsigned VT_Context_Unmap = SLOT(ID3D11DeviceContextVtbl, Unmap);
const unsigned VT_Context_UpdateSubresource = SLOT(ID3D11DeviceContextVtbl, UpdateSubresource);
const unsigned VT_Context_CopyResource = SLOT(ID3D11DeviceContextVtbl, CopyResource);
const unsigned VT_Context_CopySubresourceRegion = SLOT(ID3D11DeviceContextVtbl, CopySubresourceRegion);
