// unit_filter.h - the compositor's unit-draw filter (epic 0040, issue 4752). Written 2026-11-17 for this repository
// (GPL-3.0-or-later). Design and EXE facts: research/05-graphics-composition/unit-draw-filter.md section 3; the table of
// programs: shell/common/unit_programs.h (issue 4751); the mode word: shell/common/shell_shared.h VPShellMode.
//
// What it does. While the server draws the units (the mode word's VPMODE_UNITS, latched once per frame at the Present of
// the target swap chain, used only while the server is alive), every draw whose bound pixel shader is one of the EXE's
// unit pass-0 programs (kVPUnitDrop) and whose bound vertex shader is that program's pair is not passed to d3d11, on the
// immediate context and on every deferred context alike. Every other draw is passed unchanged. Ticket 4785: while the latched
// word also has VPMODE_UNIT_SHADOWS, the unit shadow-mask pairs (kVPUnitMaskPairs) are dropped the same way (the server
// casts its units' shadows into the tapped map); the shadow tap flags the slots copied under that latch.
//
// Recognition (which ID3D11VertexShader / ID3D11PixelShader objects are the table's programs):
//   path 1 - the device's CreateVertexShader / CreatePixelShader hooks hash the bytecode of every creation (SHA-256,
//            once per creation) and mark the object when it equals a table digest; a creation that does not match
//            unmarks the object's address (a reused address is never taken for a unit program).
//   path 2 - the EXE creates all its shaders at app start, before the composite is loaded (GATE2 2, decision log Gate 2),
//            so path 1 never sees them in the game. At the first Present after the composite's EXE validation, each
//            program's object is read from the EXE's renderer: the 16-bit handle from its handle global, the object from
//            the per-stage object array; marked only when the instruction patterns at its create site, the shader level
//            (10 = the 5_0 set), the SHA-256 of the blob at the program's VA, the handle's range, the vertex shader's
//            recorded bytecode pointer and size, and the object itself (a d3d11 object of that stage on the EXE's
//            device) all agree. Any mismatch: refused LOUD, nothing marked, the filter off for the process (draws pass).
//            Read only: no byte of the EXE is ever written (D4).
// Tracking: the VSSetShader / PSSetShader hooks keep the bound program per context (a small table keyed by the context
// pointer, a thread-local cache of the last context); a draw reads it without a COM call. Before a drop the bound shaders
// are read back (VSGetShader / PSGetShader) and compared, so a stale track can only ever pass a draw, never drop one.
// Failures: an exception inside any detour body disables the filter alone (draws pass), logged once; the composite goes on.
// D118 (issue 4761): at every Present the filter writes VPShellMode::filterState (can drop, or cannot drop with the
// reason: path 2 refused or not run, an exception, no DROP program, unit_filter=off, no hooked draw, ...), before
// latchedWord. The composite has no route to D118's popup; the server's mode writer reads the word and, while it holds
// VPMODE_UNITS and the composite latched it, takes the D125 fatal step (units would be drawn twice).
//
// Keys (shell_composite.ini [composite], read by shell_composite.cpp): unit_filter = auto | on | off (auto = the mode
// word; on and off are TEST ONLY and logged), hook_units (the kill switch of every hook of this file). Test only, read
// here (VPSHELL_<KEY> or the ini): unit_filter_fault = draw | set | create | present (one access violation in that
// detour), unit_filter_table = <file> (lines "<index> <64 hex digits>": the digests of programs 0..11 replaced, for the
// stand-in host whose shaders are not the EXE's; logged LOUD).
//
// The model filter (epic 0561, ticket 4874; city-renderer.md 10): the same recogniser and hooks also know the EXE's
// BuildingShader programs (shell/common/shell_model_scene.h; path 2 checks their create sites as one group of its own:
// a refusal turns the model filter off, never the units'). While the latch holds VPMODE_CITIES / VPMODE_LANDMARKS, a
// colour-pass draw of a DROP pair into a target with a depth view is dropped when its mesh key is in the server's
// identity set for that bit and its tap record is complete; every other family draw passes and is counted. The tap
// (shell/common/shell_model_tap.h) writes the dropped draws (and those the server wants, or all with model_tap_passed)
// per EXE frame. Its own hooks (hook_models): CreateBuffer / CreateTexture2D (the content hashes of the key, at creation
// with initial data), Map / Unmap (the Unmap copy of the buffers a family draw reads: slot 1, VS cb0/cb1, PS cb1, VS t27).
// Keys (shell_composite.ini): model_filter = auto | on | off (on and off TEST ONLY), hook_models, model_tap_passed (TEST
// ONLY), model_tap_slot_kb. Log lines "model_filter: ...", stats "modelfilter 1s:" / "modelfilter totals:".
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "shell_shared.h"
#include "shell_model_tap.h"
#include "vp_frame.h"

enum { UF_MODE_AUTO = 0, UF_MODE_ON = 1, UF_MODE_OFF = 2 };
extern const char* const kUnitFilterModeNames[3];   // "auto", "on", "off"

// the draw entries of ID3D11DeviceContext (S6: the game uses DrawIndexedInstanced only; the others are hooked and counted)
enum { UF_E_DII, UF_E_DI, UF_E_D, UF_E_DINST, UF_E_DIIIND, UF_E_DINSTIND, UF_E_DAUTO, UF_E_COUNT };

typedef HRESULT(STDMETHODCALLTYPE* PFN_UfCreateVS)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_UfCreatePS)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
typedef void(STDMETHODCALLTYPE* PFN_UfVSSet)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfPSSet)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfDrawII)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfDrawI)(ID3D11DeviceContext*, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE* PFN_UfDraw)(ID3D11DeviceContext*, UINT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfDrawInst)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfDrawIndirect)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfDrawAuto)(ID3D11DeviceContext*);

// ---- detour bodies (tr = the trampoline of the variant that was called) ----
HRESULT UnitFilter_CreateVS(PFN_UfCreateVS tr, ID3D11Device* d, const void* bc, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** out);
HRESULT UnitFilter_CreatePS(PFN_UfCreatePS tr, ID3D11Device* d, const void* bc, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** out);
void UnitFilter_VSSet(PFN_UfVSSet tr, ID3D11DeviceContext* c, ID3D11VertexShader* s, ID3D11ClassInstance* const* ci, UINT n);
void UnitFilter_PSSet(PFN_UfPSSet tr, ID3D11DeviceContext* c, ID3D11PixelShader* s, ID3D11ClassInstance* const* ci, UINT n);
void UnitFilter_DrawII(PFN_UfDrawII tr, ID3D11DeviceContext* c, UINT a, UINT b, UINT s, INT bv, UINT si);
void UnitFilter_DrawI(PFN_UfDrawI tr, ID3D11DeviceContext* c, UINT a, UINT s, INT bv);
void UnitFilter_Draw(PFN_UfDraw tr, ID3D11DeviceContext* c, UINT a, UINT s);
void UnitFilter_DrawInst(PFN_UfDrawInst tr, ID3D11DeviceContext* c, UINT a, UINT b, UINT s, UINT si);
void UnitFilter_DrawIIIndirect(PFN_UfDrawIndirect tr, ID3D11DeviceContext* c, ID3D11Buffer* buf, UINT off);
void UnitFilter_DrawInstIndirect(PFN_UfDrawIndirect tr, ID3D11DeviceContext* c, ID3D11Buffer* buf, UINT off);
void UnitFilter_DrawAuto(PFN_UfDrawAuto tr, ID3D11DeviceContext* c);

// The context's pipeline state was reset to the defaults by the runtime (FinishCommandList or ExecuteCommandList with
// RestoreState FALSE) or the context is new (a deferred context at a possibly reused address): its tracked shaders are cleared.
void UnitFilter_OnStateReset(ID3D11DeviceContext* c);

struct UnitFilterEnv
{
	ID3D11Device* dev;            // the target's device (the EXE's)
	uint32_t exeBase;             // module base of the EXE (or a test host's fake image)
	int32_t exeValidated;         // the composite's EXE validation: 1 valid, -1 refused, 0 not yet
	VPShellHeader* hdr;           // the main section (null while not mapped)
	int staleMs;                  // stale_ms: the heartbeat age above which the server is not alive
	int64_t qpcFreq;
	int64_t presentCount;         // the camera block's presentCount of this Present
	int compositeOn;              // mode=composite and the composite not disabled: the server's picture is shown
	int mode;                     // UF_MODE_* (unit_filter)
	void (*log)(const char* fmt, ...);
	// issue 4761 (4757 #4248): where the server's liveness is read. handover=ring (the product since 0490): the ring sink
	// keeps the main header's magic 0 and writes magic, serverPid, state and heartbeatQpc only into the <base>.Frame
	// section's server block, so the latch reads that block (null while the composite has not mapped it). handover=v3:
	// the main header (VPShellHeader magic, serverPid, heartbeatQpc) as before.
	int ring;                     // 1 = handover=ring
	const VPFrameServer* frameSrv; // VPFRAME_SERVER of the composite's <base>.Frame mapping (ring), else null
	// ticket 4874: the model filter and tap
	int modelMode;                // UF_MODE_* (model_filter)
	int modelTapPassed;           // model_tap_passed (TEST ONLY)
	int modelSlotKb;              // model_tap_slot_kb (checked by the composite's config)
	int hookModels;               // hook_models on (and mode not off): the creation and Map/Unmap hooks are enabled
	int hookUnits;                // hook_units on: the draw and bind hooks are enabled
};

// ---- the model tap (ticket 4874): resource identity at creation, the Unmap copy, the command lists' records ----
// Ticket 0719, optional key model_map_cache (not in shell_composite.ini; [composite] or VPSHELL_MODEL_MAP_CACHE): 1 (the
// default) = a WRITE_DISCARD Map of a DYNAMIC non-constant buffer the copy wants hands the EXE a staging of ordinary memory,
// and its Unmap forwards the written pages to the driver's memory before the copy reads the staging (unit_filter.cpp,
// g_mapCache); 0 = every Map hands the EXE the driver's memory and the copy reads it there (the path before 0719);
// verify = 1 plus a read-back comparison of every forwarded page (TEST ONLY). Counters: the "modelfilter" lines' map_*,
// fwd_*, unwritten_kb, stage_*, ww_fail, verify_*, late_pages.
typedef HRESULT(STDMETHODCALLTYPE* PFN_UfCreateBuffer)(ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_UfCreateTex2D)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_UfMap)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
typedef void(STDMETHODCALLTYPE* PFN_UfUnmap)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
HRESULT UnitFilter_CreateBuffer(PFN_UfCreateBuffer tr, ID3D11Device* d, const D3D11_BUFFER_DESC* bd, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Buffer** out);
HRESULT UnitFilter_CreateTexture2D(PFN_UfCreateTex2D tr, ID3D11Device* d, const D3D11_TEXTURE2D_DESC* td, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Texture2D** out);
HRESULT UnitFilter_Map(PFN_UfMap tr, ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m);
void UnitFilter_Unmap(PFN_UfUnmap tr, ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub);
// a write into a resource that has an identity clears it (lead2-0561 #6273): UpdateSubresource, CopyResource,
// CopySubresourceRegion (their destination)
typedef void(STDMETHODCALLTYPE* PFN_UfUpdateSub)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_UfCopyRes)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
typedef void(STDMETHODCALLTYPE* PFN_UfCopySubRegion)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*);
void UnitFilter_UpdateSubresource(PFN_UfUpdateSub tr, ID3D11DeviceContext* c, ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* p, UINT rp, UINT dp);
void UnitFilter_CopyResource(PFN_UfCopyRes tr, ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src);
void UnitFilter_CopySubresourceRegion(PFN_UfCopySubRegion tr, ID3D11DeviceContext* c, ID3D11Resource* dst, UINT dsub, UINT x, UINT y, UINT z, ID3D11Resource* src, UINT ssub, const D3D11_BOX* box);
// A deferred context finished a command list: its pending model records go with the list. The immediate context executes
// a list (called before the list runs): the list's records take their place in this frame's tap, in execution order.
void UnitFilter_OnFinishList(ID3D11DeviceContext* c, ID3D11CommandList* cl);
void UnitFilter_OnExecuteList(ID3D11DeviceContext* c, ID3D11CommandList* cl);

// Render thread, every Present of the target (after the camera block was published): path 2 once, the liveness check, the
// latch of the mode word for the draws until the next Present, the consumer fields of VPShellMode.
void UnitFilter_OnPresent(const UnitFilterEnv& env);
// Control thread, every 500 ms: a latch older than 1 s (no Present of the target meanwhile: the Present hook switched off,
// the target gone) is cleared, so drops never outlive the Present that checked the server. Also hands the log function
// over early (a creation before the first Present logs too).
void UnitFilter_ControlTick(void (*log)(const char* fmt, ...));
// Render thread, from the composite's stats tick: "unitfilter 1s:" (this interval) and "unitfilter totals:" lines.
void UnitFilter_StatsLine(void (*log)(const char* fmt, ...), double elapsedMs);
// Ticket 4785 (unit-combat-server.md 13): 1 while the latch drops the EXE's unit shadow-mask pairs (kVPUnitMaskPairs: the
// latched word has VPMODE_UNITS and VPMODE_UNIT_SHADOWS, the filter on and not off for the process), else 0. Read by the
// shadow tap at its copy (ShadowTap_OnPresent runs before UnitFilter_OnPresent in the composite's Present, so this is the
// latch the copied frame's draws were made under) for the slot flag VPSHD_SLOT_UNIT_MASK_DROPPED.
int UnitFilter_UnitMaskDropLatched(void);
