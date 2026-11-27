// unit_filter.cpp - the compositor's unit-draw filter (epic 0040, issue 4752). Written 2026-11-17 for this repository
// (GPL-3.0-or-later). What it does and why: unit_filter.h; the EXE facts and the measurements:
// research/05-graphics-composition/unit-draw-filter.md section 3. Every EXE address here is a symbol of the EXE build
// table (epic 0590, D202; ticket 4965; shell_exe.h, exebuild/symbols/shell.tsv), resolved for the running build (read from
// the image's header) and rebased on the module base at run time; nothing of the EXE is written, and no EXE byte is in
// this file except short instruction patterns that are compared (D83, as common/exe_camera.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "unit_filter.h"
#include "shell_exe.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "unit_programs.h"
#include "shell_model_scene.h"
#include "tail_read.h"   // ticket 0758: the tail's streaming-load read

#pragma comment(lib, "bcrypt.lib")

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
// as shell_composite.cpp AsanAfterSeh: an __except unwinds instrumented frames without their epilogues
static __declspec(noinline) void UfAsanAfterSeh()
{
	volatile char probe = 0;
	char* p = (char*)&probe;
	__asan_unpoison_memory_region(p - 65536, 65536);
}
#else
static inline void UfAsanAfterSeh() {}
#endif

const char* const kUnitFilterModeNames[3] = { "auto", "on", "off" };

// ===========================================================================
// The programs: 0..4 = kVPUnitDrop, 5..11 = kVPUnitKeep (unit_programs.h order); 12..21 = kVPModelPrograms (ticket 4874,
// shell_model_scene.h order). Path 2 resolves the units' twelve and the models' ten as two groups, all or nothing each.
// ===========================================================================
enum { UF_UNIT_PROGS = VPUNIT_DROP_PROGRAMS + VPUNIT_KEEP_PROGRAMS, UF_MODEL0 = UF_UNIT_PROGS, UF_PROGS = UF_UNIT_PROGS + VPMODEL_PROGRAMS, UF_NONE = 0xFF };
static_assert(UF_UNIT_PROGS == 12 && UF_PROGS == 22, "the path-2 table below names 12 + 10 programs");
static_assert(VPMODEL_STAGE_VS == VPUNIT_STAGE_VS && VPMODEL_STAGE_PS == VPUNIT_STAGE_PS, "one stage encoding");

static uint8_t g_dig[UF_PROGS][32];        // digests (the table's, or the test table's)
static uint8_t g_stage[UF_PROGS];          // VPUNIT_STAGE_*
static uint8_t g_isDropPs[UF_PROGS];       // 1 = a DROP pixel shader (units)
static uint8_t g_pairVs[UF_PROGS];         // for a DROP PS: the program index of its pair's VS (units)
static uint8_t g_isMaskPs[UF_PROGS];       // ticket 4785: 1 = a shadow-mask pixel shader (kVPUnitMaskPairs)
static uint8_t g_maskPairVs[UF_PROGS];     // ... and the program index of its pair's VS
static char g_short[UF_PROGS][12];         // "29a0b061" (the table's digest, also with a test table)

static inline bool IsModelProg(int i) { return i >= UF_MODEL0 && i < UF_PROGS; }
static const uint8_t* ProgSha(int i) { return i < VPUNIT_DROP_PROGRAMS ? kVPUnitDrop[i].sha256 : i < UF_UNIT_PROGS ? kVPUnitKeep[i - VPUNIT_DROP_PROGRAMS].sha256 : kVPModelPrograms[i - UF_MODEL0].sha256; }
static uint32_t ProgVa(int i) { return i < VPUNIT_DROP_PROGRAMS ? kVPUnitDrop[i].exeVa : i < UF_UNIT_PROGS ? kVPUnitKeep[i - VPUNIT_DROP_PROGRAMS].exeVa : kVPModelPrograms[i - UF_MODEL0].exeVa; }
static uint8_t ProgStage(int i) { return i < VPUNIT_DROP_PROGRAMS ? kVPUnitDrop[i].stage : i < UF_UNIT_PROGS ? kVPUnitKeep[i - VPUNIT_DROP_PROGRAMS].stage : kVPModelPrograms[i - UF_MODEL0].stage; }

// the model class of a bound (VS, PS) pair of program indices (12..21), or VPMODEL_CLASS_NONE; its kVPModelDropPairs row
static uint8_t g_mPairCls[VPMODEL_PROGRAMS][VPMODEL_PROGRAMS];
static uint8_t g_mPairBit[VPMODEL_CLASSES];

// ---- path 2: where the EXE keeps each program (unit-draw-filter.md 3.2: each program has exactly one create site, in the
// 5_0 set of the shader init; found 2026-11-17 in the llvm-objdump listing of build 4390913's EXE and by hashing every
// create site's blob of render/src/shaders/create_sites_table.inc). Epic 0590 (ticket 4965): every address is a symbol of
// the build table (shell_exe.h): the blob (.shader), the push of the blob at its create site, the 16-bit handle global the
// site stores; the create call is at push + UF_CALL_OFF and the handle store at push + storeOff in every build (one
// function body; the site patterns below check both on every build gen_table.py generates) ----
struct UfSite
{
	int blob;             // VPX_*_blob: the program's DXBC container (the operand of the push)
	int push;             // VPX_*_push: 68 <blob>
	int handle;           // VPX_*_handle: the handle global (66 A3 <handle> stores the create function's result)
	uint32_t storeOff;    // the handle store's offset from the push
};
#define UF_CALL_OFF 0x0Bu     // E8 rel32 -> uf_create_vs (VS) / uf_create_ps (PS), from the push
static const UfSite kSites[UF_PROGS] = {
	{ VPX_uf_vs_29a0b061_blob, VPX_uf_vs_29a0b061_push, VPX_uf_vs_29a0b061_handle, 0x25u },   // VS 29a0b061 (89, 90)
	{ VPX_uf_vs_82e59838_blob, VPX_uf_vs_82e59838_push, VPX_uf_vs_82e59838_handle, 0x15u },   // VS 82e59838 (87)
	{ VPX_uf_ps_60d270e1_blob, VPX_uf_ps_60d270e1_push, VPX_uf_ps_60d270e1_handle, 0x20u },   // PS 60d270e1 (90)
	{ VPX_uf_ps_5def4f99_blob, VPX_uf_ps_5def4f99_push, VPX_uf_ps_5def4f99_handle, 0x20u },   // PS 5def4f99 (89)
	{ VPX_uf_ps_0e7a3566_blob, VPX_uf_ps_0e7a3566_push, VPX_uf_ps_0e7a3566_handle, 0x20u },   // PS 0e7a3566 (87)
	{ VPX_uf_vs_8cb52b96_blob, VPX_uf_vs_8cb52b96_push, VPX_uf_vs_8cb52b96_handle, 0x20u },   // VS 8cb52b96 mask (89, 90)
	{ VPX_uf_ps_72c27aea_blob, VPX_uf_ps_72c27aea_push, VPX_uf_ps_72c27aea_handle, 0x21u },   // PS 72c27aea mask (90)
	{ VPX_uf_ps_246527ed_blob, VPX_uf_ps_246527ed_push, VPX_uf_ps_246527ed_handle, 0x20u },   // PS 246527ed mask (89)
	{ VPX_uf_vs_59af33ff_blob, VPX_uf_vs_59af33ff_push, VPX_uf_vs_59af33ff_handle, 0x25u },   // VS 59af33ff mask (87)
	{ VPX_uf_ps_9a5991d7_blob, VPX_uf_ps_9a5991d7_push, VPX_uf_ps_9a5991d7_handle, 0x20u },   // PS 9a5991d7 mask (87)
	{ VPX_uf_vs_da2382af_blob, VPX_uf_vs_da2382af_push, VPX_uf_vs_da2382af_handle, 0x25u },   // VS da2382af selection
	{ VPX_uf_ps_8d853988_blob, VPX_uf_ps_8d853988_push, VPX_uf_ps_8d853988_handle, 0x20u },   // PS 8d853988 selection
	// ticket 4874 (P5 offline, city-renderer.md 10.1; render/evidence/0561/4874/p5_sites.txt): the BuildingShader programs,
	// each with exactly one create site among the 1,417 of create_sites_table.inc, all in the 5_0 set of the shader init
	{ VPX_uf_vs_7ec4e9a5_blob, VPX_uf_vs_7ec4e9a5_push, VPX_uf_vs_7ec4e9a5_handle, 0x22u },   // VS 7ec4e9a5 (4, 5)
	{ VPX_uf_ps_c704c80b_blob, VPX_uf_ps_c704c80b_push, VPX_uf_ps_c704c80b_handle, 0x20u },   // PS c704c80b (5)
	{ VPX_uf_ps_f0e1f0a6_blob, VPX_uf_ps_f0e1f0a6_push, VPX_uf_ps_f0e1f0a6_handle, 0x20u },   // PS f0e1f0a6 (3, 4, 6)
	{ VPX_uf_vs_c4bb25da_blob, VPX_uf_vs_c4bb25da_push, VPX_uf_vs_c4bb25da_handle, 0x25u },   // VS c4bb25da (1, 3)
	{ VPX_uf_vs_7a9e17f6_blob, VPX_uf_vs_7a9e17f6_push, VPX_uf_vs_7a9e17f6_handle, 0x25u },   // VS 7a9e17f6 (6)
	{ VPX_uf_vs_88c2b7d4_blob, VPX_uf_vs_88c2b7d4_push, VPX_uf_vs_88c2b7d4_handle, 0x25u },   // VS 88c2b7d4 mask (4, 5)
	{ VPX_uf_ps_73daa800_blob, VPX_uf_ps_73daa800_push, VPX_uf_ps_73daa800_handle, 0x22u },   // PS 73daa800 mask (5)
	{ VPX_uf_ps_e12e9eaf_blob, VPX_uf_ps_e12e9eaf_push, VPX_uf_ps_e12e9eaf_handle, 0x20u },   // PS e12e9eaf mask (3, 4, 6)
	{ VPX_uf_vs_0a3f2230_blob, VPX_uf_vs_0a3f2230_push, VPX_uf_vs_0a3f2230_handle, 0x22u },   // VS 0a3f2230 mask (3)
	{ VPX_uf_vs_34b98e63_blob, VPX_uf_vs_34b98e63_push, VPX_uf_vs_34b98e63_handle, 0x20u },   // VS 34b98e63 mask (6)
};
// uf_create_vs: D3D11Renderer::CreateVertexShader (blob, size, mask), handle = renderer+0x458; uf_create_ps:
// D3D11Renderer::CreatePixelShader (blob, size), handle = renderer+0x45c; pxw_renderer: the D3D11Renderer* (ecx of every
// create call); pxw_device: the ID3D11Device* the create functions call; uf_level: the shader level (<= 8 set 4_0, 9 set
// 4_1, 10 set 5_0; asset-pipeline.md 2.2); uf_ps_obj / uf_vs_obj: ID3D11PixelShader* / ID3D11VertexShader* [handle];
// uf_vs_bc: const void* bytecode [handle] (VS only); uf_vs_size: SIZE_T bytecode size [handle] (VS only)
#define UF_RENDERER_VS_COUNT 0x458u
#define UF_RENDERER_PS_COUNT 0x45cu
#define UF_MAX_HANDLES 1024u          // each object array is 0x1000 bytes (uf_ps_obj .. uf_vs_obj)
#define UF_LEVEL_5_0 10u

// Instruction patterns read (compared, never written) before any data is read (R(name) = 4 bytes holding the symbol's
// address in the running build; gen_table.py checks every row on both builds, exebuild/patterns.txt).
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kUfPats[] = {
	X(VPX_uf_create_ps, "CreatePixelShader: mov eax, [device]", "A1 R(pxw_device)")
	X(VPX_uf_ps_count, "CreatePixelShader: mov ecx, [esi+0x45c]", "8B 8E 5C 04 00 00")
	X(VPX_uf_ps_store, "CreatePixelShader: mov [4*ecx+PS objects], edx", "89 14 8D R(uf_ps_obj)")
	X(VPX_uf_create_vs, "CreateVertexShader: mov eax, [device]", "A1 R(pxw_device)")
	X(VPX_uf_vs_count, "CreateVertexShader: mov ecx, [esi+0x458]", "8B 8E 58 04 00 00")
	X(VPX_uf_vs_store, "CreateVertexShader: mov [4*ecx+VS objects], edx", "89 14 8D R(uf_vs_obj)")
	X(VPX_uf_vs_bc_store, "CreateVertexShader: mov [4*edx+VS bytecode], ebx", "89 1C 95 R(uf_vs_bc)")
	X(VPX_uf_vs_size_store, "CreateVertexShader: mov [4*eax+VS size], edi", "89 3C 85 R(uf_vs_size)")
	X(VPX_uf_shader_init, "shader init: mov ecx, [renderer]", "8B 0D R(pxw_renderer)")
	X(VPX_uf_shader_level_read, "shader init: mov eax, [shader level]", "A1 R(uf_level)")
	// the create sites (push of the blob, the create call, the handle store)
	X(VPX_uf_vs_29a0b061_push, "VS 29a0b061: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_29a0b061_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_29a0b061_handle)")
	X(VPX_uf_vs_82e59838_push, "VS 82e59838: push blob, create call at +0x0B, handle store at +0x15", "68 R(uf_vs_82e59838_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_82e59838_handle)")
	X(VPX_uf_ps_60d270e1_push, "PS 60d270e1: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_60d270e1_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_60d270e1_handle)")
	X(VPX_uf_ps_5def4f99_push, "PS 5def4f99: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_5def4f99_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_5def4f99_handle)")
	X(VPX_uf_ps_0e7a3566_push, "PS 0e7a3566: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_0e7a3566_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_0e7a3566_handle)")
	X(VPX_uf_vs_8cb52b96_push, "VS 8cb52b96: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_vs_8cb52b96_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_8cb52b96_handle)")
	X(VPX_uf_ps_72c27aea_push, "PS 72c27aea: push blob, create call at +0x0B, handle store at +0x21", "68 R(uf_ps_72c27aea_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_72c27aea_handle)")
	X(VPX_uf_ps_246527ed_push, "PS 246527ed: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_246527ed_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_246527ed_handle)")
	X(VPX_uf_vs_59af33ff_push, "VS 59af33ff: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_59af33ff_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_59af33ff_handle)")
	X(VPX_uf_ps_9a5991d7_push, "PS 9a5991d7: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_9a5991d7_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_9a5991d7_handle)")
	X(VPX_uf_vs_da2382af_push, "VS da2382af: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_da2382af_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_da2382af_handle)")
	X(VPX_uf_ps_8d853988_push, "PS 8d853988: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_8d853988_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_8d853988_handle)")
	X(VPX_uf_vs_7ec4e9a5_push, "VS 7ec4e9a5: push blob, create call at +0x0B, handle store at +0x22", "68 R(uf_vs_7ec4e9a5_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_7ec4e9a5_handle)")
	X(VPX_uf_ps_c704c80b_push, "PS c704c80b: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_c704c80b_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_c704c80b_handle)")
	X(VPX_uf_ps_f0e1f0a6_push, "PS f0e1f0a6: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_f0e1f0a6_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_f0e1f0a6_handle)")
	X(VPX_uf_vs_c4bb25da_push, "VS c4bb25da: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_c4bb25da_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_c4bb25da_handle)")
	X(VPX_uf_vs_7a9e17f6_push, "VS 7a9e17f6: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_7a9e17f6_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_7a9e17f6_handle)")
	X(VPX_uf_vs_88c2b7d4_push, "VS 88c2b7d4: push blob, create call at +0x0B, handle store at +0x25", "68 R(uf_vs_88c2b7d4_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_88c2b7d4_handle)")
	X(VPX_uf_ps_73daa800_push, "PS 73daa800: push blob, create call at +0x0B, handle store at +0x22", "68 R(uf_ps_73daa800_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_73daa800_handle)")
	X(VPX_uf_ps_e12e9eaf_push, "PS e12e9eaf: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_ps_e12e9eaf_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_ps_e12e9eaf_handle)")
	X(VPX_uf_vs_0a3f2230_push, "VS 0a3f2230: push blob, create call at +0x0B, handle store at +0x22", "68 R(uf_vs_0a3f2230_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_0a3f2230_handle)")
	X(VPX_uf_vs_34b98e63_push, "VS 34b98e63: push blob, create call at +0x0B, handle store at +0x20", "68 R(uf_vs_34b98e63_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 A3 R(uf_vs_34b98e63_handle)")
};
#undef X
#define UF_NPATS ((int)(sizeof(kUfPats) / sizeof(kUfPats[0])))
// the symbols path 2 reads besides the pattern sites
static const int kUfNeed[] = { VPX_pxw_renderer, VPX_pxw_device, VPX_uf_level, VPX_uf_ps_obj, VPX_uf_vs_obj, VPX_uf_vs_bc,
	VPX_uf_vs_size, VPX_uf_create_vs, VPX_uf_create_ps };

// ===========================================================================
// State
// ===========================================================================
static void (*volatile g_log)(const char* fmt, ...);
#define ULOG(...) do { void (*l_)(const char*, ...) = g_log; if (l_) l_(__VA_ARGS__); } while (0)

static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
static volatile LONG g_off;                 // 1 = the filter is off for the process (an exception, or path 2 refused)
static char g_offWhy[256];
static volatile LONG g_offReason;           // issue 4761: VPFILTER_R_* of the first turn-off (the mode block's filterState)
static volatile LONG g_dropNow;             // this frame: drop the table's pairs (latched at the last Present)
static volatile LONG g_setNow;              // this frame: the latched word has VPMODE_UNITS
static volatile LONG g_maskDropNow;         // ticket 4785: this frame: drop the mask pairs (g_dropNow and VPMODE_UNIT_SHADOWS latched)
static volatile LONG g_maskSetNow;          // ticket 4785: this frame: the latched word has VPMODE_UNITS and VPMODE_UNIT_SHADOWS
static volatile LONGLONG g_latchQpc;       // QPC of the last latch (UnitFilter_ControlTick)
static int64_t g_freq;
enum { UFF_NONE, UFF_DRAW, UFF_SET, UFF_CREATE, UFF_PRESENT };
static int g_fault;                         // unit_filter_fault (test only)
static volatile LONG g_faultFired;
static bool g_testTable;
static wchar_t g_testTablePath[MAX_PATH];
static char g_testTableNote[200];

// marked objects: open addressing, never shrinks; prog UF_NONE = an address seen at a creation that matched nothing
struct UfMark { void* volatile obj; volatile LONG prog; };
static const int kMarks = 512;
static UfMark g_marks[kMarks];
static SRWLOCK g_markLock = SRWLOCK_INIT;

static inline unsigned MarkHash(const void* p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) >> 23; }   // 9 bits

static int MarkLookup(const void* obj)
{
	if (!obj) return UF_NONE;
	for (unsigned i = MarkHash(obj), k = 0; k < (unsigned)kMarks; ++k, i = (i + 1) & (kMarks - 1))
	{
		void* o = g_marks[i].obj;
		if (o == obj) return (int)g_marks[i].prog;
		if (!o) return UF_NONE;
	}
	return UF_NONE;
}

// returns false when the table is full (never in practice: 12 programs, the EXE's few re-creations)
static bool MarkSet(void* obj, int prog)
{
	AcquireSRWLockExclusive(&g_markLock);
	bool ok = false;
	for (unsigned i = MarkHash(obj), k = 0; k < (unsigned)kMarks; ++k, i = (i + 1) & (kMarks - 1))
	{
		void* o = g_marks[i].obj;
		if (o == obj || !o)
		{
			g_marks[i].prog = prog;
			MemoryBarrier();
			g_marks[i].obj = obj;
			ok = true;
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_markLock);
	return ok;
}

// ticket 4874: the model filter's per-context counters (the "modelfilter" lines; summed as uint32 arrays)
enum { MC_SEEN0, MC_SEEN1, MC_SEEN2, MC_SEEN3,        // family colour draws seen per class (a DROP pair bound)
	MC_DROP_C, MC_DROP_L, MC_PSET_C, MC_PSET_L,           // dropped / passed while latched, per bit (cities, landmarks)
	MC_TAPPED, MC_NOTINSET, MC_NODSV, MC_UNHASHED, MC_NOSHADOW, MC_BUDGET, MC_NOMEM, MC_MISMATCH, MC_ENTRY, MC_BOUND,
	MC_IDLE, MC_KEEP,
	// why a key is incomplete (live load 1 of 2026-11-18: every tapped draw was "unhashed"): the part not in the identity
	// table (created before the hooks, or never seen: then hashed at first sight, see the read-back below) or in it
	// without the needed hash (a creation without initial data, a format without a row size)
	MC_UH_IB_MISS, MC_UH_IB_OTHER, MC_UH_VB_MISS, MC_UH_VB_OTHER, MC_UH_TEX_MISS, MC_UH_TEX_NOID,
	// ticket 4879: CPU-built meshes seen (R32 on the block / material-3 programs while cities are wanted), and those in the
	// set and latched but passed because a buffer is not held in the geometry section
	MC_CPU_SEEN, MC_CPU_NOGEOM,
	// ticket 0788: droppable draws held (passed, recorded HELD) the first frame their key became droppable
	MC_HELD, MC_COUNT };
static const char* const kMcNames[MC_COUNT] = { "seen_block", "seen_single", "seen_inst", "seen_skinned", "dropped_cities",
	"dropped_landmarks", "passed_set_cities", "passed_set_landmarks", "tapped", "not_in_set", "no_dsv", "unhashed", "no_shadow",
	"budget", "nomem", "mismatch", "entry", "bound", "idle", "keep", "uh_ib_miss", "uh_ib_other", "uh_vb_miss", "uh_vb_other",
	"uh_tex_miss", "uh_tex_noid", "cpu_seen", "cpu_nogeom", "held" };

// ticket 0719: real = the driver's pointer of a cached map (data is then the staging), size = the buffer's ByteWidth, sh
// = the shadow that holds the staging; real == nullptr for the direct path
struct Shadow;
struct UfOpenMap { void* res; void* data; uint32_t bytes; void* real; uint32_t size; Shadow* sh; uint32_t need; };
struct MList;

// per-context tracking and counters (one writer per context: the thread that records on it)
struct UfCtx
{
	void* volatile ctx;
	volatile uint8_t vs, ps, any, pad;
	volatile uint32_t entry[UF_E_COUNT];      // every draw, per entry
	volatile uint32_t seen[UF_PROGS], dropped[UF_PROGS], passed[UF_PROGS];
	volatile uint32_t passedSet;               // DROP-PS draws passed while the latched word had VPMODE_UNITS
	volatile uint32_t mismatch;                // a drop refused: the bound shaders read back differ from the track
	volatile uint32_t passedSetMask;           // ticket 4785: mask-PS draws passed while the latch had both unit bits
	// ---- ticket 4874 ----
	volatile uint32_t mc[MC_COUNT];
	int deferred;                              // -1 not asked yet, 0 the immediate context, 1 deferred
	uint32_t epoch;                            // incremented at each FinishCommandList (own shadows older than it are stale)
	UfOpenMap om[4];                           // open maps of buffers the tap copies at Unmap
	MList* list;                               // deferred: the records of the list being recorded (null until the first)
	void* lastRtv; void* lastDsv; uint32_t lastTarget[6]; int lastTargetOk;   // the target cache (RTV/DSV desc)
};
static const int kCtx = 64;
static UfCtx g_ctxs[kCtx];
static volatile LONG g_nCtx;
static SRWLOCK g_ctxLock = SRWLOCK_INIT;     // static: a bind or a draw may come before anything initialised the filter
static volatile LONG g_untracked;           // draws on a context beyond kCtx (passed)
static __declspec(thread) void* t_ctx;
static __declspec(thread) UfCtx* t_slot;

// global counters (interlocked; creations and path 2 are rare)
static volatile LONG g_created[UF_PROGS], g_marked[UF_PROGS], g_resolved[UF_PROGS], g_refused[UF_PROGS];
static volatile LONG g_createsHashed, g_markFull;
static const char* g_path2 = "pending";     // pending | not-run | resolved | refused
static bool g_path2Done;

// latch and liveness (render thread)
// UFL_SRVSTATE (issue 4761): handover=ring, the frame section's server block says FAILED or STOPPED
enum { UFL_LIVE, UFL_NOSECTION, UFL_NOTINIT, UFL_PID, UFL_STALE, UFL_NOTCOMPOSITE, UFL_SRVSTATE, UFL_COUNT };
static const char* kLiveNames[UFL_COUNT] = { "live", "nosection", "notinit", "pid", "stale", "notcomposite", "srvstate" };
static uint32_t g_ignored[UFL_COUNT];       // Presents whose raw word had VPMODE_UNITS but the server was not live, by reason
static uint32_t g_framesSet, g_framesDrop, g_presents;
static uint32_t g_framesMaskDrop;            // ticket 4785: Presents whose latch drops the mask pairs
static uint32_t g_lastLatched = 0xFFFFFFFFu;
static int g_lastReason = -1, g_lastMode = -1;
static uint32_t g_clearedByTick;
static uint32_t g_framesUnable;             // Presents whose latch asked for drops while the filter could not drop (LOUD once)
static bool g_unableLogged;
static char g_unableWhy[400];               // why path 2 marked nothing (for that LOUD line)
static uint32_t g_filterState = 0xFFFFFFFFu;   // issue 4761: the last filterState (VPFILTER_WORD); none before the first Present
static const uint32_t kHookGracePresents = 2;   // issue 4761: Presents before "no draw reached the hooks" counts (hooks going in)

// ---- ticket 4874: the model filter's state ----
// model_filter_fault = draw | map | create | present | execute (TEST ONLY): one access violation in that model path
enum { MFF_NONE, MFF_DRAW, MFF_MAP, MFF_CREATE, MFF_PRESENT, MFF_EXECUTE };
static int g_mfault;
static volatile LONG g_mfaultFired;
static bool g_mSetAll;                      // model_filter_set=all (TEST ONLY)
// ticket 4879, model_geom_test (TEST ONLY): "full" = the geometry section never has room; "oversize:<bytes>" = a buffer
// over <bytes> is refused as oversize (the product's bound is VPSHELL_MODELGEOM_MAX_BUFFER); "changed" = every copy reads
// as differing from its creation hash (the refusal of a write after the hash); ticket 0597: "bytes:<n>" = the section's
// data is <n> bytes (below model_geom_kb's minimum: the full section with the stand-in's small buffers)
static bool g_geomTestFull, g_geomTestChanged;
static uint32_t g_geomTestMaxBytes, g_geomTestBytes;
// ticket 0597: the geometry section's data size, from model_geom_kb (KB, VPSHELL_MODELGEOM_KB_MIN..MAX, default
// VPSHELL_MODELGEOM_KB_DEFAULT; a value out of range is clamped, one that is not a number is the default, both noted in
// the section's line), or the test's bytes:<n>
static uint32_t g_geomDataBytes = VPSHELL_MODELGEOM_KB_DEFAULT * 1024u;
static char g_geomSizeNote[96] = "default";
static volatile LONG g_exc;                 // an exception in any detour: the unit and the model filter are both off
static volatile LONG g_moff;               // the model filter is off for the process (its path 2 refused, the test table, the tap)
static volatile LONG g_moffReason;          // VPFILTER_R_* / VPMFILTER_R_* of the first turn-off
static char g_moffWhy[256];
static const char* g_mpath2 = "pending";    // pending | not-run | resolved | refused
static char g_mUnableWhy[400];
static volatile LONG g_mDropBits;           // this frame: the bits whose draws may be dropped (VPMODE_CITIES | VPMODE_LANDMARKS)
static volatile LONG g_mTapBits;            // this frame: the bits whose draws are tapped (drop bits, the server's want bits)
static volatile LONG g_mTapAll;             // model_tap_passed (TEST ONLY)
static int g_mapCache = 1;                  // ticket 0719, model_map_cache: 0 direct, 1 cached, 2 cached and verified (TEST ONLY)
static char g_mapCacheNote[96];             // ticket 0719: a CONFIG ERROR line for a bad value (logged at the first Present)
static int g_mHold = 1;                     // ticket 0788: 1 the hold of a newly droppable key (HoldSeenBefore); 0 by C5_SHELL_MODEL_HOLD=0 (diagnostic)
// ticket 0758: how OnUnmap reads a cached map's tail from the driver's pointer (tail_read.h TAILREAD_*), chosen once at the
// init: the CPU's best (TailReadLevel) by default; model_tail_stream (TEST ONLY) = 0 (memcpy), sse41 or avx2 (at most the
// CPU's best), verify (the CPU's best, and every tail run read again with memcpy and compared: tail_diff)
static int g_tailLevel = TAILREAD_COPY, g_tailCpu = TAILREAD_COPY;
static bool g_tailVerify, g_tailKeySet;
static char g_tailNote[128];                // ticket 0758: a CONFIG ERROR or capped-level line (logged at the first Present)
static volatile LONG g_mActive;             // any of the three
static uint32_t g_filterState2 = 0xFFFFFFFFu;
static volatile LONGLONG g_mDrawQpc, g_mPubQpc;   // the tap's time in the draws and in the publication
static bool g_mUnableLogged[2];
static uint32_t g_mFramesUnable[2], g_mFramesSet[2];
static inline bool ModelsOff() { return g_moff || g_exc; }
static inline bool BothOff() { return g_off && ModelsOff(); }   // tracking and path 1 go on while either filter is on
static void MTurnOff(const char* why, LONG reason)
{
	InterlockedCompareExchange(&g_moffReason, reason, 0);
	if (InterlockedExchange(&g_moff, 1) == 0)
	{
		strcpy_s(g_moffWhy, why);
		InterlockedExchange(&g_mDropBits, 0);
	}
}

// ===========================================================================
// Keys, table, SHA-256
// ===========================================================================
static bool Sha256(const void* p, size_t n, uint8_t out[32])
{
	return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)p, (ULONG)n, out, 32) == 0;
}

static void ReadKey(const wchar_t* key, wchar_t* out, DWORD n)
{
	out[0] = 0;
	wchar_t env[64] = L"VPSHELL_";
	for (const wchar_t* k = key; *k && wcslen(env) < 63; ++k) { size_t l = wcslen(env); env[l] = (wchar_t)towupper(*k); env[l + 1] = 0; }
	if (GetEnvironmentVariableW(env, out, n)) return;
	wchar_t dir[MAX_PATH] = L"";
	HMODULE m = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ReadKey, &m);
	GetModuleFileNameW(m, dir, MAX_PATH);
	wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", dir);
	GetPrivateProfileStringW(L"composite", key, L"", out, n, ini);
}

// ticket 0597: model_geom_kb -> g_geomDataBytes (see above); the test's bytes:<n> wins
static void GeomSizeFromKey()
{
	wchar_t v[64];
	ReadKey(L"model_geom_kb", v, 64);
	uint32_t kb = VPSHELL_MODELGEOM_KB_DEFAULT;
	if (v[0])
	{
		wchar_t* end = nullptr;
		const unsigned long x = wcstoul(v, &end, 10);
		if (end == v || *end) _snprintf_s(g_geomSizeNote, _TRUNCATE, "model_geom_kb=%ls is not a number: the default", v);
		else if (x < VPSHELL_MODELGEOM_KB_MIN || x > VPSHELL_MODELGEOM_KB_MAX)
		{
			kb = x < VPSHELL_MODELGEOM_KB_MIN ? VPSHELL_MODELGEOM_KB_MIN : VPSHELL_MODELGEOM_KB_MAX;
			_snprintf_s(g_geomSizeNote, _TRUNCATE, "model_geom_kb=%lu out of %u..%u: clamped", x, VPSHELL_MODELGEOM_KB_MIN, VPSHELL_MODELGEOM_KB_MAX);
		}
		else { kb = (uint32_t)x; _snprintf_s(g_geomSizeNote, _TRUNCATE, "model_geom_kb=%u", kb); }
	}
	g_geomDataBytes = kb * 1024u;
	if (g_geomTestBytes)
	{
		g_geomDataBytes = g_geomTestBytes < 16u ? 16u : g_geomTestBytes;
		_snprintf_s(g_geomSizeNote, _TRUNCATE, "model_geom_test=bytes:%u (TEST ONLY)", g_geomDataBytes);
	}
}

static int HexVal(wchar_t c) { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1; }

// unit_filter_table (test only): "<index> <64 hex>" per line; every index 0..11 must be given once, else the file is refused.
// Ticket 4874: indices 12..21 (the model programs) may follow, all ten or none (none: the model programs keep the table's
// digests); g_testTableModels says which.
static bool g_testTableModels;
static bool LoadTestTable(const wchar_t* path, char* why, size_t whyN)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"rt, ccs=UTF-8") || !f) { _snprintf_s(why, whyN, _TRUNCATE, "cannot be opened"); return false; }
	uint8_t dig[UF_PROGS][32];
	bool have[UF_PROGS] = {};
	wchar_t line[256];
	int lineNo = 0;
	bool ok = true;
	while (ok && fgetws(line, 256, f))
	{
		++lineNo;
		wchar_t* p = line;
		while (*p == L' ' || *p == L'\t') ++p;
		if (!*p || *p == L'\n' || *p == L'\r' || *p == L'#') continue;
		wchar_t* end = nullptr;
		long idx = wcstol(p, &end, 10);
		if (end == p || idx < 0 || idx >= UF_PROGS || have[idx]) { _snprintf_s(why, whyN, _TRUNCATE, "line %d: bad or repeated index", lineNo); ok = false; break; }
		p = end;
		while (*p == L' ' || *p == L'\t') ++p;
		for (int i = 0; i < 32 && ok; ++i)
		{
			int a = HexVal(p[2 * i]), b = a < 0 ? -1 : HexVal(p[2 * i + 1]);
			if (a < 0 || b < 0) { _snprintf_s(why, whyN, _TRUNCATE, "line %d: not 64 hex digits", lineNo); ok = false; }
			else dig[idx][i] = (uint8_t)(a * 16 + b);
		}
		have[idx] = true;
	}
	fclose(f);
	for (int i = 0; ok && i < UF_UNIT_PROGS; ++i)
		if (!have[i]) { _snprintf_s(why, whyN, _TRUNCATE, "program %d missing", i); ok = false; }
	int models = 0;
	for (int i = UF_MODEL0; i < UF_PROGS; ++i) models += have[i] ? 1 : 0;
	if (ok && models && models != VPMODEL_PROGRAMS)
	{
		for (int i = UF_MODEL0; i < UF_PROGS; ++i)
			if (!have[i]) { _snprintf_s(why, whyN, _TRUNCATE, "model program %d missing (give all of 12..21 or none)", i); break; }
		ok = false;
	}
	if (ok)
	{
		memcpy(g_dig, dig, (size_t)UF_UNIT_PROGS * 32);
		if (models) memcpy(g_dig[UF_MODEL0], dig[UF_MODEL0], (size_t)VPMODEL_PROGRAMS * 32);
		g_testTableModels = models != 0;
	}
	return ok;
}

static BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*)
{
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	for (int i = 0; i < UF_PROGS; ++i)
	{
		const uint8_t* d = ProgSha(i);
		memcpy(g_dig[i], d, 32);
		g_stage[i] = ProgStage(i);
		g_pairVs[i] = UF_NONE;
		g_maskPairVs[i] = UF_NONE;
		_snprintf_s(g_short[i], _TRUNCATE, "%02x%02x%02x%02x", d[0], d[1], d[2], d[3]);
	}
	for (int k = 0; k < VPUNIT_DROP_PAIRS; ++k)
	{
		g_isDropPs[kVPUnitDropPairs[k].ps] = 1;
		g_pairVs[kVPUnitDropPairs[k].ps] = kVPUnitDropPairs[k].vs;
	}
	for (int k = 0; k < VPUNIT_MASK_PAIRS; ++k)   // ticket 4785: indices into kVPUnitKeep, which starts at VPUNIT_DROP_PROGRAMS
	{
		const int ps = VPUNIT_DROP_PROGRAMS + kVPUnitMaskPairs[k].ps;
		g_isMaskPs[ps] = 1;
		g_maskPairVs[ps] = (uint8_t)(VPUNIT_DROP_PROGRAMS + kVPUnitMaskPairs[k].vs);
	}
	memset(g_mPairCls, VPMODEL_CLASS_NONE, sizeof(g_mPairCls));
	for (int k = 0; k < VPMODEL_DROP_PAIRS; ++k)
	{
		const VPModelPair& p = kVPModelDropPairs[k];
		g_mPairCls[p.vs][p.ps] = p.cls;
		g_mPairBit[p.cls] = p.bit;
	}
	wchar_t v[MAX_PATH];
	ReadKey(L"unit_filter_fault", v, 64);
	g_fault = !_wcsicmp(v, L"draw") ? UFF_DRAW : !_wcsicmp(v, L"set") ? UFF_SET : !_wcsicmp(v, L"create") ? UFF_CREATE : !_wcsicmp(v, L"present") ? UFF_PRESENT : UFF_NONE;
	ReadKey(L"model_filter_set", v, 64);
	g_mSetAll = !_wcsicmp(v, L"all");
	ReadKey(L"model_geom_test", v, 64);
	g_geomTestFull = !_wcsicmp(v, L"full");
	g_geomTestChanged = !_wcsicmp(v, L"changed");
	g_geomTestMaxBytes = !_wcsnicmp(v, L"oversize:", 9) ? (uint32_t)wcstoul(v + 9, nullptr, 10) : 0u;
	g_geomTestBytes = !_wcsnicmp(v, L"bytes:", 6) ? (uint32_t)wcstoul(v + 6, nullptr, 10) : 0u;
	GeomSizeFromKey();   // ticket 0597: model_geom_kb
	ReadKey(L"model_filter_fault", v, 64);
	g_mfault = !_wcsicmp(v, L"draw") ? MFF_DRAW : !_wcsicmp(v, L"map") ? MFF_MAP : !_wcsicmp(v, L"create") ? MFF_CREATE
	         : !_wcsicmp(v, L"present") ? MFF_PRESENT : !_wcsicmp(v, L"execute") ? MFF_EXECUTE : MFF_NONE;
	// ticket 0788 (see g_mHold): the environment variable C5_SHELL_MODEL_HOLD=0 alone turns the hold off (a diagnostic
	// switch, no ini key: MC #16446)
	v[0] = 0;
	if (!GetEnvironmentVariableW(L"C5_SHELL_MODEL_HOLD", v, 64)) v[0] = 0;
	g_mHold = wcscmp(v, L"0") != 0;
	ReadKey(L"model_map_cache", v, 64);   // ticket 0719 (see g_mapCache)
	if (!v[0] || !wcscmp(v, L"1")) g_mapCache = 1;
	else if (!wcscmp(v, L"0")) g_mapCache = 0;
	else if (!_wcsicmp(v, L"verify")) g_mapCache = 2;
	else { g_mapCache = 1; _snprintf_s(g_mapCacheNote, _TRUNCATE, "CONFIG ERROR model_map_cache='%ls' (0, 1 or verify): 1", v); }
	g_tailCpu = TailReadLevel();   // ticket 0758 (see g_tailLevel)
	g_tailLevel = g_tailCpu;
	ReadKey(L"model_tail_stream", v, 64);
	g_tailKeySet = v[0] != 0;
	if (!v[0] || !_wcsicmp(v, L"auto") || !wcscmp(v, L"1")) g_tailKeySet = false;
	else if (!wcscmp(v, L"0")) g_tailLevel = TAILREAD_COPY;
	else if (!_wcsicmp(v, L"verify")) g_tailVerify = true;
	else if (!_wcsicmp(v, L"sse41") || !_wcsicmp(v, L"avx2"))
	{
		const int want = !_wcsicmp(v, L"avx2") ? TAILREAD_AVX2 : TAILREAD_SSE41;
		g_tailLevel = want < g_tailCpu ? want : g_tailCpu;
		if (want > g_tailCpu)
			_snprintf_s(g_tailNote, _TRUNCATE, "model_tail_stream=%ls: this CPU has %s at most: %s", v, kTailReadNames[g_tailCpu], kTailReadNames[g_tailLevel]);
	}
	else { g_tailKeySet = false; _snprintf_s(g_tailNote, _TRUNCATE, "CONFIG ERROR model_tail_stream='%ls' (auto, 0, sse41, avx2 or verify): auto", v); }
	ReadKey(L"unit_filter_table", v, MAX_PATH);
	if (v[0])
	{
		wcsncpy_s(g_testTablePath, v, _TRUNCATE);
		char why[96] = "";
		g_testTable = LoadTestTable(v, why, sizeof(why));
		if (g_testTable) _snprintf_s(g_testTableNote, _TRUNCATE, "TEST TABLE %ls (TEST ONLY: the digests of the 12 programs replaced)%s", v,
		                             g_testTableModels ? " (and the 10 model programs')" : "");
		else
		{
			_snprintf_s(g_testTableNote, _TRUNCATE, "TEST TABLE %ls REFUSED (%s): the unit filter is off", v, why);
			InterlockedCompareExchange(&g_offReason, VPFILTER_R_TABLE_REFUSED, 0);   // before g_off (issue 4761)
			InterlockedExchange(&g_off, 1);
			strcpy_s(g_offWhy, "unit_filter_table refused");
			MTurnOff("unit_filter_table refused", VPFILTER_R_TABLE_REFUSED);   // ticket 4874: the models' digests come from it too
		}
	}
	return TRUE;
}

static inline void EnsureInit() { InitOnceExecuteOnce(&g_initOnce, InitOnce, nullptr, nullptr); }

// issue 4761: the reason is stored before g_off (the first one wins), so a Present that sees g_off sees its reason
static inline void SetOffReason(LONG reason) { InterlockedCompareExchange(&g_offReason, reason, 0); }

static void TurnOff(const char* why, LONG reason)
{
	SetOffReason(reason);
	if (InterlockedExchange(&g_off, 1) == 0)
	{
		strcpy_s(g_offWhy, why);
		InterlockedExchange(&g_dropNow, 0);
		InterlockedExchange(&g_maskDropNow, 0);   // ticket 4785
	}
}

static void OnException(const char* where, DWORD code)
{
	InterlockedExchange(&g_exc, 1);
	SetOffReason(VPFILTER_R_EXCEPTION);
	if (InterlockedExchange(&g_off, 1) == 0)
	{
		_snprintf_s(g_offWhy, _TRUNCATE, "exception 0x%08X in %s", (unsigned)code, where);
		InterlockedExchange(&g_dropNow, 0);
		InterlockedExchange(&g_maskDropNow, 0);   // ticket 4785
		ULOG("unit_filter: LOUD: EXCEPTION 0x%08X in the %s detour - the unit filter is OFF for this process (every draw passes; "
		     "the composite goes on)", (unsigned)code, where);
	}
}

static inline void MaybeFault(int where)
{
	if (g_fault == where && InterlockedExchange(&g_faultFired, 1) == 0) *(volatile int*)nullptr = 0x4752;
}
static inline void MaybeMFault(int where)
{
	if (g_mfault == where && InterlockedExchange(&g_mfaultFired, 1) == 0) *(volatile int*)nullptr = 0x4874;
}

// ===========================================================================
// Path 1: creation
// ===========================================================================
static int Classify(const void* bc, SIZE_T n, uint8_t stage)
{
	if (!bc || n < 32 || n > (64u << 20)) return UF_NONE;
	uint8_t d[32];
	if (!Sha256(bc, n, d)) return UF_NONE;
	InterlockedIncrement(&g_createsHashed);
	for (int i = 0; i < UF_PROGS; ++i)
		if (g_stage[i] == stage && !memcmp(d, g_dig[i], 32)) return i;
	return UF_NONE;
}

static void OnCreated(const void* bc, SIZE_T n, void* obj, uint8_t stage)
{
	MaybeFault(UFF_CREATE);
	int p = Classify(bc, n, stage);
	if (p != UF_NONE) InterlockedIncrement(&g_created[p]);
	if (p == UF_NONE && MarkLookup(obj) == UF_NONE) return;   // nothing to record: unmarked stays unmarked
	if (MarkSet(obj, p)) { if (p != UF_NONE) InterlockedIncrement(&g_marked[p]); }
	else if (InterlockedIncrement(&g_markFull) == 1) ULOG("unit_filter: LOUD: the table of marked objects is full; object %p not recorded", obj);
}

static void SafeOnCreated(const void* bc, SIZE_T n, void* obj, uint8_t stage)
{
	__try { OnCreated(bc, n, obj, stage); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException(stage == VPUNIT_STAGE_VS ? "CreateVertexShader" : "CreatePixelShader", GetExceptionCode()); }
}

HRESULT UnitFilter_CreateVS(PFN_UfCreateVS tr, ID3D11Device* d, const void* bc, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** out)
{
	HRESULT hr = tr(d, bc, n, l, out);
	EnsureInit();
	if (SUCCEEDED(hr) && out && *out && !BothOff()) SafeOnCreated(bc, n, *out, VPUNIT_STAGE_VS);
	return hr;
}

HRESULT UnitFilter_CreatePS(PFN_UfCreatePS tr, ID3D11Device* d, const void* bc, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** out)
{
	HRESULT hr = tr(d, bc, n, l, out);
	EnsureInit();
	if (SUCCEEDED(hr) && out && *out && !BothOff()) SafeOnCreated(bc, n, *out, VPUNIT_STAGE_PS);
	return hr;
}

// ===========================================================================
// Tracking
// ===========================================================================
static __declspec(noinline) UfCtx* SlotMiss(void* c)
{
	UfCtx* s = nullptr;
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i) if (g_ctxs[i].ctx == c) { s = &g_ctxs[i]; break; }
	if (!s)
	{
		AcquireSRWLockExclusive(&g_ctxLock);
		n = g_nCtx;
		for (LONG i = 0; i < n; ++i) if (g_ctxs[i].ctx == c) { s = &g_ctxs[i]; break; }
		if (!s && n < kCtx)
		{
			s = &g_ctxs[n];
			s->vs = s->ps = UF_NONE; s->any = 0;
			s->deferred = -1;
			s->ctx = c;
			MemoryBarrier();
			InterlockedExchange(&g_nCtx, n + 1);
		}
		ReleaseSRWLockExclusive(&g_ctxLock);
	}
	if (s) { t_ctx = c; t_slot = s; }
	return s;
}

static inline UfCtx* Slot(void* c) { return c == t_ctx ? t_slot : SlotMiss(c); }

static void SetTracked(void* c, void* shader, bool vs)
{
	MaybeFault(UFF_SET);
	UfCtx* s = Slot(c);
	if (!s) return;
	int p = MarkLookup(shader);
	if (p != UF_NONE && g_stage[p] != (vs ? VPUNIT_STAGE_VS : VPUNIT_STAGE_PS)) p = UF_NONE;
	if (vs) s->vs = (uint8_t)p; else s->ps = (uint8_t)p;
	s->any = (s->vs != UF_NONE || s->ps != UF_NONE) ? 1 : 0;
}

static void SafeSetTracked(void* c, void* shader, bool vs)
{
	__try { SetTracked(c, shader, vs); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException(vs ? "VSSetShader" : "PSSetShader", GetExceptionCode()); }
}

void UnitFilter_VSSet(PFN_UfVSSet tr, ID3D11DeviceContext* c, ID3D11VertexShader* sh, ID3D11ClassInstance* const* ci, UINT n)
{
	tr(c, sh, ci, n);
	if (!BothOff()) SafeSetTracked(c, sh, true);
}

void UnitFilter_PSSet(PFN_UfPSSet tr, ID3D11DeviceContext* c, ID3D11PixelShader* sh, ID3D11ClassInstance* const* ci, UINT n)
{
	tr(c, sh, ci, n);
	if (!BothOff()) SafeSetTracked(c, sh, false);
}

void UnitFilter_OnStateReset(ID3D11DeviceContext* c)
{
	if (BothOff() || !c) return;
	__try
	{
		LONG n = g_nCtx;
		for (LONG i = 0; i < n; ++i)
			if (g_ctxs[i].ctx == c) { g_ctxs[i].vs = g_ctxs[i].ps = UF_NONE; g_ctxs[i].any = 0; break; }
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("state reset", GetExceptionCode()); }
}

// ===========================================================================
// Ticket 4874: the model tap - resource identity, the Unmap copy, the records (city-renderer.md 10)
// ===========================================================================
static inline int64_t QpcNow() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
struct UfDrawArgs { uint32_t indexCount, instanceCount, startIndex, startInstance; int32_t baseVertex; int indexed; };

// ---- resource identity: the content hashes of the key, taken at CreateBuffer / CreateTexture2D with initial data ----
// Open addressing keyed by the object's address; a new creation at an address overwrites its entry (a creation without
// identity at a known address clears it), so a reused address never keeps an old identity. Readers are lock-free (a
// per-entry sequence); writers take the lock.
enum { RK_NONE = 0, RK_VB = 1, RK_IB = 2, RK_TEX = 4 };
struct ResId { void* volatile obj; volatile LONG seq; uint32_t kind; uint32_t pad; uint64_t h0, h16, h32; };
static const unsigned kResIds = 1u << 16;
static ResId g_res[kResIds];
static SRWLOCK g_resLock = SRWLOCK_INIT;
static volatile LONG g_resUsed, g_resFull;
static volatile LONG g_bufInit, g_bufNoInit, g_texInit, g_texNoInit, g_texNoId;
static volatile LONGLONG g_hashBytes, g_hashQpc;

static inline unsigned ResHash(const void* p) { return (unsigned)(((uint32_t)((uintptr_t)p >> 4) * 2654435761u) >> 16) & (kResIds - 1); }

static void ResPut(void* obj, uint32_t kind, uint64_t h0, uint64_t h16, uint64_t h32, bool onlyIfKnown)
{
	AcquireSRWLockExclusive(&g_resLock);
	unsigned i = ResHash(obj);
	for (unsigned k = 0; k < kResIds; ++k, i = (i + 1) & (kResIds - 1))
	{
		void* o = g_res[i].obj;
		if (o != obj && o) continue;
		if (!o && onlyIfKnown) break;
		if (!o && (unsigned)g_resUsed >= kResIds / 4 * 3) { InterlockedIncrement(&g_resFull); break; }
		ResId& r = g_res[i];
		InterlockedIncrement(&r.seq);   // odd: being written
		r.kind = kind; r.h0 = h0; r.h16 = h16; r.h32 = h32;
		if (!o) { MemoryBarrier(); r.obj = obj; InterlockedIncrement(&g_resUsed); }
		InterlockedIncrement(&r.seq);
		break;
	}
	ReleaseSRWLockExclusive(&g_resLock);
}

static bool ResGet(const void* obj, uint32_t& kind, uint64_t& h0, uint64_t& h16, uint64_t& h32)
{
	if (!obj) return false;
	unsigned i = ResHash(obj);
	for (unsigned k = 0; k < kResIds; ++k, i = (i + 1) & (kResIds - 1))
	{
		void* o = g_res[i].obj;
		if (!o) return false;
		if (o != obj) continue;
		for (int t = 0; t < 8; ++t)
		{
			const LONG s0 = g_res[i].seq;
			if (s0 & 1) { YieldProcessor(); continue; }
			MemoryBarrier();
			kind = g_res[i].kind; h0 = g_res[i].h0; h16 = g_res[i].h16; h32 = g_res[i].h32;
			MemoryBarrier();
			if (g_res[i].seq == s0) return true;
		}
		return false;
	}
	return false;
}

static void OnBufferCreated(const D3D11_BUFFER_DESC* bd, const D3D11_SUBRESOURCE_DATA* sd, void* obj)
{
	MaybeMFault(MFF_CREATE);
	const UINT bind = bd->BindFlags;
	// only IMMUTABLE vertex and index buffers get an identity (#6273: their content cannot change after the hash)
	if (!(bind & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER)) || bd->Usage != D3D11_USAGE_IMMUTABLE) { ResPut(obj, RK_NONE, 0, 0, 0, true); return; }
	if (!sd || !sd->pSysMem || !bd->ByteWidth) { InterlockedIncrement(&g_bufNoInit); ResPut(obj, RK_NONE, 0, 0, 0, true); return; }
	InterlockedIncrement(&g_bufInit);
	const int64_t t0 = QpcNow();
	const uint8_t* p = (const uint8_t*)sd->pSysMem;
	const uint32_t n = bd->ByteWidth;
	uint64_t h0 = 0, h16 = 0, h32 = 0, hashed = 0;
	uint32_t kind = RK_NONE;
	if (bind & D3D11_BIND_VERTEX_BUFFER) { h0 = VPModelHash64(p, n); kind |= RK_VB; hashed += n; }
	if (bind & D3D11_BIND_INDEX_BUFFER)
	{
		// from index 1 (the pad index 0 is not stable, 4870 5.5); the index size is known only at the bind: both kept
		h16 = n > 2 ? VPModelHash64(p + 2, n - 2) : 0;
		h32 = n > 4 ? VPModelHash64(p + 4, n - 4) : 0;
		kind |= RK_IB; hashed += 2ull * n;
	}
	ResPut(obj, kind, h0, h16, h32, false);
	InterlockedExchangeAdd64(&g_hashBytes, (LONGLONG)hashed);
	InterlockedExchangeAdd64(&g_hashQpc, QpcNow() - t0);
}

static void OnTextureCreated(const D3D11_TEXTURE2D_DESC* td, const D3D11_SUBRESOURCE_DATA* sd, void* obj)
{
	MaybeMFault(MFF_CREATE);
	// only DEFAULT / IMMUTABLE shader-resource textures without RT, DS or UAV binding (#6273; a later write clears it)
	if (!(td->BindFlags & D3D11_BIND_SHADER_RESOURCE) || (td->Usage != D3D11_USAGE_DEFAULT && td->Usage != D3D11_USAGE_IMMUTABLE) ||
	    (td->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS))) { ResPut(obj, RK_NONE, 0, 0, 0, true); return; }
	if (!sd) { InterlockedIncrement(&g_texNoInit); ResPut(obj, RK_NONE, 0, 0, 0, true); return; }
	InterlockedIncrement(&g_texInit);
	const int64_t t0 = QpcNow();
	uint32_t levels = td->MipLevels;
	if (!levels) { levels = 1; for (uint32_t m = td->Width > td->Height ? td->Width : td->Height; m > 1; m >>= 1) ++levels; }
	const uint32_t mip = VPModelTextureMip(td->Width, td->Height, levels);
	const uint32_t w = (td->Width >> mip) ? (td->Width >> mip) : 1u, h = (td->Height >> mip) ? (td->Height >> mip) : 1u;
	const D3D11_SUBRESOURCE_DATA& s = sd[mip];   // array slice 0: subresource index = mip level
	const uint64_t id = s.pSysMem ? VPModelTextureHash(s.pSysMem, s.SysMemPitch, w, h, (uint32_t)td->Format) : 0;
	if (!id) InterlockedIncrement(&g_texNoId);
	ResPut(obj, id ? RK_TEX : RK_NONE, id, 0, 0, !id);
	uint32_t bw = 1; const uint32_t bb = VPModelFormatBlock((uint32_t)td->Format, &bw);
	InterlockedExchangeAdd64(&g_hashBytes, (LONGLONG)((w + bw - 1) / bw * bb) * ((h + bw - 1) / bw));
	InterlockedExchangeAdd64(&g_hashQpc, QpcNow() - t0);
}

// Only resources whose content cannot change after the hash get an identity (lead2-0561 #6273): IMMUTABLE buffers;
// DEFAULT / IMMUTABLE shader-resource textures without RT, DS or UAV binding (a later write the compositor sees clears it).
static bool BufferKeyable(const D3D11_BUFFER_DESC& d) { return d.Usage == D3D11_USAGE_IMMUTABLE && (d.BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER)); }
static bool TextureKeyable(const D3D11_TEXTURE2D_DESC& d)
{
	return (d.Usage == D3D11_USAGE_DEFAULT || d.Usage == D3D11_USAGE_IMMUTABLE) && (d.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
	       !(d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS));
}
static volatile LONG g_writeCleared;        // identities cleared by a later write (UpdateSubresource, CopyResource, ...)

// ---- the read-back (the ticket's "else once per resource at first sight"; lead2-0561 #6273): a key part not in the
// creation table is AddRef'd and queued at the draw (which passes); at a Present the immediate context copies it to a
// staging resource (a buffer whole, a texture only its identity mip of slice 0), a later Present maps it with
// DO_NOT_WAIT, hashes it with the creation path's functions and enters it in the table. Never retried; bounded per
// Present; never a stall. ----
enum { RB_FREE, RB_NEW, RB_COPIED, RB_DONE, RB_FAILED };
struct ReadBack { void* volatile res; volatile LONG state; ID3D11Resource* staging; uint32_t present, tex, mip, w, h, fmt, bytes, bind; };
static const unsigned kReadBacks = 4096;
static ReadBack g_rb[kReadBacks];
static SRWLOCK g_rbLock = SRWLOCK_INIT;
static volatile LONG g_rbNew, g_rbHashed, g_rbFailed, g_rbFull, g_rbNotKeyable, g_rbPending;
static volatile LONGLONG g_rbBytes;

static void ReadBackRequest(ID3D11Resource* r, bool tex)
{
	if (!r) return;
	const unsigned mask = kReadBacks - 1;
	AcquireSRWLockExclusive(&g_rbLock);
	for (unsigned i = (unsigned)(((uint32_t)((uintptr_t)r >> 4) * 2654435761u) >> 16) & mask, k = 0; k < kReadBacks; ++k, i = (i + 1) & mask)
	{
		void* o = g_rb[i].res;
		if (o == r) break;   // queued or done before (a done entry stays as the mark: never retried)
		if (o) continue;
		if ((unsigned)g_rbNew >= kReadBacks / 4 * 3) { InterlockedIncrement(&g_rbFull); break; }
		r->AddRef();   // held until hashed: the address cannot be reused meanwhile
		g_rb[i].tex = tex ? 1u : 0u; g_rb[i].staging = nullptr;
		MemoryBarrier();
		g_rb[i].res = r;
		InterlockedExchange(&g_rb[i].state, RB_NEW);
		InterlockedIncrement(&g_rbNew);
		InterlockedIncrement(&g_rbPending);
		break;
	}
	ReleaseSRWLockExclusive(&g_rbLock);
}

static void ReadBackDone(ReadBack& b, bool ok)
{
	if (b.staging) { b.staging->Release(); b.staging = nullptr; }
	((ID3D11Resource*)b.res)->Release();
	InterlockedExchange(&b.state, ok ? RB_DONE : RB_FAILED);
	InterlockedIncrement(ok ? &g_rbHashed : &g_rbFailed);
	InterlockedDecrement(&g_rbPending);
}

// render thread, at a Present: start up to 64 copies (32 MB) and finish the copies of earlier Presents that are ready
static void ReadBackPresent(ID3D11Device* dev, uint32_t presentNo)
{
	if (!g_rbPending || !dev) return;
	ID3D11DeviceContext* ctx = nullptr;
	dev->GetImmediateContext(&ctx);
	if (!ctx) return;
	uint32_t started = 0; uint64_t bytes = 0;
	for (unsigned i = 0; i < kReadBacks && g_rbPending; ++i)
	{
		ReadBack& b = g_rb[i];
		const LONG st = b.state;
		if (st == RB_NEW && started < 64 && bytes < (32u << 20))
		{
			ID3D11Resource* r = (ID3D11Resource*)b.res;
			if (!b.tex)
			{
				ID3D11Buffer* buf = nullptr;
				D3D11_BUFFER_DESC d = {};
				if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Buffer), (void**)&buf)) && buf) { buf->GetDesc(&d); buf->Release(); }
				if (!BufferKeyable(d)) { InterlockedIncrement(&g_rbNotKeyable); ReadBackDone(b, false); continue; }
				D3D11_BUFFER_DESC sd = {}; sd.ByteWidth = d.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				ID3D11Buffer* stg = nullptr;
				if (FAILED(dev->CreateBuffer(&sd, nullptr, &stg)) || !stg) { ReadBackDone(b, false); continue; }
				ctx->CopyResource(stg, r);
				b.staging = stg; b.bytes = d.ByteWidth; b.bind = d.BindFlags;
			}
			else
			{
				ID3D11Texture2D* t = nullptr;
				D3D11_TEXTURE2D_DESC d = {};
				if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { t->GetDesc(&d); t->Release(); }
				else { ReadBackDone(b, false); continue; }
				uint32_t bw = 1;
				if (!TextureKeyable(d) || !VPModelFormatBlock((uint32_t)d.Format, &bw)) { InterlockedIncrement(&g_rbNotKeyable); ReadBackDone(b, false); continue; }
				const uint32_t levels = d.MipLevels ? d.MipLevels : 1u;
				const uint32_t mip = VPModelTextureMip(d.Width, d.Height, levels);
				D3D11_TEXTURE2D_DESC sd = d;
				sd.Width = (d.Width >> mip) ? (d.Width >> mip) : 1u; sd.Height = (d.Height >> mip) ? (d.Height >> mip) : 1u;
				sd.MipLevels = 1; sd.ArraySize = 1; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
				sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
				ID3D11Texture2D* stg = nullptr;
				if (FAILED(dev->CreateTexture2D(&sd, nullptr, &stg)) || !stg) { ReadBackDone(b, false); continue; }   // e.g. a BC mip not a multiple of 4
				ctx->CopySubresourceRegion(stg, 0, 0, 0, 0, r, D3D11CalcSubresource(mip, 0, levels), nullptr);
				b.staging = stg; b.mip = mip; b.w = sd.Width; b.h = sd.Height; b.fmt = (uint32_t)d.Format;
				b.bytes = (sd.Width + bw - 1) / bw * VPModelFormatBlock((uint32_t)d.Format, &bw) * ((sd.Height + bw - 1) / bw);
			}
			b.present = presentNo;
			InterlockedExchange(&b.state, RB_COPIED);
			started++; bytes += b.bytes;
			continue;
		}
		if (st == RB_COPIED && presentNo != b.present)
		{
			D3D11_MAPPED_SUBRESOURCE m;
			const HRESULT hr = ctx->Map(b.staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
			if (hr == DXGI_ERROR_WAS_STILL_DRAWING) continue;   // a later Present
			if (FAILED(hr)) { ReadBackDone(b, false); continue; }
			if (!b.tex)
			{
				const uint8_t* p = (const uint8_t*)m.pData;
				const uint32_t n = b.bytes;
				uint32_t kind = RK_NONE; uint64_t h0 = 0, h16 = 0, h32 = 0;
				if (b.bind & D3D11_BIND_VERTEX_BUFFER) { h0 = VPModelHash64(p, n); kind |= RK_VB; }
				if (b.bind & D3D11_BIND_INDEX_BUFFER) { h16 = n > 2 ? VPModelHash64(p + 2, n - 2) : 0; h32 = n > 4 ? VPModelHash64(p + 4, n - 4) : 0; kind |= RK_IB; }
				ResPut(b.res, kind, h0, h16, h32, false);
			}
			else
			{
				const uint64_t id = VPModelTextureHash(m.pData, m.RowPitch, b.w, b.h, b.fmt);
				ResPut(b.res, id ? RK_TEX : RK_NONE, id, 0, 0, false);
			}
			ctx->Unmap(b.staging, 0);
			InterlockedExchangeAdd64(&g_rbBytes, b.bytes);
			ReadBackDone(b, true);
		}
	}
	ctx->Release();
}

// a write into a resource after its hash: its identity is cleared (counted)
static void OnResourceWritten(ID3D11Resource* r)
{
	uint32_t kind; uint64_t h0, h16, h32;
	if (r && ResGet(r, kind, h0, h16, h32) && kind != RK_NONE) { ResPut(r, RK_NONE, 0, 0, 0, true); InterlockedIncrement(&g_writeCleared); }
}

// ---- the Unmap copy: buffers a family draw read (marked at the draw), copied at each Unmap of a WRITE map ----
// bytes: the buffer's size; need: the end of the part the family draws read so far (a high-water mark, only grows): an
// Unmap copies [0, need + need/4 + 1 KB) of the buffer, at most its size (live load 1, 2026-11-18: copying whole
// buffers read about 440 KB of upload memory per frame at about 158 MB/s, 2.8 ms per frame). A draw past the copy passes
// (no_shadow) and raises the mark for the next frame: never a drop without its record.
struct Interest { void* volatile res; uint32_t bytes; volatile LONG need; };
static const unsigned kInterest = 256;
static Interest g_int[kInterest];
static volatile LONG g_nInt, g_intFull;
static SRWLOCK g_intLock = SRWLOCK_INIT;
static inline unsigned PtrHash8(const void* p, unsigned mask) { return (unsigned)(((uint32_t)((uintptr_t)p >> 4) * 2654435761u) >> 20) & mask; }

static uint32_t InterestGet(const void* res)
{
	for (unsigned i = PtrHash8(res, kInterest - 1), k = 0; k < kInterest; ++k, i = (i + 1) & (kInterest - 1))
	{
		void* o = g_int[i].res;
		if (o == res) return g_int[i].bytes;
		if (!o) return 0;
	}
	return 0;
}

// the copy length of an Unmap of res: [0, need + need/4 + 1 KB), 16-byte aligned, at most the buffer
static uint32_t InterestCopyBytes(const void* res, uint32_t* needOut = nullptr)
{
	for (unsigned i = PtrHash8(res, kInterest - 1), k = 0; k < kInterest; ++k, i = (i + 1) & (kInterest - 1))
	{
		void* o = g_int[i].res;
		if (o == res)
		{
			const uint32_t need = (uint32_t)g_int[i].need, size = g_int[i].bytes;
			const uint32_t want = (need + need / 4 + 1024u + 15u) & ~15u;
			if (needOut) *needOut = need;   // ticket 0719
			return want < size ? want : size;
		}
		if (!o) return 0;
	}
	return 0;
}

// a draw read res up to `end`: the mark only grows
static void InterestNeed(const void* res, uint32_t end)
{
	if (!res) return;
	for (unsigned i = PtrHash8(res, kInterest - 1), k = 0; k < kInterest; ++k, i = (i + 1) & (kInterest - 1))
	{
		void* o = g_int[i].res;
		if (o == res)
		{
			LONG cur = g_int[i].need;
			while ((uint32_t)cur < end)
			{
				const LONG was = InterlockedCompareExchange(&g_int[i].need, (LONG)end, cur);
				if (was == cur) break;
				cur = was;
			}
			return;
		}
		if (!o) return;
	}
}

static void InterestAdd(void* res, uint32_t bytes)
{
	if (!res || !bytes) return;
	AcquireSRWLockExclusive(&g_intLock);
	for (unsigned i = PtrHash8(res, kInterest - 1), k = 0; k < kInterest; ++k, i = (i + 1) & (kInterest - 1))
	{
		void* o = g_int[i].res;
		if (o == res) { if (g_int[i].bytes < bytes) g_int[i].bytes = bytes; break; }
		if (!o)
		{
			if ((unsigned)g_nInt >= kInterest / 4 * 3) { InterlockedIncrement(&g_intFull); break; }
			g_int[i].bytes = bytes;
			MemoryBarrier();
			g_int[i].res = res;
			InterlockedIncrement(&g_nInt);
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_intLock);
}

// a copy of one buffer as one context last wrote it (ci = the context's slot); written and read only by that context's
// thread, the immediate context's ones also by the render thread (the same thread)
// Ticket 0719: stage/stageCap/pages are the cached map's staging of the same (buffer, context), touched only by that
// context's thread between its Map and its Unmap (never by the placement).
// old/oldBytes/oldCap (verify only): [0, bytes) as the direct path reads it, from the driver's memory after the forward
struct Shadow { void* volatile res; volatile LONG ci; uint8_t* data; uint32_t bytes, cap, epoch; volatile LONG version;
	uint8_t* stage; uint32_t stageCap; ULONG_PTR* pages; uint8_t* old; uint32_t oldBytes, oldCap;
	uint32_t lastPages, lastMapBytes; uint8_t lastCached; };   // verify diagnostics: the last Unmap's written pages, length, path
static const unsigned kShadows = 1024;
static Shadow g_sh[kShadows];
static SRWLOCK g_shLock = SRWLOCK_INIT;
static volatile LONG g_nSh, g_shFull, g_omFull, g_shNomem;
static volatile LONGLONG g_copyBytes, g_copyQpc;
static volatile LONG g_immCi = -1;          // the immediate context's slot

// ---- ticket 0719: the cached map ----
// The EXE's dynamic vertex and instance buffers come back from Map as uncached (write-combined) memory: the copy above read
// them at 118-249 MB/s on an RTX 4080 and about 500 MB/s on an RTX 2080 (research/05-graphics-composition/
// frame-cost-exe-side.md 1.6). With model_map_cache=1 (the default) a WRITE_DISCARD Map of a buffer the copy wants that is
// DYNAMIC and not a constant buffer (constant buffers come back cached) hands the EXE a staging of ordinary memory instead,
// one per (buffer, context), allocated with MEM_WRITE_WATCH. Its Unmap forwards every page the EXE wrote since the Map
// (GetWriteWatch, reset at each read) to the driver's pointer, whole pages: after WRITE_DISCARD the rest of the buffer is
// undefined, so a page's bytes the EXE did not write this time carry the staging's older bytes there, never over data the
// GPU may use. The copy then reads the staging. Every other Map (WRITE_NO_OVERWRITE, WRITE, the read maps, constant
// buffers, a full table) is the direct path as before: the EXE writes the driver's memory itself. A cached map's Unmap
// forwards even when the model filter went off between the two calls (g_cachedOpen). model_map_cache=0 = the direct path
// for every map; model_map_cache=verify (TEST ONLY) also reads the forwarded pages back from the driver's pointer and
// compares them with the staging (verify_bad), and counts pages written into a staging outside a Map (late_pages).
// g_mapCache, g_mapCacheNote: with the model filter's state above (read at the init)
static const uint32_t kStageMaxBytes = 64u << 20;   // all stagings together; past it a map takes the direct path (stage_fail)
static volatile LONG g_cachedOpen;          // cached maps handed out whose Unmap has not come yet
static volatile LONG g_mapCached, g_mapDirect, g_stageFail, g_wwFail, g_verifyBad, g_latePages;
static volatile LONGLONG g_fwdBytes, g_fwdQpc, g_stageBytes, g_verifyBytes, g_unwrittenBytes, g_tailBytes;   // tail: unwritten bytes below need read from the driver
// ticket 0758: the tail read's time (inside copy_ms: tail_ms), and with model_tail_stream=verify the bytes read again with
// memcpy (tail_vkb) and the runs whose bytes differed (tail_diff, must stay 0)
static volatile LONGLONG g_tailQpc, g_tailVBytes;
static volatile LONG g_tailDiff;
// verify only: every read of a copy into a record compared with the direct path's copy of the same bytes (the driver's
// memory read after the forward): rec_kb compared, rec_diff reads that differ, rec_noold reads with no direct copy to compare
static volatile LONG g_recDiff, g_recNoOld;
static volatile LONGLONG g_recBytes;

static void VerifyRead(const uint8_t* src, size_t n)
{
	for (unsigned i = 0; i < kShadows; ++i)
	{
		const Shadow& s = g_sh[i];
		if (!s.data || src < s.data || src >= s.data + s.bytes) continue;
		const size_t off = (size_t)(src - s.data);
		if (!s.old || off + n > s.oldBytes) { InterlockedIncrement(&g_recNoOld); return; }
		if (memcmp(src, s.old + off, n))
		{
			if (InterlockedIncrement(&g_recDiff) <= 24)
			{
				size_t k = 0;
				while (k < n && src[k] == s.old[off + k]) ++k;
				const size_t at = off + k;
				bool inWritten = false;
				if (s.lastPages == 0xFFFFFFFFu) inWritten = true;
				for (uint32_t p = 0; s.lastPages != 0xFFFFFFFFu && p < s.lastPages && s.stage; ++p)
					if ((size_t)((uintptr_t)s.pages[p] - (uintptr_t)s.stage) == (at & ~(size_t)4095)) inWritten = true;
				uint32_t a = 0, b = 0;
				memcpy(&a, s.data + (at & ~(size_t)3), at + 4 <= s.bytes ? 4 : 0);
				memcpy(&b, s.old + (at & ~(size_t)3), at + 4 <= s.oldBytes ? 4 : 0);
				ULOG("model_filter: verify: record read differs: res %p slot %ld at %u (read %u+%u) last unmap %s, %u bytes, %u pages written, "
				     "this page %s; copy %08X direct %08X", s.res, s.ci, (unsigned)at, (unsigned)off, (unsigned)n, s.lastCached ? "cached" : "direct",
				     s.lastMapBytes, s.lastPages, inWritten ? "written" : "NOT written", a, b);
			}
		}
		InterlockedExchangeAdd64(&g_recBytes, (LONGLONG)n);
		return;
	}
	InterlockedIncrement(&g_recNoOld);
}

static inline unsigned ShHash(const void* res, int ci) { return (PtrHash8(res, 0xFFFFFu) * 31u + (unsigned)ci) & (kShadows - 1); }

static Shadow* ShadowFind(const void* res, int ci)
{
	if (ci < 0) return nullptr;
	for (unsigned i = ShHash(res, ci), k = 0; k < kShadows; ++k, i = (i + 1) & (kShadows - 1))
	{
		void* o = g_sh[i].res;
		if (!o) return nullptr;
		if (o == res && g_sh[i].ci == ci) return &g_sh[i];
	}
	return nullptr;
}

static Shadow* ShadowMake(void* res, int ci)
{
	Shadow* s = ShadowFind(res, ci);
	if (s) return s;
	AcquireSRWLockExclusive(&g_shLock);
	for (unsigned i = ShHash(res, ci), k = 0; k < kShadows; ++k, i = (i + 1) & (kShadows - 1))
	{
		void* o = g_sh[i].res;
		if (o == res && g_sh[i].ci == ci) { s = &g_sh[i]; break; }
		if (!o)
		{
			if ((unsigned)g_nSh >= kShadows / 4 * 3) { InterlockedIncrement(&g_shFull); break; }
			g_sh[i].ci = ci;
			MemoryBarrier();
			g_sh[i].res = res;
			InterlockedIncrement(&g_nSh);
			s = &g_sh[i];
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_shLock);
	return s;
}

// a slot without creating one (Map/Unmap must not count as "a hooked draw reached the filter", issue 4761's reason 8)
static UfCtx* SlotFind(void* c)
{
	if (c == t_ctx) return t_slot;
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i) if (g_ctxs[i].ctx == c) return &g_ctxs[i];
	return nullptr;
}

static inline int SlotIndex(const UfCtx* s) { return (int)(s - g_ctxs); }

static void KnowType(ID3D11DeviceContext* c, UfCtx* s)
{
	if (s->deferred >= 0) return;
	const bool imm = c->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
	s->deferred = imm ? 0 : 1;
	if (imm) InterlockedExchange(&g_immCi, SlotIndex(s));
}

// ticket 0719: the buffer's ByteWidth when a WRITE_DISCARD map of it may be cached (DYNAMIC, not a constant buffer), else 0
static uint32_t CacheableBytes(ID3D11Resource* r)
{
	D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
	r->GetType(&dim);
	if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return 0;
	D3D11_BUFFER_DESC d = {};
	static_cast<ID3D11Buffer*>(r)->GetDesc(&d);
	if (d.Usage != D3D11_USAGE_DYNAMIC || (d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) return 0;
	return d.ByteWidth;
}

// ticket 0719: a staging of at least `size` bytes for this shadow (write-watched, 64 KB granular); false = the direct path
static bool StageEnsure(Shadow* sh, uint32_t size)
{
	if (sh->stage && sh->stageCap >= size) return true;
	const uint32_t need = (size + 0xFFFFu) & ~0xFFFFu;
	if (!need || (uint64_t)g_stageBytes + need > kStageMaxBytes) { InterlockedIncrement(&g_stageFail); return false; }
	uint8_t* p = (uint8_t*)VirtualAlloc(nullptr, need, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
	ULONG_PTR* pg = p ? (ULONG_PTR*)malloc((need / 4096u) * sizeof(ULONG_PTR)) : nullptr;
	if (!p || !pg)
	{
		if (p) VirtualFree(p, 0, MEM_RELEASE);
		InterlockedIncrement(&g_stageFail);
		return false;
	}
	// Every page touched once, then the watch reset: a demand-zero page counts as written when it is first touched, even by
	// a read (the copy reads [0, want), past what a partial map wrote), and was forwarded once at the next map (seen in the
	// stand-in, 2026-11-23: two pages "written outside a map" in the split case; harmless after DISCARD, but noise)
	memset(p, 0, need);
	ResetWriteWatch(p, need);
	if (sh->stage)
	{
		VirtualFree(sh->stage, 0, MEM_RELEASE);
		InterlockedExchangeAdd64(&g_stageBytes, -(LONGLONG)sh->stageCap);
	}
	free(sh->pages);
	sh->stage = p; sh->stageCap = need; sh->pages = pg;
	InterlockedExchangeAdd64(&g_stageBytes, need);
	return true;
}

// ticket 0719: the pages of [stage, stage + size) written since the last read (reset = clear them); false = GetWriteWatch failed
static bool WrittenPages(Shadow* sh, uint32_t size, bool reset, ULONG_PTR& n, ULONG& gran)
{
	n = sh->stageCap / 4096u;
	gran = 0;
	return GetWriteWatch(reset ? WRITE_WATCH_FLAG_RESET : 0, sh->stage, size, (PVOID*)sh->pages, &n, &gran) == 0 && gran;
}

static void OnMap(ID3D11DeviceContext* c, ID3D11Resource* r, D3D11_MAP type, D3D11_MAPPED_SUBRESOURCE* mp)
{
	MaybeMFault(MFF_MAP);
	void* const data = mp->pData;
	uint32_t need = 0;
	const uint32_t bytes = InterestCopyBytes(r, &need);   // the high-water part only (the buffer's size bounds it)
	if (!bytes) return;
	UfCtx* s = SlotFind(c);
	if (!s) return;
	KnowType(c, s);
	UfOpenMap* at = nullptr;
	for (UfOpenMap& m : s->om)
	{
		if (m.res == r) { at = &m; break; }
		if (!m.res && !at) at = &m;
	}
	if (!at) { InterlockedIncrement(&g_omFull); return; }
	if (at->res == r && at->real) InterlockedDecrement(&g_cachedOpen);   // mapped again without an Unmap: the older one is gone
	at->res = r; at->data = data; at->bytes = bytes; at->real = nullptr; at->size = 0; at->sh = nullptr; at->need = need;
	if (g_mapCache && type == D3D11_MAP_WRITE_DISCARD)
	{
		const uint32_t size = CacheableBytes(r);
		Shadow* sh = size ? ShadowMake(r, SlotIndex(s)) : nullptr;
		if (sh && StageEnsure(sh, size))
		{
			if (g_mapCache == 2)
			{
				ULONG_PTR n; ULONG gran;
				if (WrittenPages(sh, size, false, n, gran) && n)
				{
					if (InterlockedExchangeAdd(&g_latePages, (LONG)n) < 8)
						ULOG("model_filter: verify: %u page(s) of the staging of %p (context slot %d, %u bytes) written outside a map, the first at %u",
						     (unsigned)n, (void*)r, SlotIndex(s), size, (unsigned)((uintptr_t)sh->pages[0] - (uintptr_t)sh->stage));
				}
			}
			at->real = data; at->size = size; at->sh = sh; at->data = sh->stage;
			if (at->bytes > size) at->bytes = size;   // the interest's size may be an older, larger buffer's at this address
			InterlockedIncrement(&g_cachedOpen);
			InterlockedIncrement(&g_mapCached);
			mp->pData = sh->stage;   // last: a fault above leaves the EXE the driver's pointer
			return;
		}
	}
	InterlockedIncrement(&g_mapDirect);
	if (g_mapCache == 2)   // TEST ONLY: the driver's pointer of a direct map never lies in this buffer's staging
	{
		Shadow* sh = ShadowFind(r, SlotIndex(s));
		static volatile LONG logged;
		if (sh && sh->stage && (uint8_t*)data >= sh->stage && (uint8_t*)data < sh->stage + sh->stageCap && InterlockedIncrement(&logged) <= 8)
			ULOG("model_filter: verify: the direct map (type %d) of %p returned %p, inside its staging %p", (int)type, (void*)r, data, (void*)sh->stage);
	}
}

// ticket 0719: the written pages of a cached map go to the driver's pointer (before its Unmap)
static void Forward(UfOpenMap& m)
{
	const int64_t t0 = QpcNow();
	Shadow* sh = m.sh;
	uint8_t* const real = (uint8_t*)m.real;
	ULONG_PTR n; ULONG gran;
	uint64_t fwd = 0, written = 0;   // written: bytes of [0, m.bytes) in the pages written
	if (!WrittenPages(sh, m.size, true, n, gran))
	{
		InterlockedIncrement(&g_wwFail);   // not expected: the whole buffer, as the EXE may have written any of it
		memcpy(real, sh->stage, m.size);
		fwd = m.size;
		written = m.bytes;
		sh->lastPages = 0xFFFFFFFFu;   // every page forwarded: all count as written
	}
	else
	{
		for (ULONG_PTR i = 0; i < n;)
		{
			const uintptr_t first = (uintptr_t)sh->pages[i];
			ULONG_PTR j = i + 1;
			while (j < n && (uintptr_t)sh->pages[j] == (uintptr_t)sh->pages[j - 1] + gran) ++j;
			const uint32_t off = (uint32_t)(first - (uintptr_t)sh->stage);
			uint32_t end = off + (uint32_t)((j - i) * gran);
			if (end > m.size) end = m.size;
			if (off < end)
			{
				memcpy(real + off, sh->stage + off, end - off);
				fwd += end - off;
				if (off < m.bytes) written += (end < m.bytes ? end : m.bytes) - off;
				if (g_mapCache == 2)
				{
					if (memcmp(real + off, sh->stage + off, end - off)) InterlockedIncrement(&g_verifyBad);
					InterlockedExchangeAdd64(&g_verifyBytes, end - off);
				}
			}
			i = j;
		}
		sh->lastPages = (uint32_t)n;   // verify diagnostics (sh->pages holds them until the next forward)
	}
	InterlockedExchangeAdd64(&g_fwdBytes, (LONGLONG)fwd);
	InterlockedExchangeAdd64(&g_unwrittenBytes, (LONGLONG)(m.bytes - written));
	InterlockedExchangeAdd64(&g_fwdQpc, QpcNow() - t0);
}

// ticket 0758, model_tail_stream=verify (TEST ONLY): a tail run read again from the driver with memcpy (today's read before
// 0758) and compared with what the streaming read put into the shadow
static void TailVerify(const uint8_t* got, const uint8_t* driver, uint32_t n, const void* res, uint32_t at)
{
	uint8_t buf[4096];
	for (uint32_t o = 0; o < n; o += sizeof(buf))
	{
		const uint32_t k = n - o < (uint32_t)sizeof(buf) ? n - o : (uint32_t)sizeof(buf);
		memcpy(buf, driver + o, k);
		if (memcmp(buf, got + o, k))
		{
			uint32_t j = 0;
			while (j < k && buf[j] == got[o + j]) ++j;
			if (InterlockedIncrement(&g_tailDiff) <= 8)
				ULOG("model_filter: verify: tail read differs: res %p at %u (run %u+%u, %s): streaming %02X, memcpy %02X", res, at + o + j, at, n,
				     kTailReadNames[g_tailLevel], got[o + j], buf[j]);
			break;
		}
	}
	InterlockedExchangeAdd64(&g_tailVBytes, (LONGLONG)n);
}

static void OnUnmap(ID3D11DeviceContext* c, ID3D11Resource* r)
{
	UfCtx* s = SlotFind(c);
	if (!s) return;
	for (UfOpenMap& m : s->om)
	{
		if (m.res != r) continue;
		const int64_t t0 = QpcNow();
		const uint8_t* const driver = (const uint8_t*)(m.real ? m.real : m.data);   // verify only: the direct path's source
		if (m.real)
		{
			UfOpenMap cm = m;   // the entry is free whatever the forward does
			m.res = nullptr; m.real = nullptr;
			InterlockedDecrement(&g_cachedOpen);
			Forward(cm);
			if (ModelsOff()) { InterlockedExchangeAdd64(&g_copyQpc, QpcNow() - t0); return; }
		}
		else if (ModelsOff()) { m.res = nullptr; return; }   // reached only because another context has a cached map open
		Shadow* sh = ShadowMake(r, SlotIndex(s));
		if (sh)
		{
			if (sh->cap < m.bytes)
			{
				uint8_t* nd = (uint8_t*)realloc(sh->data, m.bytes);
				if (nd) { sh->data = nd; sh->cap = m.bytes; } else InterlockedIncrement(&g_shNomem);
			}
			if (sh->cap >= m.bytes)
			{
				memcpy(sh->data, m.data, m.bytes);
				// ticket 0719: a cached map's pages the EXE did not write, inside the part the draws have read so far ([0, need)),
				// come from the driver's memory as before (the EXE's draws read up to 512 bytes past what a map wrote, live
				// 2026-11-24 verify launch: the GPU then reads the driver's older bytes, and so must the record)
				if (m.data != driver && sh->lastPages != 0xFFFFFFFFu)
				{
					const uint32_t lim = m.need < m.bytes ? m.need : m.bytes;
					uint32_t pi = 0, runAt = 0, runEnd = 0;
					for (uint32_t a = 0; a < lim; a += 4096u)
					{
						while (pi < sh->lastPages && (uint32_t)((uintptr_t)sh->pages[pi] - (uintptr_t)sh->stage) < a) ++pi;
						const bool written = pi < sh->lastPages && (uint32_t)((uintptr_t)sh->pages[pi] - (uintptr_t)sh->stage) == a;
						const uint32_t e = a + 4096u < lim ? a + 4096u : lim;
						if (!written) { if (runEnd != a) runAt = a; runEnd = e; }
						if ((written || e == lim) && runEnd > runAt)
						{
							// ticket 0758: streaming loads where the CPU has them (tail_read.h), the same bytes as memcpy
							const int64_t q0 = QpcNow();
							TailRead(g_tailLevel, sh->data + runAt, driver + runAt, runEnd - runAt);
							InterlockedExchangeAdd64(&g_tailQpc, QpcNow() - q0);
							if (g_tailVerify) TailVerify(sh->data + runAt, driver + runAt, runEnd - runAt, r, runAt);
							InterlockedExchangeAdd64(&g_tailBytes, runEnd - runAt);
							runAt = runEnd = 0;
						}
					}
				}
				if (sh->bytes < m.bytes) sh->bytes = m.bytes;   // never shrinks (a reference checked at a draw stays in bounds)
				if (g_mapCache == 2)   // TEST ONLY: the direct path's copy of the same Unmap (its bytes as the driver now holds them)
				{
					sh->lastCached = m.data != driver; sh->lastMapBytes = m.bytes;
					if (!sh->lastCached) sh->lastPages = 0;   // (diagnostics only)
					if (sh->oldCap < sh->bytes)
					{
						uint8_t* no = (uint8_t*)realloc(sh->old, sh->bytes);
						if (no) { sh->old = no; sh->oldCap = sh->bytes; }
					}
					if (sh->old && sh->oldCap >= m.bytes)
					{
						memcpy(sh->old, driver, m.bytes);   // the part past m.bytes stays as earlier copies left it, as in data
						if (sh->oldBytes < m.bytes) sh->oldBytes = m.bytes;
					}
				}
				sh->epoch = s->epoch;
				InterlockedIncrement(&sh->version);
				InterlockedExchangeAdd64(&g_copyBytes, m.bytes);
			}
		}
		InterlockedExchangeAdd64(&g_copyQpc, QpcNow() - t0);
		m.res = nullptr;
		return;
	}
}

// ---- the records: one list per deferred context while it records, one per command list, the frame's ----
enum { MREF_COPY = 0, MREF_PALETTE = 1 };
struct MRef { uint32_t kind, row, srcOff, bytes; void* res; void* res2; uint32_t count, first; };   // filled at placement
struct MList
{
	VPModelTapDraw* d; uint32_t nd, capd;
	float (*r)[4]; uint32_t nr, capr;
	MRef* ref; uint32_t nref, capref;
	VPModelTapConst* k; uint64_t* kh; uint32_t nk, capk, capkh;
	VPModelTapTarget t[VPSHELL_MODELTAP_MAX_TARGETS]; uint32_t nt;
	uint32_t dropped[2], passedTapped, lost;
};

static bool Grow(void** p, uint32_t* cap, uint32_t need, size_t elem)
{
	if (need <= *cap) return true;
	uint32_t nc = *cap ? *cap : 64;
	while (nc < need) nc *= 2;
	void* q = realloc(*p, (size_t)nc * elem);
	if (!q) return false;
	*p = q; *cap = nc;
	return true;
}

static MList* ListNew() { return (MList*)calloc(1, sizeof(MList)); }
static void ListFree(MList* l)
{
	if (!l) return;
	free(l->d); free(l->r); free(l->ref); free(l->k); free(l->kh);
	free(l);
}
static void ListClear(MList* l) { l->nd = l->nr = l->nref = l->nk = l->nt = 0; l->dropped[0] = l->dropped[1] = l->passedTapped = l->lost = 0; }

static int ListTarget(MList* l, const uint32_t t6[6])
{
	for (uint32_t i = 0; i < l->nt; ++i)
		if (!memcmp(&l->t[i], t6, 24)) { l->t[i].draws++; return (int)i; }
	if (l->nt >= VPSHELL_MODELTAP_MAX_TARGETS) return -1;
	VPModelTapTarget& t = l->t[l->nt];
	memset(&t, 0, sizeof(t));
	memcpy(&t, t6, 24);
	t.draws = 1;
	return (int)l->nt++;
}

// the frame being assembled (render thread) and the command lists' records
static MList g_frame;
static MList* g_immList;                    // the immediate context's records since the last placement
struct ClRecs { ID3D11CommandList* cl; MList* list; uint32_t present; };
static const int kClRecs = 256;
static ClRecs g_clRecs[kClRecs];
static SRWLOCK g_clLock = SRWLOCK_INIT;
static volatile LONG g_clFull, g_clStale;
static volatile LONG g_placeMiss, g_budgetLeft, g_budgetFail;
static uint32_t g_presentNo;

// ---- ticket 0788: the hold of a draw whose key becomes droppable (research/05-graphics-composition/lategame-model-flash.md)
// Under exact lockstep the server draws frame P from the tap of Present P-1 (pan-coherence.md 5.11): it draws the records
// that tap holds as DROPPED. A draw that went from passed in frame P-1 (the EXE drew it; no record, or a passed one) to
// dropped in frame P was therefore drawn by nobody in frame P: one frame without those models, the late-game flash (a draw
// past its copy's high-water mark, a part hashed one frame late, a key joining the identity set, a latch bit set). The rule:
// a droppable draw whose mesh key was not droppable in the frame before is passed once more (the EXE draws it) and
// recorded with VPMTAP_DRAW_HELD; from the next frame on it is dropped, and the server draws the HELD record of the tap
// it pairs then. Keys, not instances: an instance entering the EXE's cull under a key already dropped is 0709's keep.
// The environment variable C5_SHELL_MODEL_HOLD=0 (diagnostic only, no ini key) restores the old rule (g_mHold).
struct HoldEntry { uint64_t h; uint32_t last, prev; };   // the two newest Presents (frame numbers) the key was droppable in
static const uint32_t kHoldCap = 16384, kHoldProbe = 32;
static HoldEntry g_hold[kHoldCap];
static SRWLOCK g_holdLock = SRWLOCK_INIT;
static volatile LONG g_holdFull;   // a key with no free entry in its probe range: dropped without a hold (the old rule)

// The key hash h is droppable in this frame (g_presentNo); true when it was droppable in the frame before as well.
static bool HoldSeenBefore(uint64_t h)
{
	const uint32_t now = g_presentNo;
	bool before = true;
	AcquireSRWLockExclusive(&g_holdLock);
	HoldEntry* hit = nullptr; HoldEntry* free_ = nullptr;
	for (uint32_t i = (uint32_t)h & (kHoldCap - 1), n = 0; n < kHoldProbe; ++n, i = (i + 1) & (kHoldCap - 1))
	{
		HoldEntry& e = g_hold[i];
		if (e.h == h) { hit = &e; break; }
		if (!free_ && (!e.h || now - e.last > 2)) free_ = &e;   // empty, or not droppable for two frames: reusable
		if (!e.h) break;
	}
	if (hit)
	{
		before = hit->last + 1 == now || (hit->last == now && hit->prev + 1 == now);
		if (hit->last != now) { hit->prev = hit->last; hit->last = now; }
	}
	else if (free_) { free_->h = h; free_->last = now; free_->prev = now - 3; before = false; }
	else InterlockedIncrement(&g_holdFull);
	ReleaseSRWLockExclusive(&g_holdLock);
	return before;
}

// ---- the identity set (the server's): key -> bits, class; swapped whole at a Present, old tables kept 8 Presents ----
struct SetEntry { VPModelKey key; uint8_t bits, cls, used, pad[5]; };
struct SetTable { SetEntry* e; uint32_t cap, count, generation, serverPid; };
static SetTable* volatile g_set;
static SetTable* g_retire[8];
static uint32_t g_retireAt[8];

static void SetFree(SetTable* t) { if (t) { free(t->e); free(t); } }
static void SetRetire(SetTable* t)
{
	if (!t) return;
	for (int i = 0; i < 8; ++i) if (!g_retire[i]) { g_retire[i] = t; g_retireAt[i] = g_presentNo; return; }
	// all 8 in use (8 changes within 8 Presents): the oldest is freed now; a draw that still reads it is 8 Presents old
	int o = 0;
	for (int i = 1; i < 8; ++i) if (g_retireAt[i] < g_retireAt[o]) o = i;
	SetFree(g_retire[o]); g_retire[o] = t; g_retireAt[o] = g_presentNo;
}

static const SetEntry* SetLookup(const VPModelKey& k)
{
	SetTable* t = g_set;
	if (!t || !t->cap) return nullptr;
	for (uint32_t i = (uint32_t)VPModelHash64(&k, sizeof(k)) & (t->cap - 1), n = 0; n < t->cap; ++n, i = (i + 1) & (t->cap - 1))
	{
		const SetEntry& e = t->e[i];
		if (!e.used) return nullptr;
		if (!memcmp(&e.key, &k, sizeof(k))) return &e;
	}
	return nullptr;
}

// ---- one family colour draw (a DROP pair bound, an indexed entry) ----
struct MGot   // the COM references a draw's gathering took (released once at the end)
{
	ID3D11RenderTargetView* rtv; ID3D11DepthStencilView* dsv; ID3D11Buffer* ib; ID3D11Buffer* vb[2];
	ID3D11ShaderResourceView* srv[5]; ID3D11Resource* tex[5]; ID3D11Buffer* cb[2]; ID3D11Buffer* pcb1;
	ID3D11ShaderResourceView* t27; ID3D11Resource* t27res;
};
static void GotRelease(MGot& g)
{
	IUnknown* all[] = { g.rtv, g.dsv, g.ib, g.vb[0], g.vb[1], g.srv[0], g.srv[1], g.srv[2], g.srv[3], g.srv[4], g.tex[0], g.tex[1], g.tex[2],
		g.tex[3], g.tex[4], g.cb[0], g.cb[1], g.pcb1, g.t27, g.t27res };
	for (IUnknown* u : all) if (u) u->Release();
}

static uint32_t BufBytes(ID3D11Buffer* b) { if (!b) return 0; D3D11_BUFFER_DESC d; b->GetDesc(&d); return d.ByteWidth; }

// The content of `res` bytes [0, end) as this draw sees it: the draw's own context's copy (a deferred context that mapped
// it since its last FinishCommandList) or the immediate context's. pending: a deferred draw reading the immediate
// context's copy, which is taken when the list is placed (its ExecuteCommandList). null: no copy covers it (yet).
static const Shadow* SourceSh(UfCtx* s, void* res, uint32_t end, bool& pending)
{
	pending = false;
	if (!res) return nullptr;
	if (s->deferred == 1)
	{
		Shadow* own = ShadowFind(res, SlotIndex(s));
		if (own && own->epoch == s->epoch && own->version && own->bytes >= end) return own;
	}
	Shadow* im = ShadowFind(res, g_immCi);
	if (!im || !im->version || im->bytes < end) return nullptr;
	pending = s->deferred == 1;
	return im;
}
static const uint8_t* Source(UfCtx* s, void* res, uint32_t end, bool& pending)
{
	const Shadow* sh = SourceSh(s, res, end, pending);
	return sh ? sh->data : nullptr;
}

static bool Reserve(LONG bytes)
{
	if (InterlockedExchangeAdd(&g_budgetLeft, -bytes) - bytes < 0) { InterlockedExchangeAdd(&g_budgetLeft, bytes); InterlockedIncrement(&g_budgetFail); return false; }
	return true;
}

// rows [row, row + bytes/16) of l: copied now (src) or a reference resolved at placement (pending)
static void PutRows(MList* l, uint32_t row, const uint8_t* src, bool pending, void* res, uint32_t srcOff, uint32_t bytes)
{
	if (!pending) { memcpy(l->r[row], src + srcOff, bytes); if (g_mapCache == 2) VerifyRead(src + srcOff, bytes); return; }
	MRef& f = l->ref[l->nref++];
	f.kind = MREF_COPY; f.row = row; f.srcOff = srcOff; f.bytes = bytes; f.res = res; f.res2 = nullptr; f.count = 0; f.first = 0;
}

// ===========================================================================
// Ticket 4879: the CPU-built meshes (walls, the Great Wall, the road and rail bridge; city-renderer.md 13). A family draw
// of the block or material-3 programs with an R32 index buffer is built by the EXE at run time: no model file holds its
// buffers. Both buffers (IMMUTABLE, hashed at creation) are copied once (a staging copy at a Present, mapped DO_NOT_WAIT
// at a later one, checked against the creation hash) into the geometry section Local\VPShellModelGeom.<pid>
// (shell_model_tap.h), where the server takes them. Such a draw is dropped only while both buffers are entries of the
// section (and, as every model draw, its key is in the server's identity set); else it passes (cpu_nogeom). An entry is
// freed when no draw named it for kGeomIdlePresents, or, when the section is full, the least recently named first
// (after kGeomMinAge Presents unnamed). Never a stall; bounded per Present.
// ===========================================================================
enum { GS_EMPTY = 0, GS_WANTED, GS_COPIED, GS_HELD, GS_REFUSED, GS_EVICTED };
enum { GR_NONE = 0, GR_OVERSIZE, GR_CHANGED, GR_FULL, GR_FAILED, GR_NOTKEYABLE };
struct GeomRec
{
	volatile uint64_t hash; uint32_t kind; volatile LONG state;
	ID3D11Resource* res; ID3D11Resource* staging;
	uint32_t bytes, stride, present, entry, offset, why, retryAt;
	volatile uint32_t lastNamed;
	uint32_t fullCount;                         // ticket 0597: how often this buffer was refused `full` (its first is logged)
};
static const unsigned kGeoms = 4096;
static const uint32_t kGeomIdlePresents = 18000, kGeomMinAge = 120, kGeomRetry = 600;
static GeomRec g_geom[kGeoms];
static SRWLOCK g_geomLock = SRWLOCK_INIT;
static volatile LONG g_gUsed, g_gTableFull, g_gWanted, g_gHeld, g_gOversize, g_gChanged, g_gFull, g_gFailed, g_gEvicted, g_gNotKeyable,
	g_gMapFail, g_gPending;
// ticket 0597: the logged refusals (table_full once per episode; full once per buffer, plus a line when such a buffer is
// held at last), the records reclaimed, the most entries and data bytes the section has held
static volatile LONG g_gTableFullLogged, g_gEvictedRoom, g_gFullLogged, g_gMaxEntries, g_gUsedBytes, g_gMaxBytes;
static volatile LONG g_gCompactions;          // ticket 0597: compactions of the data (GeomCompact)
static volatile LONGLONG g_gCompactBytes;
static volatile LONGLONG g_gBytes, g_gQpc;
static HANDLE g_geomMap;
static VPModelGeomHeader* g_geomHdr;           // header + entries, mapped for the process's life (64 KB)
static bool g_geomOff;                         // the section could not be made: CPU-built draws always pass

static inline unsigned GeomSlot(uint64_t h, uint32_t kind) { return (unsigned)((h ^ (h >> 29) ^ kind) * 2654435761u) & (kGeoms - 1); }

// draw threads: the record of (kind, hash), made WANTED (the buffer AddRef'd) at first sight; true when held in the section
static bool GeomNote(ID3D11Resource* r, uint32_t kind, uint64_t hash, uint32_t stride)
{
	if (!r || !hash || g_geomOff) return false;
	const uint32_t now = g_presentNo;
	AcquireSRWLockShared(&g_geomLock);
	unsigned i = GeomSlot(hash, kind);
	GeomRec* hit = nullptr;
	for (unsigned k = 0; k < kGeoms; ++k, i = (i + 1) & (kGeoms - 1))
	{
		GeomRec& g = g_geom[i];
		if (g.state == GS_EMPTY) break;
		if (g.hash == hash && g.kind == kind) { hit = &g; break; }
	}
	if (hit)
	{
		hit->lastNamed = now;
		const LONG st = hit->state;
		const bool again = st == GS_EVICTED || (st == GS_REFUSED && hit->why == GR_FULL && (int32_t)(now - hit->retryAt) >= 0);
		ReleaseSRWLockShared(&g_geomLock);
		if (!again) return st == GS_HELD;
		AcquireSRWLockExclusive(&g_geomLock);
		if (hit->state == GS_EVICTED || (hit->state == GS_REFUSED && hit->why == GR_FULL))
		{
			r->AddRef(); hit->res = r; hit->stride = stride; hit->why = GR_NONE;
			InterlockedExchange(&hit->state, GS_WANTED); InterlockedIncrement(&g_gWanted); InterlockedIncrement(&g_gPending);
		}
		ReleaseSRWLockExclusive(&g_geomLock);
		return false;
	}
	ReleaseSRWLockShared(&g_geomLock);
	AcquireSRWLockExclusive(&g_geomLock);
	i = GeomSlot(hash, kind);
	for (unsigned k = 0; k < kGeoms; ++k, i = (i + 1) & (kGeoms - 1))
	{
		GeomRec& g = g_geom[i];
		if (g.state != GS_EMPTY) { if (g.hash == hash && g.kind == kind) break; continue; }   // another thread was first
		if ((unsigned)g_gUsed >= kGeoms / 4 * 3)
		{
			// ticket 0597: logged once (the draw passes: the EXE draws this mesh; the reclaim at the next scan makes room)
			InterlockedIncrement(&g_gTableFull);
			if (!InterlockedExchange(&g_gTableFullLogged, 1))
				ULOG("model_filter: REFUSED table_full: %u of %u geometry records in use; a new CPU-built buffer (kind %u, hash "
				     "%016llx) is not copied and the EXE draws its mesh (not lost); records of freed or refused buffers are "
				     "reclaimed at the next scan", (unsigned)g_gUsed, kGeoms, kind, (unsigned long long)hash);
			break;
		}
		r->AddRef();
		g.res = r; g.staging = nullptr; g.kind = kind; g.stride = stride; g.why = GR_NONE; g.lastNamed = now; g.entry = 0xFFFFFFFFu;
		g.fullCount = 0;
		g.hash = hash;
		MemoryBarrier();
		InterlockedExchange(&g.state, GS_WANTED);
		InterlockedIncrement(&g_gUsed); InterlockedIncrement(&g_gWanted); InterlockedIncrement(&g_gPending);
		break;
	}
	ReleaseSRWLockExclusive(&g_geomLock);
	return false;
}

static bool GeomEnsureSection()
{
	if (g_geomHdr || g_geomOff) return g_geomHdr != nullptr;
	wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELGEOM_FMT, GetCurrentProcessId());
	const uint64_t total = (uint64_t)VPSHELL_MODELGEOM_DATA_OFFSET + g_geomDataBytes;
	g_geomMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)(total >> 32), (DWORD)total, n);
	if (g_geomMap) g_geomHdr = (VPModelGeomHeader*)MapViewOfFile(g_geomMap, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_MODELGEOM_DATA_OFFSET);
	if (!g_geomHdr)
	{
		ULOG("model_filter: LOUD: the geometry section %ls cannot be made (%u): the CPU-built meshes (walls, the Great Wall, bridges) stay "
		     "the EXE's", n, GetLastError());
		if (g_geomMap) { CloseHandle(g_geomMap); g_geomMap = nullptr; }
		g_geomOff = true;
		return false;
	}
	VPModelGeomHeader* h = g_geomHdr;
	InterlockedIncrement((volatile LONG*)&h->seq);
	MemoryBarrier();
	h->magic = VPSHELL_MODELGEOM_MAGIC; h->version = VPSHELL_MODELGEOM_VERSION; h->headerBytes = sizeof(VPModelGeomHeader);
	h->entryBytes = sizeof(VPModelGeomEntry); h->maxEntries = VPSHELL_MODELGEOM_MAX_ENTRIES; h->dataOffset = VPSHELL_MODELGEOM_DATA_OFFSET;
	h->dataBytes = g_geomDataBytes; h->exePid = GetCurrentProcessId(); h->generation = 1;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&h->seq);
	ULOG("model_filter: geometry section %ls (%u entries, %u KB of data, %u B; %s) for the CPU-built meshes", n, VPSHELL_MODELGEOM_MAX_ENTRIES,
	     g_geomDataBytes / 1024, g_geomDataBytes, g_geomSizeNote);
	return true;
}

static inline VPModelGeomEntry* GeomEntries() { return (VPModelGeomEntry*)((uint8_t*)g_geomHdr + sizeof(VPModelGeomHeader)); }

// first fit over the valid entries' ranges (sorted by offset); 0xFFFFFFFF = no room
static uint32_t GeomFit(uint32_t bytes, uint32_t* slot)
{
	if (g_geomTestFull) return 0xFFFFFFFFu;
	VPModelGeomEntry* e = GeomEntries();
	uint32_t used[VPSHELL_MODELGEOM_MAX_ENTRIES][2]; uint32_t n = 0; *slot = 0xFFFFFFFFu;
	for (uint32_t i = 0; i < VPSHELL_MODELGEOM_MAX_ENTRIES; ++i)
	{
		if (e[i].kind == VPMGEOM_KIND_FREE) { if (*slot == 0xFFFFFFFFu) *slot = i; continue; }
		uint32_t j = n++;
		while (j > 0 && used[j - 1][0] > e[i].offset) { used[j][0] = used[j - 1][0]; used[j][1] = used[j - 1][1]; --j; }
		used[j][0] = e[i].offset; used[j][1] = e[i].offset + ((e[i].bytes + 15u) & ~15u);
	}
	if (*slot == 0xFFFFFFFFu) return 0xFFFFFFFFu;
	const uint32_t need = (bytes + 15u) & ~15u;
	uint32_t at = 0;
	for (uint32_t k = 0; k < n; ++k) { if (used[k][0] >= at + need) return at; if (used[k][1] > at) at = used[k][1]; }
	return (uint64_t)at + need <= g_geomDataBytes ? at : 0xFFFFFFFFu;
}

static void GeomFreeEntry(GeomRec& g, bool count)
{
	if (g.entry < VPSHELL_MODELGEOM_MAX_ENTRIES)
	{
		VPModelGeomHeader* h = g_geomHdr;
		InterlockedIncrement((volatile LONG*)&h->seq);
		MemoryBarrier();
		GeomEntries()[g.entry].kind = VPMGEOM_KIND_FREE;
		h->generation++; if (h->entriesUsed) h->entriesUsed--;
		MemoryBarrier();
		InterlockedIncrement((volatile LONG*)&h->seq);
		InterlockedExchangeAdd(&g_gUsedBytes, -(LONG)((g.bytes + 15u) & ~15u));
	}
	g.entry = 0xFFFFFFFFu;
	InterlockedExchange(&g.state, GS_EVICTED);
	if (count) InterlockedIncrement(&g_gEvicted);
	InterlockedDecrement(&g_gHeld);
}

// the least recently named held record not named for kGeomMinAge Presents; false when none
static bool GeomEvictOne(uint32_t now)
{
	GeomRec* best = nullptr;
	for (unsigned i = 0; i < kGeoms; ++i)
		if (g_geom[i].state == GS_HELD && now - g_geom[i].lastNamed > kGeomMinAge && (!best || (int32_t)(g_geom[i].lastNamed - best->lastNamed) < 0)) best = &g_geom[i];
	if (!best) return false;
	GeomFreeEntry(*best, true);
	InterlockedIncrement(&g_gEvictedRoom);   // ticket 0597: freed to make room (not by the idle rule)
	return true;
}

// ticket 0597: the data moved down to one block from offset 0 (the live Tiny run of 2026-11-20 with a 1,024 KB section:
// 252,544 B free in two holes of 108,160 and 144,384 B, a 226,380-B buffer refused for good). Within one seq-odd window:
// each valid entry, in offset order, has its bytes moved to the lowest free offset (memmove through a short view; the
// destination is always below the source) and then its offset changed; its generation stays, so the server, which keys
// its copies by kind and hash, reads nothing again (a read in progress sees the seq move and reads again later). A view
// that cannot be made ends the pass; the entry whose move failed is freed (its draw passes to the EXE). Returns the
// bytes moved; *ok false when an entry had to be freed.
static uint8_t* GeomView(uint64_t off, uint64_t bytes, uint8_t** at)   // a view of the data's [off, off + bytes)
{
	const DWORD gran = 65536u;
	const uint64_t abs = (uint64_t)VPSHELL_MODELGEOM_DATA_OFFSET + off, base = abs / gran * gran;
	uint8_t* v = (uint8_t*)MapViewOfFile(g_geomMap, FILE_MAP_ALL_ACCESS, (DWORD)(base >> 32), (DWORD)base, (SIZE_T)(abs - base + bytes));
	*at = v ? v + (abs - base) : nullptr;
	return v;
}
// [from, from + bytes) -> [to, ...), to < from: one view and memmove when the gap is under 1 MB (at most 9 MB mapped),
// else 1-MB pieces through two views each (a piece's source and destination do not overlap); false when a view fails
// (the destination may then hold part of the bytes and the source may be overwritten: the caller frees the entry)
static bool GeomMoveDown(uint32_t from, uint32_t to, uint32_t bytes)
{
	const uint32_t kPiece = 1u << 20;
	if (from - to < kPiece)
	{
		uint8_t* p = nullptr;
		uint8_t* v = GeomView(to, (uint64_t)(from - to) + bytes, &p);
		if (!v) return false;
		memmove(p, p + (from - to), bytes);
		UnmapViewOfFile(v);
		return true;
	}
	for (uint32_t done = 0; done < bytes; done += kPiece)
	{
		const uint32_t n = bytes - done < kPiece ? bytes - done : kPiece;
		uint8_t *ps = nullptr, *pd = nullptr;
		uint8_t* vs = GeomView(from + done, n, &ps);
		uint8_t* vd = vs ? GeomView(to + done, n, &pd) : nullptr;
		if (vs && vd) memcpy(pd, ps, n);
		if (vd) UnmapViewOfFile(vd);
		if (vs) UnmapViewOfFile(vs);
		if (!vs || !vd) return false;
	}
	return true;
}

// need: the aligned size wanted; the pass stops at the first hole that holds it (live r4, 2026-11-20: whole passes moved
// 2.1-3.5 MB of a 4 MB section in 2.9-8.2 ms on the render thread, several in one Present)
static uint64_t GeomCompact(uint32_t need, bool* ok)
{
	*ok = true;
	VPModelGeomEntry* e = GeomEntries();
	GeomRec* recOf[VPSHELL_MODELGEOM_MAX_ENTRIES] = {};
	for (unsigned i = 0; i < kGeoms; ++i)
		if (g_geom[i].state == GS_HELD && g_geom[i].entry < VPSHELL_MODELGEOM_MAX_ENTRIES) recOf[g_geom[i].entry] = &g_geom[i];
	uint32_t order[VPSHELL_MODELGEOM_MAX_ENTRIES]; uint32_t n = 0;
	for (uint32_t i = 0; i < VPSHELL_MODELGEOM_MAX_ENTRIES; ++i)
	{
		if (e[i].kind == VPMGEOM_KIND_FREE) continue;
		uint32_t j = n++;
		while (j > 0 && e[order[j - 1]].offset > e[i].offset) { order[j] = order[j - 1]; --j; }
		order[j] = i;
	}
	VPModelGeomHeader* h = g_geomHdr;
	InterlockedIncrement((volatile LONG*)&h->seq);
	MemoryBarrier();
	uint64_t moved = 0;
	uint32_t at = 0;
	for (uint32_t k = 0; k < n; ++k)
	{
		VPModelGeomEntry& x = e[order[k]];
		const uint32_t sz = (x.bytes + 15u) & ~15u;
		if (x.offset - at >= need) break;   // the hole below this entry holds the buffer: enough moved
		if (x.offset > at)
		{
			if (!GeomMoveDown(x.offset, at, x.bytes))
			{
				// its bytes may be damaged: the entry goes (inside this window, so no reader sees it half moved)
				InterlockedIncrement(&g_gMapFail);
				x.kind = VPMGEOM_KIND_FREE;
				if (h->entriesUsed) h->entriesUsed--;
				InterlockedExchangeAdd(&g_gUsedBytes, -(LONG)sz);
				if (GeomRec* r = recOf[order[k]])
				{
					r->entry = 0xFFFFFFFFu;
					InterlockedExchange(&r->state, GS_EVICTED);
					InterlockedIncrement(&g_gEvicted);
					InterlockedDecrement(&g_gHeld);
				}
				*ok = false;
				break;
			}
			x.offset = at;
			if (recOf[order[k]]) recOf[order[k]]->offset = at;
			moved += x.bytes;
		}
		at += sz;
	}
	h->generation++;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&h->seq);
	return moved;
}

static void GeomRefuse(GeomRec& g, uint32_t why, uint32_t now)
{
	// ticket 0597 (D118/D125: a logged refusal, not a silent loss): the first `full` of each buffer is one line with the
	// section's state; the draw passes, so the EXE draws the mesh; tried again every kGeomRetry Presents
	if (why == GR_FULL && g.fullCount++ == 0)
	{
		InterlockedIncrement(&g_gFullLogged);
		const VPModelGeomHeader* h = g_geomHdr;
		ULOG("model_filter: REFUSED full: a CPU-built %s buffer of %u B (hash %016llx) does not fit the geometry section: %u of %u "
		     "entries, %ld of %u B in use, no entry unnamed for %u Presents to evict (every held mesh drawn by the EXE lately: "
		     "in view, or on a small map anywhere); the EXE draws this mesh (not lost); tried again every %u Presents; model_geom_kb sets the section (%s)",
		     g.kind == VPMGEOM_KIND_INDEX32 ? "index" : "vertex", g.bytes, (unsigned long long)g.hash, h ? h->entriesUsed : 0u,
		     VPSHELL_MODELGEOM_MAX_ENTRIES, (long)g_gUsedBytes, g_geomDataBytes, kGeomMinAge, kGeomRetry, g_geomSizeNote);
	}
	if (g.staging) { g.staging->Release(); g.staging = nullptr; }
	if (g.res) { g.res->Release(); g.res = nullptr; }
	g.why = why; g.retryAt = now + kGeomRetry;
	InterlockedExchange(&g.state, GS_REFUSED);
	InterlockedDecrement(&g_gPending);
	InterlockedIncrement(why == GR_OVERSIZE ? &g_gOversize : why == GR_CHANGED ? &g_gChanged : why == GR_FULL ? &g_gFull :
	                     why == GR_NOTKEYABLE ? &g_gNotKeyable : &g_gFailed);
	if (g_geomHdr) g_geomHdr->refused++;
}

// render thread, at a Present: start up to 8 copies (16 MB), finish the earlier ones, free idle entries
static void GeomPresent(ID3D11Device* dev, uint32_t now)
{
	if (g_geomOff || !dev) return;
	if (!g_gPending && !g_gHeld) return;
	// with nothing pending only the idle rule has work: the table is scanned once every kGeomRetry Presents (the live
	// smoke of 2026-11-19 measured the scan of every Present at about 0.04 ms while entries were held)
	if (!g_gPending && now % kGeomRetry) return;
	if (!GeomEnsureSection()) return;
	const int64_t q0 = QpcNow();
	ID3D11DeviceContext* ctx = nullptr;
	dev->GetImmediateContext(&ctx);
	if (!ctx) return;
	uint32_t started = 0; uint64_t bytes = 0;
	const uint32_t maxBytes = g_geomTestMaxBytes ? g_geomTestMaxBytes : VPSHELL_MODELGEOM_MAX_BUFFER;
	AcquireSRWLockExclusive(&g_geomLock);
	for (unsigned i = 0; i < kGeoms; ++i)
	{
		GeomRec& g = g_geom[i];
		const LONG st = g.state;
		if (st == GS_HELD && now - g.lastNamed > kGeomIdlePresents) { GeomFreeEntry(g, true); continue; }
		if (st == GS_WANTED && started < 8 && bytes < (16u << 20))
		{
			ID3D11Buffer* buf = nullptr; D3D11_BUFFER_DESC d = {};
			if (SUCCEEDED(g.res->QueryInterface(__uuidof(ID3D11Buffer), (void**)&buf)) && buf) { buf->GetDesc(&d); buf->Release(); }
			if (!BufferKeyable(d)) { GeomRefuse(g, GR_NOTKEYABLE, now); continue; }
			if (d.ByteWidth > maxBytes || d.ByteWidth > g_geomDataBytes) { GeomRefuse(g, GR_OVERSIZE, now); continue; }
			D3D11_BUFFER_DESC sd = {}; sd.ByteWidth = d.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ID3D11Buffer* stg = nullptr;
			if (FAILED(dev->CreateBuffer(&sd, nullptr, &stg)) || !stg) { GeomRefuse(g, GR_FAILED, now); continue; }
			ctx->CopyResource(stg, g.res);
			g.staging = stg; g.bytes = d.ByteWidth; g.present = now;
			InterlockedExchange(&g.state, GS_COPIED);
			started++; bytes += d.ByteWidth;
			continue;
		}
		if (st == GS_COPIED && now != g.present)
		{
			D3D11_MAPPED_SUBRESOURCE m;
			const HRESULT hr = ctx->Map(g.staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
			if (hr == DXGI_ERROR_WAS_STILL_DRAWING) continue;
			if (FAILED(hr)) { GeomRefuse(g, GR_FAILED, now); continue; }
			// the bytes must be the ones hashed at creation (an IMMUTABLE buffer cannot change; a mismatch = not the content
			// the key names, e.g. a write the compositor did not see): refused, the draw passes
			if ((VPModelGeomHash(g.kind, m.pData, g.bytes) ^ (g_geomTestChanged ? 1u : 0u)) != g.hash) { ctx->Unmap(g.staging, 0); GeomRefuse(g, GR_CHANGED, now); continue; }
			uint32_t slot = 0, off = GeomFit(g.bytes, &slot);
			// ticket 0597: no hole fits: when the free bytes and an entry suffice the data is compacted (once), else the
			// least recently named entry unnamed for kGeomMinAge Presents is freed, and so on
			bool compacted = false;
			while (off == 0xFFFFFFFFu && !g_geomTestFull)
			{
				const uint32_t need = (g.bytes + 15u) & ~15u;
				if (!compacted && g_geomHdr->entriesUsed < VPSHELL_MODELGEOM_MAX_ENTRIES && g_geomDataBytes - (uint32_t)g_gUsedBytes >= need)
				{
					compacted = true;
					const uint32_t freeB = g_geomDataBytes - (uint32_t)g_gUsedBytes;
					const int64_t c0 = QpcNow();
					bool ok = true;
					const uint64_t moved = GeomCompact(need, &ok);
					const double ms = (double)(QpcNow() - c0) * 1000.0 / (double)(g_freq ? g_freq : 1);
					InterlockedIncrement(&g_gCompactions);
					InterlockedExchangeAdd64(&g_gCompactBytes, (LONGLONG)moved);
					ULOG("model_filter: geometry section compacted for a CPU-built buffer of %u B: %u B were free but in no hole that "
					     "fits; %llu B moved in %.2f ms%s", g.bytes, freeB, (unsigned long long)moved, ms,
					     ok ? "" : " (a view failed: the entry being moved was freed, its draw passes to the EXE)");
					off = GeomFit(g.bytes, &slot);
					continue;
				}
				if (!GeomEvictOne(now)) break;
				off = GeomFit(g.bytes, &slot);
			}
			if (off == 0xFFFFFFFFu) { ctx->Unmap(g.staging, 0); GeomRefuse(g, GR_FULL, now); continue; }
			// the data through a short view (no address space kept in the 32-bit process), then the entry
			const DWORD gran = 65536u;
			const uint64_t abs = (uint64_t)VPSHELL_MODELGEOM_DATA_OFFSET + off, base = abs / gran * gran;
			uint8_t* v = (uint8_t*)MapViewOfFile(g_geomMap, FILE_MAP_ALL_ACCESS, (DWORD)(base >> 32), (DWORD)base, (SIZE_T)(abs - base + g.bytes));
			if (!v) { ctx->Unmap(g.staging, 0); InterlockedIncrement(&g_gMapFail); continue; }   // tried again at the next Present
			VPModelGeomHeader* h = g_geomHdr;
			InterlockedIncrement((volatile LONG*)&h->seq);
			MemoryBarrier();
			memcpy(v + (abs - base), m.pData, g.bytes);
			VPModelGeomEntry& e = GeomEntries()[slot];
			e.hash = g.hash; e.stride = g.kind == VPMGEOM_KIND_INDEX32 ? 4u : g.stride; e.offset = off; e.bytes = g.bytes;
			h->generation++; e.generation = h->generation; e.reserved = 0;
			MemoryBarrier();
			e.kind = g.kind;
			h->entriesUsed++;
			MemoryBarrier();
			InterlockedIncrement((volatile LONG*)&h->seq);
			UnmapViewOfFile(v);
			ctx->Unmap(g.staging, 0);
			g.staging->Release(); g.staging = nullptr;
			g.res->Release(); g.res = nullptr;
			g.entry = slot; g.offset = off;
			InterlockedExchange(&g.state, GS_HELD);
			InterlockedIncrement(&g_gHeld); InterlockedDecrement(&g_gPending);
			InterlockedExchangeAdd64(&g_gBytes, g.bytes);
			// ticket 0597: the section's high-water marks; a buffer refused `full` before is logged when it fits at last
			const LONG ub = InterlockedExchangeAdd(&g_gUsedBytes, (LONG)((g.bytes + 15u) & ~15u)) + (LONG)((g.bytes + 15u) & ~15u);
			if (ub > g_gMaxBytes) g_gMaxBytes = ub;
			if ((LONG)h->entriesUsed > g_gMaxEntries) g_gMaxEntries = (LONG)h->entriesUsed;
			if (g.fullCount)
				ULOG("model_filter: geometry section: the CPU-built %s buffer %016llx refused full %u time(s) is held now (%u of %u "
				     "entries, %ld of %u B in use)", g.kind == VPMGEOM_KIND_INDEX32 ? "index" : "vertex", (unsigned long long)g.hash,
				     g.fullCount, h->entriesUsed, VPSHELL_MODELGEOM_MAX_ENTRIES, (long)ub, g_geomDataBytes);
			g.fullCount = 0;
		}
	}
	ReleaseSRWLockExclusive(&g_geomLock);
	ctx->Release();
	InterlockedExchangeAdd64(&g_gQpc, QpcNow() - q0);
}

static bool ModelDraw(ID3D11DeviceContext* c, UfCtx* s, const UfDrawArgs& a, uint8_t cls)
{
	const LONG dropBits = g_mDropBits, tapBits = g_mTapBits, tapAll = g_mTapAll;
	if (!dropBits && !tapBits && !tapAll) { s->mc[MC_IDLE]++; return false; }
	if (!a.indexed) { s->mc[MC_ENTRY]++; return false; }
	const int64_t t0 = QpcNow();
	KnowType(c, s);
	MGot g; memset(&g, 0, sizeof(g));
	bool drop = false, held = false;   // held: ticket 0788
	do
	{
		// 1. the target: only a draw into a target with a depth view (the scene, the reflection; the mask pass has none)
		c->OMGetRenderTargets(1, &g.rtv, &g.dsv);
		if (!g.dsv) { s->mc[MC_NODSV]++; break; }
		if (s->lastRtv != (void*)g.rtv || s->lastDsv != (void*)g.dsv || !s->lastTargetOk)
		{
			memset(s->lastTarget, 0, sizeof(s->lastTarget));
			ID3D11Resource* rr = nullptr; ID3D11Texture2D* t2 = nullptr; D3D11_TEXTURE2D_DESC td;
			if (g.rtv) { g.rtv->GetResource(&rr); if (rr && SUCCEEDED(rr->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2)) && t2)
				{ t2->GetDesc(&td); s->lastTarget[0] = td.Width; s->lastTarget[1] = td.Height; s->lastTarget[2] = td.SampleDesc.Count; s->lastTarget[3] = td.Format; t2->Release(); t2 = nullptr; } }
			if (rr) { rr->Release(); rr = nullptr; }
			g.dsv->GetResource(&rr);
			if (rr && SUCCEEDED(rr->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2)) && t2) { t2->GetDesc(&td); s->lastTarget[4] = td.Width; s->lastTarget[5] = td.Height; t2->Release(); }
			if (rr) rr->Release();
			s->lastRtv = g.rtv; s->lastDsv = g.dsv; s->lastTargetOk = 1;
		}
		// 2. the key (4870's rule): IB from index 1, VB slot 0, the range, t0 / t1
		DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0, strides[2] = {}, offs[2] = {};
		c->IAGetIndexBuffer(&g.ib, &fmt, &ibOff);
		c->IAGetVertexBuffers(0, 2, g.vb, strides, offs);
		c->PSGetShaderResources(0, 5, g.srv);
		VPModelKey key; memset(&key, 0, sizeof(key));
		bool unhashed = false;
		uint32_t kind; uint64_t h0, h16, h32;
		const bool i32 = fmt == DXGI_FORMAT_R32_UINT;
		const uint32_t isz = i32 ? 4u : 2u;
		if (!g.ib || (fmt != DXGI_FORMAT_R16_UINT && !i32) || ibOff % isz) { unhashed = true; s->mc[MC_UH_IB_OTHER]++; }
		else if (!ResGet(g.ib, kind, h0, h16, h32)) { unhashed = true; s->mc[MC_UH_IB_MISS]++; ReadBackRequest(g.ib, false); }
		else if (!(kind & RK_IB)) { unhashed = true; s->mc[MC_UH_IB_OTHER]++; }
		else { key.ib = i32 ? h32 : h16; key.flags = i32 ? VPMKEY_INDEX32 : 0u; }
		if (!g.vb[0] || !strides[0] || offs[0] % strides[0]) { unhashed = true; s->mc[MC_UH_VB_OTHER]++; }
		else if (!ResGet(g.vb[0], kind, h0, h16, h32)) { unhashed = true; s->mc[MC_UH_VB_MISS]++; ReadBackRequest(g.vb[0], false); }
		else if (!(kind & RK_VB)) { unhashed = true; s->mc[MC_UH_VB_OTHER]++; }
		else key.vb = h0;
		key.startIndex = a.startIndex + ibOff / isz;
		key.indexCount = a.indexCount;
		key.baseVertex = a.baseVertex + (strides[0] ? (int32_t)(offs[0] / strides[0]) : 0);
		uint64_t tid[5] = {};
		for (int t = 0; t < 5; ++t)
		{
			if (!g.srv[t]) continue;
			g.srv[t]->GetResource(&g.tex[t]);
			const bool got = g.tex[t] && ResGet(g.tex[t], kind, h0, h16, h32);
			if (got && (kind & RK_TEX)) tid[t] = h0;
			else
			{
				if (!got && g.tex[t]) ReadBackRequest(g.tex[t], true);   // t2-t4 too: their identities go into the record
				if (t < 2) { unhashed = true; s->mc[got ? MC_UH_TEX_NOID : MC_UH_TEX_MISS]++; }
			}
		}
		key.t0 = tid[0]; key.t1 = tid[1];
		static volatile LONG s_uhLogged;
		if (unhashed && InterlockedIncrement(&s_uhLogged) <= 3)
		{
			// the first three incomplete keys of the process, named part by part (live diagnosis, 2026-11-18)
			char tx[2][120] = {};
			for (int t = 0; t < 2; ++t)
			{
				ID3D11Texture2D* t2 = nullptr; D3D11_TEXTURE2D_DESC td = {};
				if (g.tex[t] && SUCCEEDED(g.tex[t]->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2)) && t2) { t2->GetDesc(&td); t2->Release(); }
				uint32_t k2 = 0; uint64_t a0, a1, a2;
				const bool in = g.tex[t] && ResGet(g.tex[t], k2, a0, a1, a2);
				_snprintf_s(tx[t], _TRUNCATE, "t%d %p %s kind %u fmt %u usage %u bind 0x%X %ux%u mips %u", t, (void*)g.tex[t], in ? "in" : "miss", k2,
				            (unsigned)td.Format, (unsigned)td.Usage, td.BindFlags, td.Width, td.Height, td.MipLevels);
			}
			uint32_t ki = 0, kv = 0; uint64_t a0, a1, a2;
			const bool ii = g.ib && ResGet(g.ib, ki, a0, a1, a2), vi = g.vb[0] && ResGet(g.vb[0], kv, a0, a1, a2);
			D3D11_BUFFER_DESC ibd = {}, vbd = {};
			if (g.ib) g.ib->GetDesc(&ibd);
			if (g.vb[0]) g.vb[0]->GetDesc(&vbd);
			ULOG("model_filter: incomplete key (class %u, %s context): IB %p %s kind %u fmt %u off %u usage %u %u B; VB %p %s kind %u stride %u off %u usage %u %u B; "
			     "%s; %s", cls, s->deferred == 1 ? "deferred" : "immediate", (void*)g.ib, ii ? "in" : "miss", ki, (unsigned)fmt, ibOff, (unsigned)ibd.Usage, ibd.ByteWidth,
			     (void*)g.vb[0], vi ? "in" : "miss", kv, strides[0], offs[0], (unsigned)vbd.Usage, vbd.ByteWidth, tx[0], tx[1]);
		}
		// 3. the identity set and the class's bit
		const SetEntry* se = unhashed ? nullptr : SetLookup(key);
		uint8_t setBits = se && (se->cls == VPMODEL_CLASS_NONE || se->cls == cls) ? se->bits : 0;
		// model_filter_set=all (TEST ONLY, the live P5 check without a server): every fully hashed key counts as in the set,
		// under its class's bit (skinned: landmarks)
		if (g_mSetAll && !unhashed) setBits = g_mPairBit[cls] ? g_mPairBit[cls] : (uint8_t)VPMODE_LANDMARKS;
		uint8_t bit = g_mPairBit[cls];
		if (!bit) bit = (setBits == VPMODE_CITIES || setBits == VPMODE_LANDMARKS) ? setBits : 0;   // skinned: exactly one bit
		const bool inSet = bit && (setBits & bit);
		const bool latched = bit && (dropBits & bit);
		const LONG anyBit = bit ? bit : (VPMODE_CITIES | VPMODE_LANDMARKS);
		const bool wantTap = latched || (tapBits & anyBit) || tapAll;
		const int bi = bit == VPMODE_LANDMARKS ? 1 : 0;
		bool wantDrop = latched && inSet;
		// ticket 4879: a CPU-built mesh (R32 on the block or material-3 programs: no model file holds it) is copied into the
		// geometry section while the server wants cities; it is dropped only while both its buffers are held there
		if (!unhashed && i32 && (cls == VPMODEL_CLASS_BLOCK || cls == VPMODEL_CLASS_SINGLE) && ((tapBits | dropBits) & VPMODE_CITIES))
		{
			s->mc[MC_CPU_SEEN]++;
			const bool hi = GeomNote(g.ib, VPMGEOM_KIND_INDEX32, key.ib, 4);
			const bool hv = GeomNote(g.vb[0], VPMGEOM_KIND_VERTEX, key.vb, strides[0]);
			if (!(hi && hv)) { if (wantDrop) s->mc[MC_CPU_NOGEOM]++; wantDrop = false; }
		}
		if (latched && !inSet) s->mc[unhashed ? MC_UNHASHED : MC_NOTINSET]++;
		else if (!bit && (dropBits & anyBit)) s->mc[unhashed ? MC_UNHASHED : MC_NOTINSET]++;
		if (!wantTap) break;
		// 4. the record: its rows and constants must all be available (now, or at placement), else the draw passes
		MList* l = s->deferred == 1 ? s->list : g_immList;
		if (!l) { l = ListNew(); if (!l) { s->mc[MC_NOMEM]++; wantDrop = false; break; } if (s->deferred == 1) s->list = l; else g_immList = l; }
		uint32_t inst = a.instanceCount ? a.instanceCount : 1;
		if (inst > VPSHELL_MODELTAP_MAX_INSTANCES) { s->mc[MC_BOUND]++; wantDrop = false; break; }
		const uint32_t rowsPer = cls == VPMODEL_CLASS_SKINNED ? VPSHELL_MODELTAP_PALETTE_ROWS : VPSHELL_MODELTAP_INST_ROWS;
		const uint32_t instRows = inst * rowsPer;   // total; the record carries rowsPer (per instance)
		// the instance source
		void* instRes = nullptr; uint32_t instOff = 0, instBytes = 0; const uint8_t* instSrc = nullptr; bool instPend = false;
		void* palRes = nullptr; uint32_t palFirst = 0; const uint8_t* palSrc = nullptr; bool palPend = false;
		bool complete = true;
		if (cls == VPMODEL_CLASS_SINGLE)
		{
			c->VSGetConstantBuffers(0, 2, g.cb);
			instRes = g.cb[0]; instOff = 0; instBytes = 64;
			InterestAdd(g.cb[0], BufBytes(g.cb[0])); InterestNeed(g.cb[0], 64);
		}
		else
		{
			const uint32_t stride = cls == VPMODEL_CLASS_SKINNED ? 4u : 64u;
			c->VSGetConstantBuffers(1, 1, &g.cb[1]);
			if (!g.vb[1] || strides[1] != stride) complete = false;
			instRes = g.vb[1]; instOff = offs[1] + a.startInstance * stride; instBytes = inst * stride;
			InterestAdd(g.vb[1], BufBytes(g.vb[1])); InterestNeed(g.vb[1], instOff + instBytes);
			if (cls == VPMODEL_CLASS_SKINNED)
			{
				c->VSGetShaderResources(27, 1, &g.t27);
				D3D11_SHADER_RESOURCE_VIEW_DESC vd;
				if (g.t27) { g.t27->GetDesc(&vd); g.t27->GetResource(&g.t27res); }
				if (!g.t27 || !g.t27res || vd.ViewDimension != D3D11_SRV_DIMENSION_BUFFER) complete = false;
				else
				{
					palRes = g.t27res; palFirst = vd.Buffer.FirstElement;
					ID3D11Buffer* pb = nullptr;
					if (SUCCEEDED(g.t27res->QueryInterface(__uuidof(ID3D11Buffer), (void**)&pb)) && pb) { InterestAdd(g.t27res, BufBytes(pb)); pb->Release(); }
				}
			}
		}
		c->PSGetConstantBuffers(1, 1, &g.pcb1);
		const uint32_t vsCbBytes = BufBytes(g.cb[1]), psCbBytes = BufBytes(g.pcb1);
		InterestAdd(g.cb[1], vsCbBytes); InterestAdd(g.pcb1, psCbBytes); InterestNeed(g.cb[1], vsCbBytes); InterestNeed(g.pcb1, psCbBytes);
		if (vsCbBytes > VPSHELL_MODELTAP_CB_MAX_BYTES || psCbBytes > VPSHELL_MODELTAP_CB_MAX_BYTES || vsCbBytes % 16 || psCbBytes % 16) { s->mc[MC_BOUND]++; complete = false; }
		if (complete)
		{
			instSrc = Source(s, instRes, instOff + instBytes, instPend);
			if (!instSrc) complete = false;
		}
		const uint8_t* vsCbSrc = nullptr; const uint8_t* psCbSrc = nullptr; bool vsCbPend = false, psCbPend = false;
		if (complete && g.cb[1]) { vsCbSrc = Source(s, g.cb[1], vsCbBytes, vsCbPend); if (!vsCbSrc) complete = false; }
		if (complete && g.pcb1 && g.pcb1 != g.cb[1]) { psCbSrc = Source(s, g.pcb1, psCbBytes, psCbPend); if (!psCbSrc) complete = false; }
		const Shadow* palSh = nullptr;
		if (complete && cls == VPMODEL_CLASS_SKINNED)
		{
			// every id's palette rows must be inside the palette's copy. The ids are checked against the copy the draw reads
			// (for a deferred draw reading the immediate context's copies: against those copies as they are now; the
			// placement checks again and counts a miss, g_placeMiss, city-renderer.md 10.5 [I])
			uint64_t palEnd = 0;
			for (uint32_t i = 0; i < inst; ++i)
			{
				uint32_t id; memcpy(&id, instSrc + instOff + 4 * i, 4);
				const uint64_t e = ((uint64_t)palFirst + (uint64_t)VPSHELL_MODELTAP_PALETTE_ROWS * id + VPSHELL_MODELTAP_PALETTE_ROWS) * 16;
				if (e > palEnd) palEnd = e;
			}
			InterestNeed(palRes, palEnd > 0xFFFFFFF0ull ? 0xFFFFFFF0u : (uint32_t)palEnd);
			palSh = SourceSh(s, palRes, 16, palPend);
			if (!palSh || palEnd > palSh->bytes) complete = false;
			else palSrc = palSh->data;
		}
		if (!complete)
		{
			s->mc[MC_NOSHADOW]++;
			wantDrop = false;
			// the draw is still tapped (a passed record without rows) only for the test key; a record needs its rows
			break;
		}
		const uint32_t nConst = (g.cb[1] ? 1u : 0u) + (g.pcb1 && g.pcb1 != g.cb[1] ? 1u : 0u);
		const uint32_t newRows = instRows + vsCbBytes / 16 + (g.pcb1 != g.cb[1] ? psCbBytes / 16 : 0);
		if (!Reserve((LONG)(sizeof(VPModelTapDraw) + 16u * newRows + sizeof(VPModelTapConst) * nConst)))
			{ s->mc[MC_BUDGET]++; wantDrop = false; break; }
		if (!Grow((void**)&l->d, &l->capd, l->nd + 1, sizeof(VPModelTapDraw)) || !Grow((void**)&l->r, &l->capr, l->nr + newRows, 16) ||
		    !Grow((void**)&l->ref, &l->capref, l->nref + 4 + inst, sizeof(MRef)) || !Grow((void**)&l->k, &l->capk, l->nk + 2, sizeof(VPModelTapConst)) ||
		    !Grow((void**)&l->kh, &l->capkh, l->nk + 2, 8))
			{ s->mc[MC_NOMEM]++; wantDrop = false; break; }
		const int target = ListTarget(l, s->lastTarget);
		if (target < 0) { s->mc[MC_BOUND]++; wantDrop = false; break; }
		// 5. before a drop the bound shaders are read back (the units' rule: a stale track may pass a draw, never drop one)
		if (wantDrop)
		{
			ID3D11PixelShader* bps = nullptr; ID3D11VertexShader* bvs = nullptr;
			c->PSGetShader(&bps, nullptr, nullptr);
			c->VSGetShader(&bvs, nullptr, nullptr);
			const bool same = bps && bvs && MarkLookup(bps) == s->ps && MarkLookup(bvs) == s->vs;
			if (bps) bps->Release();
			if (bvs) bvs->Release();
			if (!same) { s->mc[MC_MISMATCH]++; wantDrop = false; }
		}
		// ticket 0788: the first frame a key is droppable its draw is held (passed, recorded HELD); see HoldSeenBefore
		if (wantDrop && g_mHold && !HoldSeenBefore(VPModelHash64(&key, sizeof(key)))) { wantDrop = false; held = true; s->mc[MC_HELD]++; }
		// the record
		VPModelTapDraw& d = l->d[l->nd];
		memset(&d, 0, sizeof(d));
		d.key = key; d.cls = cls; d.bits = setBits;
		d.flags = (uint8_t)((wantDrop ? VPMTAP_DRAW_DROPPED : 0) | (s->deferred == 1 ? VPMTAP_DRAW_DEFERRED : 0) |
		                    (!inSet ? VPMTAP_DRAW_NOT_IN_SET : 0) | (!latched ? VPMTAP_DRAW_BIT_OFF : 0) | (unhashed ? VPMTAP_DRAW_UNHASHED : 0) |
		                    (held ? VPMTAP_DRAW_HELD : 0));
		d.target = (uint32_t)target;
		d.instanceCount = inst; d.startInstance = a.startInstance;
		d.instRow = l->nr; d.instRows = rowsPer;
		d.env[0] = tid[2]; d.env[1] = tid[3]; d.env[2] = tid[4];
		d.vsCb1 = d.psCb1 = 0xFFFFFFFFu;
		if (cls == VPMODEL_CLASS_SKINNED)
		{
			memcpy(&d.skinId0, instSrc + instOff, 4);
			if (!instPend && !palPend)
			{
				for (uint32_t i = 0; i < inst; ++i)
				{
					uint32_t id; memcpy(&id, instSrc + instOff + 4 * i, 4);
					memcpy(l->r[l->nr + i * VPSHELL_MODELTAP_PALETTE_ROWS], palSrc + (size_t)(palFirst + VPSHELL_MODELTAP_PALETTE_ROWS * id) * 16,
					       VPSHELL_MODELTAP_PALETTE_ROWS * 16);
					if (g_mapCache == 2)   // ticket 0719, TEST ONLY
					{
						VerifyRead(instSrc + instOff + 4 * i, 4);
						VerifyRead(palSrc + (size_t)(palFirst + VPSHELL_MODELTAP_PALETTE_ROWS * id) * 16, VPSHELL_MODELTAP_PALETTE_ROWS * 16);
					}
				}
			}
			else
			{
				MRef& f = l->ref[l->nref++];   // the ids and the palette are read at placement
				f.kind = MREF_PALETTE; f.row = l->nr; f.srcOff = instOff; f.bytes = 0; f.res = instRes; f.res2 = palRes; f.count = inst; f.first = palFirst;
			}
		}
		else if (cls == VPMODEL_CLASS_SINGLE)   // one cb0 block (rows 0-3) for every instance: instRows is per instance
			for (uint32_t i = 0; i < inst; ++i) PutRows(l, l->nr + i * VPSHELL_MODELTAP_INST_ROWS, instSrc, instPend, instRes, 0, 64);
		else PutRows(l, l->nr, instSrc, instPend, instRes, instOff, instBytes);
		l->nr += instRows;
		if (g.cb[1])
		{
			VPModelTapConst& k = l->k[l->nk];
			k.firstRow = l->nr; k.rows = vsCbBytes / 16; k.stage = VPMODEL_STAGE_VS; k.uses = 1;
			l->kh[l->nk] = 0;
			PutRows(l, l->nr, vsCbSrc, vsCbPend, g.cb[1], 0, vsCbBytes);
			l->nr += vsCbBytes / 16;
			d.vsCb1 = l->nk++;
			if (g.pcb1 == g.cb[1]) d.psCb1 = d.vsCb1;
		}
		if (g.pcb1 && g.pcb1 != g.cb[1])
		{
			VPModelTapConst& k = l->k[l->nk];
			k.firstRow = l->nr; k.rows = psCbBytes / 16; k.stage = VPMODEL_STAGE_PS; k.uses = 1;
			l->kh[l->nk] = 0;
			PutRows(l, l->nr, psCbSrc, psCbPend, g.pcb1, 0, psCbBytes);
			l->nr += psCbBytes / 16;
			d.psCb1 = l->nk++;
		}
		l->nd++;
		s->mc[MC_TAPPED]++;
		if (wantDrop) { l->dropped[bi]++; s->mc[MC_DROP_C + bi]++; }
		else l->passedTapped++;
		drop = wantDrop;
	} while (0);
	if (!drop && !held)   // a held draw is counted as held (ticket 0788), not as a passed one
	{
		// passed while a bit it may belong to was latched: the counters name the mixed picture (city-renderer.md 3.2)
		const LONG pb = g_mPairBit[cls] ? (LONG)g_mPairBit[cls] : (dropBits & VPMODE_LANDMARKS ? VPMODE_LANDMARKS : dropBits & VPMODE_CITIES);
		if (dropBits & pb) s->mc[pb == VPMODE_LANDMARKS ? MC_PSET_L : MC_PSET_C]++;
	}
	GotRelease(g);
	InterlockedExchangeAdd64(&g_mDrawQpc, QpcNow() - t0);
	return drop;
}

// resolve l's references from the immediate context's copies (render thread, at the list's placement)
static void ResolveRefs(MList* l)
{
	for (uint32_t i = 0; i < l->nref; ++i)
	{
		const MRef& f = l->ref[i];
		const Shadow* sh = ShadowFind(f.res, g_immCi);
		if (f.kind == MREF_COPY)
		{
			if (sh && sh->data && (uint64_t)f.srcOff + f.bytes <= sh->bytes)
			{
				memcpy(l->r[f.row], sh->data + f.srcOff, f.bytes);
				if (g_mapCache == 2) VerifyRead(sh->data + f.srcOff, f.bytes);   // ticket 0719, TEST ONLY
			}
			else { memset(l->r[f.row], 0, f.bytes); InterlockedIncrement(&g_placeMiss); }
			continue;
		}
		const Shadow* pal = ShadowFind(f.res2, g_immCi);
		for (uint32_t k = 0; k < f.count; ++k)
		{
			float (*dst)[4] = &l->r[f.row + k * VPSHELL_MODELTAP_PALETTE_ROWS];
			uint32_t id = 0;
			const bool idOk = sh && sh->data && (uint64_t)f.srcOff + 4ull * (k + 1) <= sh->bytes;
			if (idOk) memcpy(&id, sh->data + f.srcOff + 4 * k, 4);
			const uint64_t at = ((uint64_t)f.first + (uint64_t)VPSHELL_MODELTAP_PALETTE_ROWS * id) * 16;
			if (idOk && g_mapCache == 2) VerifyRead(sh->data + f.srcOff + 4 * k, 4);   // ticket 0719, TEST ONLY
			if (idOk && pal && pal->data && at + VPSHELL_MODELTAP_PALETTE_ROWS * 16 <= pal->bytes)
			{
				memcpy(dst, pal->data + at, VPSHELL_MODELTAP_PALETTE_ROWS * 16);
				if (g_mapCache == 2) VerifyRead(pal->data + at, VPSHELL_MODELTAP_PALETTE_ROWS * 16);
			}
			else { memset(dst, 0, VPSHELL_MODELTAP_PALETTE_ROWS * 16); InterlockedIncrement(&g_placeMiss); }
		}
	}
	l->nref = 0;
}

// append l (references resolved) to the frame, constants deduplicated by content, targets merged (render thread)
static void Place(MList* l)
{
	if (!l || !l->nd) { if (l) ListClear(l); return; }
	ResolveRefs(l);
	MList& f = g_frame;
	if (!Grow((void**)&f.d, &f.capd, f.nd + l->nd, sizeof(VPModelTapDraw)) || !Grow((void**)&f.r, &f.capr, f.nr + l->nr, 16) ||
	    !Grow((void**)&f.k, &f.capk, f.nk + l->nk, sizeof(VPModelTapConst)) || !Grow((void**)&f.kh, &f.capkh, f.nk + l->nk, 8))
	{
		// cannot happen within the budget; the dropped draws of l lose their records: counted (LOUD at the stats line)
		f.lost += l->dropped[0] + l->dropped[1];
		ListClear(l);
		return;
	}
	const uint32_t rowBase = f.nr;
	memcpy(f.r[f.nr], l->r, (size_t)l->nr * 16);
	f.nr += l->nr;
	uint32_t kmap[64];
	uint32_t* km = l->nk <= 64 ? kmap : (uint32_t*)malloc(sizeof(uint32_t) * l->nk);
	for (uint32_t i = 0; i < l->nk; ++i)
	{
		const VPModelTapConst& k = l->k[i];
		const uint64_t h = VPModelHash64(l->r[k.firstRow], (size_t)k.rows * 16);
		uint32_t j = 0;
		for (; j < f.nk; ++j)
			if (f.kh[j] == h && f.k[j].rows == k.rows && !memcmp(f.r[f.k[j].firstRow], l->r[k.firstRow], (size_t)k.rows * 16)) break;
		if (j == f.nk) { f.k[j] = k; f.k[j].firstRow += rowBase; f.k[j].uses = 0; f.kh[j] = h; f.nk++; }
		f.k[j].uses += k.uses;
		if (km) km[i] = j;
	}
	uint32_t tmap[VPSHELL_MODELTAP_MAX_TARGETS];
	for (uint32_t i = 0; i < l->nt; ++i)
	{
		uint32_t j = 0;
		for (; j < f.nt; ++j) if (!memcmp(&f.t[j], &l->t[i], 24)) break;
		if (j == f.nt && f.nt < VPSHELL_MODELTAP_MAX_TARGETS) { f.t[j] = l->t[i]; f.t[j].draws = 0; f.nt++; }
		if (j < f.nt) f.t[j].draws += l->t[i].draws;
		tmap[i] = j < VPSHELL_MODELTAP_MAX_TARGETS ? j : 0;
	}
	for (uint32_t i = 0; i < l->nd; ++i)
	{
		VPModelTapDraw d = l->d[i];
		d.instRow += rowBase;
		if (d.vsCb1 != 0xFFFFFFFFu) d.vsCb1 = km ? km[d.vsCb1] : 0xFFFFFFFFu;
		if (d.psCb1 != 0xFFFFFFFFu) d.psCb1 = km ? km[d.psCb1] : 0xFFFFFFFFu;
		d.target = tmap[d.target < l->nt ? d.target : 0];
		d.order = f.nd;
		f.d[f.nd++] = d;
	}
	f.dropped[0] += l->dropped[0]; f.dropped[1] += l->dropped[1]; f.passedTapped += l->passedTapped;
	if (km && km != kmap) free(km);
	ListClear(l);
}

static void OnFinishList(ID3D11DeviceContext* c, ID3D11CommandList* cl)
{
	UfCtx* s = SlotFind(c);
	if (!s) return;
	s->epoch++;
	MList* l = s->list;
	if (!l || !l->nd) return;   // an empty list stays with the context
	AcquireSRWLockExclusive(&g_clLock);
	int at = -1, oldest = 0;
	for (int i = 0; i < kClRecs; ++i)
	{
		if (!g_clRecs[i].cl) { at = i; break; }
		if (g_clRecs[i].present < g_clRecs[oldest].present) oldest = i;
	}
	if (at < 0) { InterlockedIncrement(&g_clFull); ListFree(g_clRecs[oldest].list); at = oldest; }
	g_clRecs[at].cl = cl; g_clRecs[at].list = l; g_clRecs[at].present = g_presentNo;
	ReleaseSRWLockExclusive(&g_clLock);
	s->list = nullptr;
}

static void OnExecuteList(ID3D11CommandList* cl)
{
	MaybeMFault(MFF_EXECUTE);
	Place(g_immList);   // the immediate context's draws so far come first (execution order)
	MList* l = nullptr;
	AcquireSRWLockExclusive(&g_clLock);
	for (int i = 0; i < kClRecs; ++i)
		if (g_clRecs[i].cl == cl) { l = g_clRecs[i].list; g_clRecs[i].cl = nullptr; g_clRecs[i].list = nullptr; break; }
	ReleaseSRWLockExclusive(&g_clLock);
	if (l) { Place(l); ListFree(l); }
}

// ---- the detours' bodies ----
static void SafeOnBufferCreated(const D3D11_BUFFER_DESC* bd, const D3D11_SUBRESOURCE_DATA* sd, void* obj)
{
	__try { OnBufferCreated(bd, sd, obj); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("CreateBuffer", GetExceptionCode()); }
}
static void SafeOnTextureCreated(const D3D11_TEXTURE2D_DESC* td, const D3D11_SUBRESOURCE_DATA* sd, void* obj)
{
	__try { OnTextureCreated(td, sd, obj); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("CreateTexture2D", GetExceptionCode()); }
}

HRESULT UnitFilter_CreateBuffer(PFN_UfCreateBuffer tr, ID3D11Device* d, const D3D11_BUFFER_DESC* bd, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Buffer** out)
{
	HRESULT hr = tr(d, bd, sd, out);
	if (SUCCEEDED(hr) && bd && out && *out && !ModelsOff()) { EnsureInit(); SafeOnBufferCreated(bd, sd, *out); }
	return hr;
}

HRESULT UnitFilter_CreateTexture2D(PFN_UfCreateTex2D tr, ID3D11Device* d, const D3D11_TEXTURE2D_DESC* td, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Texture2D** out)
{
	HRESULT hr = tr(d, td, sd, out);
	if (SUCCEEDED(hr) && td && out && *out && !ModelsOff()) { EnsureInit(); SafeOnTextureCreated(td, sd, *out); }
	return hr;
}

HRESULT UnitFilter_Map(PFN_UfMap tr, ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m)
{
	HRESULT hr = tr(c, r, sub, type, flags, m);
	if (SUCCEEDED(hr) && g_nInt && m && m->pData && sub == 0 && !ModelsOff() &&
	    (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE || type == D3D11_MAP_WRITE))
	{
		__try { OnMap(c, r, type, m); }
		__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("Map", GetExceptionCode()); }
	}
	return hr;
}

void UnitFilter_Unmap(PFN_UfUnmap tr, ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub)
{
	// the copy is taken before the Unmap (the mapped memory is the EXE's own until then); ticket 0719: a cached map's
	// Unmap forwards its staging whatever the model filter's state now is
	if (sub == 0 && (g_cachedOpen || (g_nInt && !ModelsOff())))
	{
		__try { OnUnmap(c, r); }
		__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("Unmap", GetExceptionCode()); }
	}
	tr(c, r, sub);
}

static void SafeWritten(ID3D11Resource* r)
{
	__try { OnResourceWritten(r); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("resource write (model identity)", GetExceptionCode()); }
}
void UnitFilter_UpdateSubresource(PFN_UfUpdateSub tr, ID3D11DeviceContext* c, ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* p, UINT rp, UINT dp)
{
	tr(c, dst, sub, box, p, rp, dp);
	if (!ModelsOff() && g_resUsed) SafeWritten(dst);
}
void UnitFilter_CopyResource(PFN_UfCopyRes tr, ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src)
{
	tr(c, dst, src);
	if (!ModelsOff() && g_resUsed) SafeWritten(dst);
}
void UnitFilter_CopySubresourceRegion(PFN_UfCopySubRegion tr, ID3D11DeviceContext* c, ID3D11Resource* dst, UINT dsub, UINT x, UINT y, UINT z, ID3D11Resource* src, UINT ssub, const D3D11_BOX* box)
{
	tr(c, dst, dsub, x, y, z, src, ssub, box);
	if (!ModelsOff() && g_resUsed) SafeWritten(dst);
}

void UnitFilter_OnFinishList(ID3D11DeviceContext* c, ID3D11CommandList* cl)
{
	if (ModelsOff() || !c || !cl) return;
	__try { OnFinishList(c, cl); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("FinishCommandList (model records)", GetExceptionCode()); }
}

void UnitFilter_OnExecuteList(ID3D11DeviceContext* c, ID3D11CommandList* cl)
{
	if (ModelsOff() || !c || !cl) return;
	__try { OnExecuteList(cl); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("ExecuteCommandList (model records)", GetExceptionCode()); }
}

// ===========================================================================
// Draws
// ===========================================================================
static bool ModelDraw(ID3D11DeviceContext* c, UfCtx* s, const UfDrawArgs& a, uint8_t cls);

// A draw with a recognised program bound: decide, count. true = do not pass it to d3d11.
static bool DrawSlow(ID3D11DeviceContext* c, UfCtx* s, const UfDrawArgs& a)
{
	const uint8_t vs = s->vs, ps = s->ps;
	// ticket 4874: a model program bound (12..21): the model filter's path; the units' path below is unchanged
	if ((vs != UF_NONE && IsModelProg(vs)) || (ps != UF_NONE && IsModelProg(ps)))
	{
		if (ModelsOff()) return false;
		MaybeMFault(MFF_DRAW);
		if (vs != UF_NONE) s->seen[vs]++;
		if (ps != UF_NONE) s->seen[ps]++;
		const uint8_t cls = vs != UF_NONE && ps != UF_NONE && IsModelProg(vs) && IsModelProg(ps) ? g_mPairCls[vs - UF_MODEL0][ps - UF_MODEL0] : (uint8_t)VPMODEL_CLASS_NONE;
		if (cls == VPMODEL_CLASS_NONE) { s->mc[MC_KEEP]++; return false; }   // the mask pass, or a foreign pairing
		s->mc[MC_SEEN0 + cls]++;
		const bool drop = ModelDraw(c, s, a, cls);
		if (drop) { s->dropped[vs]++; s->dropped[ps]++; }
		else { s->passed[vs]++; s->passed[ps]++; }
		return drop;
	}
	if (g_off) return false;
	MaybeFault(UFF_DRAW);
	if (vs != UF_NONE) s->seen[vs]++;
	if (ps != UF_NONE) s->seen[ps]++;
	bool drop = false;
	if (ps != UF_NONE && g_isDropPs[ps])
	{
		if (g_dropNow && vs == g_pairVs[ps])
		{
			// the track is checked against the context before a drop: a stale track may pass a draw, never drop one
			ID3D11PixelShader* bps = nullptr; ID3D11VertexShader* bvs = nullptr;
			c->PSGetShader(&bps, nullptr, nullptr);
			c->VSGetShader(&bvs, nullptr, nullptr);
			drop = bps && bvs && MarkLookup(bps) == ps && MarkLookup(bvs) == vs;
			if (bps) bps->Release();
			if (bvs) bvs->Release();
			if (!drop) s->mismatch++;
		}
		if (!drop && g_setNow) s->passedSet++;
	}
	else if (ps != UF_NONE && g_isMaskPs[ps])
	{
		// ticket 4785: the units' shadow-mask pairs, dropped while the server casts the units' shadows (the same read-back check)
		if (g_maskDropNow && vs == g_maskPairVs[ps])
		{
			ID3D11PixelShader* bps = nullptr; ID3D11VertexShader* bvs = nullptr;
			c->PSGetShader(&bps, nullptr, nullptr);
			c->VSGetShader(&bvs, nullptr, nullptr);
			drop = bps && bvs && MarkLookup(bps) == ps && MarkLookup(bvs) == vs;
			if (bps) bps->Release();
			if (bvs) bvs->Release();
			if (!drop) s->mismatch++;
		}
		if (!drop && g_maskSetNow) s->passedSetMask++;
	}
	if (drop) { if (vs != UF_NONE) s->dropped[vs]++; s->dropped[ps]++; }
	else { if (vs != UF_NONE) s->passed[vs]++; if (ps != UF_NONE) s->passed[ps]++; }
	return drop;
}

static __declspec(noinline) bool SafeDrawSlow(ID3D11DeviceContext* c, UfCtx* s, const UfDrawArgs& a)
{
	__try { return DrawSlow(c, s, a); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("draw", GetExceptionCode()); }
	return false;
}

static inline bool Drop(ID3D11DeviceContext* c, int entry, const UfDrawArgs& a)
{
	if (BothOff()) return false;
	UfCtx* s = Slot(c);
	if (!s) { InterlockedIncrement(&g_untracked); return false; }
	s->entry[entry]++;
	return s->any && SafeDrawSlow(c, s, a);
}

void UnitFilter_DrawII(PFN_UfDrawII tr, ID3D11DeviceContext* c, UINT a, UINT b, UINT s, INT bv, UINT si)
{
	const UfDrawArgs d = { a, b, s, si, bv, 1 };
	if (!Drop(c, UF_E_DII, d)) tr(c, a, b, s, bv, si);
}
void UnitFilter_DrawI(PFN_UfDrawI tr, ID3D11DeviceContext* c, UINT a, UINT s, INT bv)
{
	const UfDrawArgs d = { a, 1, s, 0, bv, 1 };
	if (!Drop(c, UF_E_DI, d)) tr(c, a, s, bv);
}
// the other entries: no model draw is ever dropped through them (no index range: no mesh key); counted as "entry"
static const UfDrawArgs kNoArgs = { 0, 0, 0, 0, 0, 0 };
void UnitFilter_Draw(PFN_UfDraw tr, ID3D11DeviceContext* c, UINT a, UINT s) { if (!Drop(c, UF_E_D, kNoArgs)) tr(c, a, s); }
void UnitFilter_DrawInst(PFN_UfDrawInst tr, ID3D11DeviceContext* c, UINT a, UINT b, UINT s, UINT si) { if (!Drop(c, UF_E_DINST, kNoArgs)) tr(c, a, b, s, si); }
void UnitFilter_DrawIIIndirect(PFN_UfDrawIndirect tr, ID3D11DeviceContext* c, ID3D11Buffer* buf, UINT off) { if (!Drop(c, UF_E_DIIIND, kNoArgs)) tr(c, buf, off); }
void UnitFilter_DrawInstIndirect(PFN_UfDrawIndirect tr, ID3D11DeviceContext* c, ID3D11Buffer* buf, UINT off) { if (!Drop(c, UF_E_DINSTIND, kNoArgs)) tr(c, buf, off); }
void UnitFilter_DrawAuto(PFN_UfDrawAuto tr, ID3D11DeviceContext* c) { if (!Drop(c, UF_E_DAUTO, kNoArgs)) tr(c); }

// ===========================================================================
// Path 2: the EXE's renderer (render thread, once, at a Present after the EXE validation)
// ===========================================================================
static bool InModule(const void* p, const char* name)
{
	HMODULE m = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m)) return false;
	char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH);
	const char* s = strrchr(path, '\\');
	return !_stricmp(s ? s + 1 : path, name);
}

// One program: its object, or null with why. Every read is of EXE data at an address the patterns confirmed (the site's
// pattern, kUfPats[UF_COMMON_PATS + i], checked by Path2: the push of the blob and the handle store).
static void* Resolve(int i, int build, uint32_t base, ID3D11Device* dev, uint32_t vsCount, uint32_t psCount, char* why, size_t whyN)
{
	const UfSite& st = kSites[i];
	const bool vs = g_stage[i] == VPUNIT_STAGE_VS;
	const uintptr_t pushAt = ShellExeAddr(build, st.push, base), blobAt = ShellExeAddr(build, st.blob, base);
	const uintptr_t handleAt = ShellExeAddr(build, st.handle, base);
	const char* pushName = VpExeSymName(st.push);
	const uint8_t* push = (const uint8_t*)pushAt;
	const uint8_t* call = (const uint8_t*)(pushAt + UF_CALL_OFF);
	const uint8_t* store = (const uint8_t*)(pushAt + st.storeOff);
	if (!pushAt || !blobAt || !handleAt) { _snprintf_s(why, whyN, _TRUNCATE, "its site is not carried to %s", VpExeBuildLabel(build)); return nullptr; }
	if (push[0] != 0x68 || *(const uint32_t*)(push + 1) != (uint32_t)blobAt) { _snprintf_s(why, whyN, _TRUNCATE, "the push of its blob at %s differs", pushName); return nullptr; }
	const uintptr_t target = ShellExeAddr(build, vs ? VPX_uf_create_vs : VPX_uf_create_ps, base);
	if (call[0] != 0xE8 || (uintptr_t)(pushAt + UF_CALL_OFF + 5 + *(const int32_t*)(call + 1)) != target)
		{ _snprintf_s(why, whyN, _TRUNCATE, "the create call at %s+0x%X does not call %s", pushName, UF_CALL_OFF, vs ? "uf_create_vs" : "uf_create_ps"); return nullptr; }
	if (store[0] != 0x66 || store[1] != 0xA3 || *(const uint32_t*)(store + 2) != (uint32_t)handleAt)
		{ _snprintf_s(why, whyN, _TRUNCATE, "the handle store at %s+0x%X differs", pushName, st.storeOff); return nullptr; }
	const uint8_t* blob = (const uint8_t*)blobAt;
	if (memcmp(blob, "DXBC", 4)) { _snprintf_s(why, whyN, _TRUNCATE, "no DXBC container at %s", VpExeSymName(st.blob)); return nullptr; }
	const uint32_t size = *(const uint32_t*)(blob + 24);
	if (size < 32 || size > 65536) { _snprintf_s(why, whyN, _TRUNCATE, "the blob %s says %u bytes", VpExeSymName(st.blob), size); return nullptr; }
	uint8_t d[32];
	if (!Sha256(blob, size, d)) { _snprintf_s(why, whyN, _TRUNCATE, "SHA-256 failed"); return nullptr; }
	if (memcmp(d, g_dig[i], 32))
		{ _snprintf_s(why, whyN, _TRUNCATE, "the blob %s (%u bytes) hashes to %02x%02x%02x%02x..., not the table's digest", VpExeSymName(st.blob), size, d[0], d[1], d[2], d[3]); return nullptr; }
	const uint32_t h = *(const uint16_t*)handleAt;
	const uint32_t count = vs ? vsCount : psCount;
	if (h >= count || h >= UF_MAX_HANDLES) { _snprintf_s(why, whyN, _TRUNCATE, "handle %u at %s is not below the renderer's %s count %u", h, VpExeSymName(st.handle), vs ? "VS" : "PS", count); return nullptr; }
	void* obj = *(void* const*)(ShellExeAddr(build, vs ? VPX_uf_vs_obj : VPX_uf_ps_obj, base) + 4 * h);
	if (!obj) { _snprintf_s(why, whyN, _TRUNCATE, "handle %u holds no object", h); return nullptr; }
	if (vs)
	{
		const uint32_t bc = *(const uint32_t*)(ShellExeAddr(build, VPX_uf_vs_bc, base) + 4 * h);
		const uint32_t sz = *(const uint32_t*)(ShellExeAddr(build, VPX_uf_vs_size, base) + 4 * h);
		if (bc != (uint32_t)blobAt || sz != size)
			{ _snprintf_s(why, whyN, _TRUNCATE, "handle %u was created from module offset 0x%08X (%u bytes), not from its blob", h, bc - base, sz); return nullptr; }
	}
	// the object: a d3d11 object of the expected stage on the EXE's device
	void* vtbl = *(void* const*)obj;
	if (!InModule(*(void* const*)vtbl, "d3d11.dll")) { _snprintf_s(why, whyN, _TRUNCATE, "object %p of handle %u is not a d3d11 object", obj, h); return nullptr; }
	IUnknown* u = (IUnknown*)obj;
	void* q = nullptr;
	if (FAILED(u->QueryInterface(vs ? __uuidof(ID3D11VertexShader) : __uuidof(ID3D11PixelShader), &q)) || !q)
		{ _snprintf_s(why, whyN, _TRUNCATE, "object %p of handle %u is not a %s", obj, h, vs ? "vertex shader" : "pixel shader"); return nullptr; }
	ID3D11Device* od = nullptr;
	((ID3D11DeviceChild*)q)->GetDevice(&od);
	((IUnknown*)q)->Release();
	if (od) od->Release();
	if (q != obj || od != dev) { _snprintf_s(why, whyN, _TRUNCATE, "object %p of handle %u is not on the EXE's device", obj, h); return nullptr; }
	return obj;
}

// one group of programs [first, last): the units' 0..11 or (ticket 4874) the models' 12..21; the build, the common
// patterns, level, device and renderer checks are the same for both; the site patterns are the group's own
#define UF_COMMON_PATS 10
static bool Path2(const UnitFilterEnv& e, void** objs, char* why, size_t whyN, int& failed, int first = 0, int last = UF_UNIT_PROGS)
{
	failed = -1;
	const uint32_t base = e.exeBase;
	for (int i = first; i < last; ++i)
		if (ProgVa(i) && ProgVa(i) != kVpExeSymAnchor[kSites[i].blob])
			{ failed = i; _snprintf_s(why, whyN, _TRUNCATE, "%s names VA 0x%X, the build table's anchor of %s is 0x%X (a build error)", IsModelProg(i) ? "shell_model_scene.h" : "unit_programs.h", ProgVa(i), VpExeSymName(kSites[i].blob), kVpExeSymAnchor[kSites[i].blob]); return false; }
	const int build = ShellExeResolve(base, kUfNeed, (int)(sizeof(kUfNeed) / sizeof(kUfNeed[0])), why, whyN);
	if (build < 0) return false;
	if (!ShellExeCheck(build, base, kUfPats, UF_COMMON_PATS, why, whyN)) return false;
	for (int i = first; i < last; ++i)
		if (!ShellExeCheck(build, base, kUfPats + UF_COMMON_PATS + i, 1, why, whyN)) { failed = i; return false; }
	const uint32_t level = *(const uint32_t*)ShellExeAddr(build, VPX_uf_level, base);
	if (level != UF_LEVEL_5_0)
	{
		_snprintf_s(why, whyN, _TRUNCATE, "the EXE runs shader level %u, not 10: it created the %s variants of its programs, not the 5_0 blobs "
			"the table names", level, level <= 8 ? "4_0" : level == 9 ? "4_1" : "other");
		return false;
	}
	ID3D11Device* exeDev = *(ID3D11Device* const*)ShellExeAddr(build, VPX_pxw_device, base);
	if (!exeDev || exeDev != e.dev) { _snprintf_s(why, whyN, _TRUNCATE, "the EXE's device %p is not the target's device %p", exeDev, e.dev); return false; }
	const uint8_t* r = *(const uint8_t* const*)ShellExeAddr(build, VPX_pxw_renderer, base);
	if (!r) { _snprintf_s(why, whyN, _TRUNCATE, "no renderer at pxw_renderer"); return false; }
	const uint32_t vsCount = *(const uint32_t*)(r + UF_RENDERER_VS_COUNT), psCount = *(const uint32_t*)(r + UF_RENDERER_PS_COUNT);
	for (int i = first; i < last; ++i)
	{
		char w[200] = "";
		objs[i] = Resolve(i, build, base, exeDev, vsCount, psCount, w, sizeof(w));
		if (!objs[i]) { failed = i; _snprintf_s(why, whyN, _TRUNCATE, "%s %s: %s", g_stage[i] == VPUNIT_STAGE_VS ? "VS" : "PS", g_short[i], w); return false; }
	}
	return true;
}

static void RunPath2(const UnitFilterEnv& e)
{
	g_path2Done = true;
	void* objs[UF_PROGS] = {};
	char why[300] = "";
	int failed = -1;
	bool ok = false;
	__try { ok = Path2(e, objs, why, sizeof(why), failed); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); _snprintf_s(why, _TRUNCATE, "access violation 0x%08X while reading the EXE's renderer", (unsigned)GetExceptionCode()); ok = false; }
	if (!ok)
	{
		g_path2 = "refused";
		for (int i = 0; i < UF_UNIT_PROGS; ++i) InterlockedIncrement(&g_refused[i]);
		// not tagged LOUD here: it is LOUD at the first Present whose latch asks for drops (OnPresent), when it bites; a
		// process that never draws the server's units (ServerUnits = 0, the stand-ins of other suites) logs it once, plainly
		ULOG("unit_filter: path 2 REFUSED (EXE image 0x%08X): %s - nothing marked from the EXE, the unit filter is OFF for this "
		     "process (the EXE's units are drawn)", e.exeBase, why);
		_snprintf_s(g_unableWhy, _TRUNCATE, "path 2 REFUSED: %s", why);
		TurnOff("path 2 refused", VPFILTER_R_PATH2_REFUSED);
		return;
	}
	for (int i = 0; i < UF_UNIT_PROGS; ++i)
	{
		int had = MarkLookup(objs[i]);
		if (had != UF_NONE && had != i)
		{
			g_path2 = "refused";
			ULOG("unit_filter: path 2 REFUSED: object %p of %s is marked as %s by its creation - the unit filter is OFF", objs[i], g_short[i], g_short[had]);
			_snprintf_s(g_unableWhy, _TRUNCATE, "path 2 REFUSED: object %p of %s is marked as %s by its creation", objs[i], g_short[i], g_short[had]);
			TurnOff("path 2 conflict", VPFILTER_R_PATH2_REFUSED);
			return;
		}
	}
	for (int i = 0; i < UF_UNIT_PROGS; ++i)
	{
		if (MarkLookup(objs[i]) != i && !MarkSet(objs[i], i)) { ULOG("unit_filter: LOUD: the table of marked objects is full"); TurnOff("mark table full", VPFILTER_R_MARK_TABLE_FULL); return; }
		InterlockedIncrement(&g_resolved[i]);
	}
	g_path2 = "resolved";
	char list[400] = "";
	for (int i = 0; i < UF_UNIT_PROGS; ++i) { char one[40]; _snprintf_s(one, _TRUNCATE, "%s%s=%p", i ? " " : "", g_short[i], objs[i]); strcat_s(list, one); }
	ULOG("unit_filter: path 2: %d of %d programs resolved from the EXE's renderer (image 0x%08X, shader level 10, device %p): %s",
	     UF_UNIT_PROGS, UF_UNIT_PROGS, e.exeBase, e.dev, list);
}

// ticket 4874: path 2 for the ten BuildingShader programs, the same checks, all or nothing for the group; a refusal turns
// the model filter off (never the units')
static void RunModelPath2(const UnitFilterEnv& e)
{
	void* objs[UF_PROGS] = {};
	char why[300] = "";
	int failed = -1;
	bool ok = false;
	__try { ok = Path2(e, objs, why, sizeof(why), failed, UF_MODEL0, UF_PROGS); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); _snprintf_s(why, _TRUNCATE, "access violation 0x%08X while reading the EXE's renderer", (unsigned)GetExceptionCode()); ok = false; }
	if (ok)
		for (int i = UF_MODEL0; i < UF_PROGS && ok; ++i)
		{
			const int had = MarkLookup(objs[i]);
			if (had != UF_NONE && had != i) { _snprintf_s(why, _TRUNCATE, "object %p of %s is marked as %s by its creation", objs[i], g_short[i], g_short[had]); ok = false; }
		}
	if (!ok)
	{
		g_mpath2 = "refused";
		for (int i = UF_MODEL0; i < UF_PROGS; ++i) InterlockedIncrement(&g_refused[i]);
		ULOG("model_filter: path 2 REFUSED (EXE image 0x%08X): %s - nothing marked from the EXE, the model filter is OFF for this process "
		     "(the EXE's cities and landmarks are drawn)", e.exeBase, why);
		_snprintf_s(g_mUnableWhy, _TRUNCATE, "path 2 REFUSED: %s", why);
		MTurnOff("path 2 refused", VPFILTER_R_PATH2_REFUSED);
		return;
	}
	for (int i = UF_MODEL0; i < UF_PROGS; ++i)
	{
		if (MarkLookup(objs[i]) != i && !MarkSet(objs[i], i)) { ULOG("model_filter: LOUD: the table of marked objects is full"); MTurnOff("mark table full", VPFILTER_R_MARK_TABLE_FULL); return; }
		InterlockedIncrement(&g_resolved[i]);
	}
	g_mpath2 = "resolved";
	char list[400] = "";
	for (int i = UF_MODEL0; i < UF_PROGS; ++i) { char one[40]; _snprintf_s(one, _TRUNCATE, "%s%s=%p", i > UF_MODEL0 ? " " : "", g_short[i], objs[i]); strcat_s(list, one); }
	ULOG("model_filter: path 2: %d of %d programs resolved from the EXE's renderer (image 0x%08X, shader level 10, device %p): %s",
	     VPMODEL_PROGRAMS, VPMODEL_PROGRAMS, e.exeBase, e.dev, list);
}

// ===========================================================================
// Ticket 4874: the model tap's sections, the identity set, the publication, filterState2 (render thread, at Present)
// ===========================================================================
static HANDLE g_ctlMap, g_dataMap;
static VPShellModelTapControl* g_ctl;
static uint32_t g_slotBytes;
static uint64_t g_frameSeq;
static uint32_t g_setResult = VPMSET_NONE, g_setReads, g_framesTruncated, g_framesUnacked, g_published;
static uint32_t g_lastSetGenTried, g_lastSetResultLogged = 0xFFFFFFFFu;
static uint32_t g_lostDropped;
static bool g_lostLogged, g_missLogged;
static const char* const kSetResultNames[VPMSET_RESULT_COUNT] = { "none", "ok", "cannot open", "magic", "version", "bounds", "hash", "torn",
	"another server", "generation", "no memory" };

static void CtlWrite(void (*fill)(VPShellModelTapControl*))
{
	if (!g_ctl) return;
	InterlockedIncrement((volatile LONG*)&g_ctl->seq);   // odd
	MemoryBarrier();
	fill(g_ctl);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&g_ctl->seq);   // even
}

static VPShellModelTapControl* EnsureControl(const UnitFilterEnv& e)
{
	if (g_ctl || g_ctlMap) return g_ctl;
	wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELTAP_CONTROL_FMT, GetCurrentProcessId());
	g_ctlMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELTAP_CONTROL_BYTES, n);
	if (!g_ctlMap) { ULOG("model_filter: LOUD: the control section %ls cannot be created (%u): the model filter is OFF", n, GetLastError());
		MTurnOff("tap section", VPMFILTER_R_TAP_FAILED); g_ctlMap = INVALID_HANDLE_VALUE; return nullptr; }
	g_ctl = (VPShellModelTapControl*)MapViewOfFile(g_ctlMap, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_MODELTAP_CONTROL_BYTES);
	if (!g_ctl) { ULOG("model_filter: LOUD: the control section %ls cannot be mapped (%u): the model filter is OFF", n, GetLastError());
		MTurnOff("tap section", VPMFILTER_R_TAP_FAILED); return nullptr; }
	g_slotBytes = (uint32_t)e.modelSlotKb * 1024u;
	static uint32_t s_slot; s_slot = g_slotBytes;
	CtlWrite([](VPShellModelTapControl* c) {
		c->magic = VPSHELL_MODELTAP_MAGIC; c->version = VPSHELL_MODELTAP_VERSION; c->state = VPMTAP_RUNNING; c->failReason = VPMTAP_FAIL_NONE;
		c->exePid = GetCurrentProcessId(); c->slots = VPSHELL_MODELTAP_SLOTS; c->slotBytes = s_slot; });
	ULOG("model_filter: tap control %ls (%u slots of %u KB in the data section, made at the first frame)", n, VPSHELL_MODELTAP_SLOTS, g_slotBytes / 1024);
	return g_ctl;
}

static void TapFail(uint32_t fail, const char* why)
{
	static uint32_t s_fail; s_fail = fail;
	CtlWrite([](VPShellModelTapControl* c) { c->state = VPMTAP_FAILED; c->failReason = s_fail; });
	ULOG("model_filter: LOUD: the tap failed (%s): nothing is dropped from now on, the model filter is OFF", why);
	MTurnOff("tap failed", VPMFILTER_R_TAP_FAILED);
}

// the identity set: copied whole or not at all; any failure leaves no set (no draw dropped) until a good read
static uint32_t ApplySet(uint32_t spid, uint32_t gen)
{
	g_setReads++;
	wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELSET_FMT, GetCurrentProcessId());
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, n);
	if (!h) return VPMSET_OPEN;
	uint32_t res = VPMSET_OK;
	const uint8_t* v = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	SetTable* t = nullptr;
	SetEntry* tmpKeys = nullptr;
	do
	{
		if (!v) { res = VPMSET_OPEN; break; }
		MEMORY_BASIC_INFORMATION mbi = {};
		VirtualQuery(v, &mbi, sizeof(mbi));
		const size_t size = mbi.RegionSize;
		if (size < sizeof(VPModelSetHeader)) { res = VPMSET_BOUNDS; break; }
		const VPModelSetHeader* hd = (const VPModelSetHeader*)v;
		const uint32_t seq0 = hd->seq;
		MemoryBarrier();
		if (seq0 & 1) { res = VPMSET_TORN; break; }
		if (hd->magic != VPSHELL_MODELSET_MAGIC) { res = VPMSET_MAGIC; break; }
		if (hd->version != VPSHELL_MODELSET_VERSION || hd->headerBytes != sizeof(VPModelSetHeader) || hd->keyBytes != sizeof(VPModelSetKey)) { res = VPMSET_VERSION; break; }
		if (hd->serverPid != spid) { res = VPMSET_PID; break; }
		if (hd->generation != gen) { res = VPMSET_GENERATION; break; }
		const uint32_t count = hd->keyCount, off = hd->keyOffset;
		if (count > VPSHELL_MODELSET_MAX_KEYS || off < sizeof(VPModelSetHeader) || off % 16 || (uint64_t)off + (uint64_t)count * sizeof(VPModelSetKey) > size)
			{ res = VPMSET_BOUNDS; break; }
		const uint64_t want = hd->fnv1a64;
		VPModelSetKey* keys = (VPModelSetKey*)malloc((size_t)count * sizeof(VPModelSetKey) + 1);
		if (!keys) { res = VPMSET_NOMEM; break; }
		memcpy(keys, v + off, (size_t)count * sizeof(VPModelSetKey));
		MemoryBarrier();
		if (hd->seq != seq0) { free(keys); res = VPMSET_TORN; break; }
		if (VPModelHash64(keys, (size_t)count * sizeof(VPModelSetKey)) != want) { free(keys); res = VPMSET_HASH; break; }
		uint32_t cap = 16;
		while (cap < 2 * count) cap *= 2;
		t = (SetTable*)calloc(1, sizeof(SetTable));
		tmpKeys = t ? (SetEntry*)calloc(cap, sizeof(SetEntry)) : nullptr;
		if (!t || !tmpKeys) { free(keys); free(t); t = nullptr; free(tmpKeys); tmpKeys = nullptr; res = VPMSET_NOMEM; break; }
		t->e = tmpKeys; t->cap = cap; t->generation = gen; t->serverPid = spid;
		for (uint32_t i = 0; i < count; ++i)
		{
			const VPModelSetKey& k = keys[i];
			uint32_t j = (uint32_t)VPModelHash64(&k.key, sizeof(k.key)) & (cap - 1);
			while (t->e[j].used && memcmp(&t->e[j].key, &k.key, sizeof(k.key))) j = (j + 1) & (cap - 1);
			SetEntry& se = t->e[j];
			if (!se.used) { se.key = k.key; se.used = 1; se.cls = k.cls; t->count++; }
			else if (se.cls != k.cls) se.cls = VPMODEL_CLASS_NONE;
			se.bits |= k.bits & (VPMODE_CITIES | VPMODE_LANDMARKS);
		}
		free(keys);
	} while (0);
	if (v) UnmapViewOfFile(v);
	CloseHandle(h);
	SetTable* old = (SetTable*)InterlockedExchangePointer((PVOID volatile*)&g_set, res == VPMSET_OK ? t : nullptr);
	SetRetire(old);
	if (res != VPMSET_OK) SetFree(t);
	return res;
}

static void DropSet()
{
	SetTable* old = (SetTable*)InterlockedExchangePointer((PVOID volatile*)&g_set, nullptr);
	SetRetire(old);
}

// write the frame just ended into its slot, then the control block
static void Publish(const UnitFilterEnv& e, uint32_t latchedBits, uint32_t wantBits)
{
	if (!g_ctl || ModelsOff()) return;
	const int64_t t0 = QpcNow();
	MList& f = g_frame;
	if (!g_dataMap)
	{
		wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELTAP_DATA_FMT, GetCurrentProcessId());
		const uint64_t bytes = (uint64_t)VPSHELL_MODELTAP_SLOTS * g_slotBytes;
		g_dataMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)(bytes >> 32), (DWORD)bytes, n);
		if (!g_dataMap) { char w[96]; _snprintf_s(w, _TRUNCATE, "data section: CreateFileMapping %lu", GetLastError()); TapFail(VPMTAP_FAIL_SECTION, w); return; }
	}
	const uint32_t hdr = sizeof(VPModelTapFrame);
	uint32_t drawOff = hdr, rowOff = drawOff + f.nd * (uint32_t)sizeof(VPModelTapDraw);
	uint32_t constOff = rowOff + f.nr * 16u, targetOff = constOff + f.nk * (uint32_t)sizeof(VPModelTapConst);
	uint32_t total = targetOff + f.nt * (uint32_t)sizeof(VPModelTapTarget);
	uint32_t truncated = 0, nd = f.nd, nr = f.nr, nk = f.nk, nt = f.nt;
	if (total > g_slotBytes || f.nd > VPSHELL_MODELTAP_MAX_DRAWS || f.nk > VPSHELL_MODELTAP_MAX_CONSTS)
	{
		// the per-frame budget keeps this from happening; if it does, the frame goes out empty and its dropped draws are lost
		truncated = f.nd; g_lostDropped += f.dropped[0] + f.dropped[1];
		nd = nr = nk = nt = 0; rowOff = constOff = targetOff = total = hdr; g_framesTruncated++;
		if (!g_lostLogged) { g_lostLogged = true; ULOG("model_filter: LOUD: a frame of %u draws does not fit the %u-KB slot: its dropped draws have no record", f.nd, g_slotBytes / 1024); }
	}
	g_frameSeq++;
	const uint32_t slot = (uint32_t)(g_frameSeq % VPSHELL_MODELTAP_SLOTS);
	const uint64_t at = (uint64_t)slot * g_slotBytes;
	uint8_t* v = (uint8_t*)MapViewOfFile(g_dataMap, FILE_MAP_WRITE, (DWORD)(at >> 32), (DWORD)at, g_slotBytes);
	if (!v) { char w[96]; _snprintf_s(w, _TRUNCATE, "data section: MapViewOfFile %lu", GetLastError()); TapFail(VPMTAP_FAIL_VIEW, w); return; }
	VPModelTapFrame* h = (VPModelTapFrame*)v;
	memset(h, 0, sizeof(*h));
	h->version = VPSHELL_MODELTAP_VERSION; h->headerBytes = hdr; h->drawBytes = sizeof(VPModelTapDraw);
	h->frameSeq = g_frameSeq; h->presentCount = (uint64_t)e.presentCount;
	h->drawCount = nd; h->rowCount = nr; h->constCount = nk; h->targetCount = nt;
	h->drawOffset = drawOff; h->rowOffset = rowOff; h->constOffset = constOff; h->targetOffset = targetOff;
	h->imageBytes = total;
	h->setGeneration = g_set ? g_set->generation : 0;
	h->latchedBits = latchedBits; h->wantBits = wantBits;
	h->dropped[0] = nd ? f.dropped[0] : 0; h->dropped[1] = nd ? f.dropped[1] : 0; h->passedTapped = nd ? f.passedTapped : 0; h->truncated = truncated;
	if (nd) memcpy(v + drawOff, f.d, (size_t)nd * sizeof(VPModelTapDraw));
	if (nr) memcpy(v + rowOff, f.r, (size_t)nr * 16);
	if (nk) memcpy(v + constOff, f.k, (size_t)nk * sizeof(VPModelTapConst));
	if (nt) memcpy(v + targetOff, f.t, (size_t)nt * sizeof(VPModelTapTarget));
	h->fnv1a64 = VPModelHash64(v + hdr, total - hdr);
	MemoryBarrier();
	h->magic = VPSHELL_MODELTAP_MAGIC;
	const uint64_t fnv = h->fnv1a64;
	UnmapViewOfFile(v);
	g_published++;
	const uint64_t ack = g_ctl->ackFrameSeq;
	if (ack + VPSHELL_MODELTAP_SLOTS < g_frameSeq && g_ctl->ackServerPid) g_framesUnacked++;
	static uint64_t s_seq, s_fnv, s_pc; static uint32_t s_slot, s_bytes, s_gen, s_keys, s_res, s_reads, s_trunc, s_unacked; static float s_pub;
	s_seq = g_frameSeq; s_fnv = fnv; s_pc = (uint64_t)e.presentCount; s_slot = slot; s_bytes = total;
	s_gen = g_set ? g_set->generation : 0; s_keys = g_set ? g_set->count : 0; s_res = g_setResult; s_reads = g_setReads;
	s_trunc = g_framesTruncated; s_unacked = g_framesUnacked;
	s_pub = (float)((double)(QpcNow() - t0) * 1e6 / (double)(g_freq ? g_freq : 1));
	CtlWrite([](VPShellModelTapControl* c) {
		c->frameSeq = s_seq; c->slot = s_slot; c->frameBytes = s_bytes; c->fnv1a64 = s_fnv; c->presentCount = s_pc;
		LARGE_INTEGER q; QueryPerformanceCounter(&q); c->publishedQpc = q.QuadPart;
		c->setGenerationApplied = s_gen; c->setKeysApplied = s_keys; c->setResult = s_res; c->setReads = s_reads;
		c->framesTruncated = s_trunc; c->framesUnacked = s_unacked; c->publishUs = s_pub; });
	InterlockedExchangeAdd64(&g_mPubQpc, QpcNow() - t0);
}

// filterState2's half for one bit (filterState's encoding), in filterState's order (issue 4761)
static uint32_t ModelState(const UnitFilterEnv& e, uint32_t bit, bool asks)
{
	if (e.modelMode == UF_MODE_OFF) return VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_MODE_OFF);
	if (ModelsOff())
	{
		const LONG r = g_exc ? (LONG)VPFILTER_R_EXCEPTION : g_moffReason;
		return VPFILTER_WORD(VPFILTER_CANNOT_DROP, r > 0 && r < (LONG)VPMFILTER_R_COUNT ? (uint32_t)r : VPFILTER_R_EXCEPTION);
	}
	const bool hooked = g_nCtx > 0 || g_untracked > 0;
	if ((!e.hookModels || !e.hookUnits || !hooked) && g_presents > kHookGracePresents) return VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_NO_HOOKS);
	bool anyDrop = false;
	for (int k = 0; k < VPMODEL_DROP_PAIRS; ++k)
	{
		const VPModelPair& p = kVPModelDropPairs[k];
		if (p.bit && p.bit != bit) continue;
		const int ps = UF_MODEL0 + p.ps;
		if (g_marked[ps] || g_resolved[ps]) anyDrop = true;
	}
	if (!anyDrop)
		return VPFILTER_WORD(VPFILTER_CANNOT_DROP, !strcmp(g_mpath2, "not-run") ? VPFILTER_R_PATH2_NOT_RUN : VPFILTER_R_NO_DROP_PROGRAM);
	if (asks && !g_set && !g_mSetAll) return VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPMFILTER_R_NO_SET);
	if (!hooked) return VPFILTER_NOT_REPORTED;
	return VPFILTER_CAN_DROP;
}

static const char* const kMReasonNames[VPMFILTER_R_COUNT] = { "-", "model path 2 refused", "model path 2 skipped (EXE image refused)", "an exception in a detour",
	"no DROP program recognised", "model_filter=off", "the test table refused", "the mark table full", "no draw reached the hooks",
	"no identity set of this server", "the tap failed" };

// every Present: the identity set, the frame's publication, the latch of bits 1 and 2 for the next frame, filterState2
static void ModelOnPresent(const UnitFilterEnv& e, uint32_t word, bool live, uint32_t spid, VPShellMode* m)
{
	MaybeMFault(MFF_PRESENT);
	g_presentNo++;
	VPShellModelTapControl* ctl = ModelsOff() ? g_ctl : EnsureControl(e);
	const uint32_t want = live && ctl ? (ctl->wantBits & (VPMODE_CITIES | VPMODE_LANDMARKS)) : 0;
	const uint32_t setGen = live && ctl ? ctl->setGeneration : 0;
	// the identity set: only the live server's, only its current generation
	SetTable* cur = g_set;
	if (cur && (!live || cur->serverPid != spid || cur->generation != setGen)) { DropSet(); cur = nullptr; }
	if (live && setGen && !cur && !ModelsOff())
	{
		g_setResult = ApplySet(spid, setGen);
		if (g_setResult != g_lastSetResultLogged || setGen != g_lastSetGenTried)
		{
			ULOG("model_filter: identity set generation %u of server %u: %s%s%u keys", setGen, spid, kSetResultNames[g_setResult < VPMSET_RESULT_COUNT ? g_setResult : 0],
			     g_setResult == VPMSET_OK ? ", " : " (no draw is dropped until a good read), ", g_set ? g_set->count : 0u);
			g_lastSetResultLogged = g_setResult; g_lastSetGenTried = setGen;
		}
	}
	// the frame just ended: its last immediate draws, then the publication (only while something is tapped)
	const uint32_t frameBits = (uint32_t)g_mDropBits, frameWant = (uint32_t)g_mTapBits;
	Place(g_immList);
	if (g_frame.nd || g_mTapBits || g_mTapAll) Publish(e, frameBits, frameWant);
	g_lostDropped += g_frame.lost;
	if (g_lostDropped && !g_lostLogged) { g_lostLogged = true; ULOG("model_filter: LOUD: %u dropped draws lost their records (no memory or the slot)", g_lostDropped); }
	if (g_placeMiss && !g_missLogged) { g_missLogged = true; ULOG("model_filter: LOUD: %ld rows of deferred records could not be resolved at their placement (written as 0)", g_placeMiss); }
	static bool s_holdFullLogged;   // ticket 0788
	if (g_holdFull && !s_holdFullLogged) { s_holdFullLogged = true; ULOG("model_filter: LOUD: the hold table had no free entry for a key (%ld so far): such a key is dropped without its one-frame hold", g_holdFull); }
	ListClear(&g_frame);
	ReadBackPresent(e.dev, g_presentNo);   // the read-back of key parts missing from the creation table
	GeomPresent(e.dev, g_presentNo);       // ticket 4879: the CPU-built meshes' copies into the geometry section
	// command lists never executed (8 Presents old): their records are freed (their draws were never drawn either)
	AcquireSRWLockExclusive(&g_clLock);
	for (int i = 0; i < kClRecs; ++i)
		if (g_clRecs[i].cl && g_presentNo - g_clRecs[i].present > 8) { ListFree(g_clRecs[i].list); g_clRecs[i].cl = nullptr; g_clRecs[i].list = nullptr; InterlockedIncrement(&g_clStale); }
	ReleaseSRWLockExclusive(&g_clLock);
	for (int i = 0; i < 8; ++i) if (g_retire[i] && g_presentNo - g_retireAt[i] > 8) { SetFree(g_retire[i]); g_retire[i] = nullptr; }
	// the latch for the next frame
	const bool on = !ModelsOff() && e.modelMode != UF_MODE_OFF && e.hookModels && e.hookUnits;
	const uint32_t drop = on ? (e.modelMode == UF_MODE_ON ? (VPMODE_CITIES | VPMODE_LANDMARKS) : (word & (VPMODE_CITIES | VPMODE_LANDMARKS))) : 0u;
	InterlockedExchange(&g_mDropBits, (LONG)drop);
	InterlockedExchange(&g_mTapBits, on ? (LONG)(drop | want) : 0);
	InterlockedExchange(&g_mTapAll, on && e.modelTapPassed ? 1 : 0);
	InterlockedExchange(&g_mActive, (drop | want) || (on && e.modelTapPassed) ? 1 : 0);
	InterlockedExchange(&g_budgetLeft, (LONG)(g_slotBytes ? g_slotBytes / 2 : (uint32_t)e.modelSlotKb * 512u));
	// filterState2 per bit; LOUD once per bit when its latch asks for drops and the filter cannot drop
	uint32_t w2 = 0;
	const uint32_t bits[2] = { VPMODE_CITIES, VPMODE_LANDMARKS };
	const char* names[2] = { "VPMODE_CITIES", "VPMODE_LANDMARKS" };
	for (int b = 0; b < 2; ++b)
	{
		const bool asks = e.modelMode == UF_MODE_ON || (word & bits[b]);
		if (word & bits[b]) g_mFramesSet[b]++;
		const uint32_t st = ModelState(e, bits[b], asks);
		w2 |= st << (b ? VPFILTER2_LANDMARKS_SHIFT : VPFILTER2_CITIES_SHIFT);
		const bool cannot = (st & VPFILTER_STATE_MASK) == VPFILTER_CANNOT_DROP;
		const uint32_t r = (st & VPFILTER_REASON_MASK) >> VPFILTER_REASON_SHIFT;
		if (e.modelMode != UF_MODE_OFF && asks && cannot)
		{
			g_mFramesUnable[b]++;
			if (!g_mUnableLogged[b])
			{
				g_mUnableLogged[b] = true;
				ULOG("model_filter: LOUD: the latch asks for drops (%s) but the model filter cannot drop the EXE's %s draws: %s; they are drawn "
				     "by the EXE as well (filterState2 0x%X)", e.modelMode == UF_MODE_ON ? "model_filter=on" : names[b], b ? "landmark" : "city",
				     r == VPMFILTER_R_NO_SET ? "no identity set of this server is applied" : g_mUnableWhy[0] ? g_mUnableWhy : kMReasonNames[r < VPMFILTER_R_COUNT ? r : 0], w2);
			}
		}
	}
	if (w2 != g_filterState2)
	{
		ULOG("model_filter: filterState2 0x%X (cities: %s%s; landmarks: %s%s) at present %lld", w2,
		     (w2 & 0xFF) == VPFILTER_CAN_DROP ? "can drop" : (w2 & 0xFF) == VPFILTER_CANNOT_DROP ? "cannot drop: " : "not reported yet",
		     (w2 & 0xFF) == VPFILTER_CANNOT_DROP ? kMReasonNames[((w2 >> 8) & 0xFF) < VPMFILTER_R_COUNT ? (w2 >> 8) & 0xFF : 0] : "",
		     ((w2 >> 16) & 0xFF) == VPFILTER_CAN_DROP ? "can drop" : ((w2 >> 16) & 0xFF) == VPFILTER_CANNOT_DROP ? "cannot drop: " : "not reported yet",
		     ((w2 >> 16) & 0xFF) == VPFILTER_CANNOT_DROP ? kMReasonNames[((w2 >> 24) & 0xFF) < VPMFILTER_R_COUNT ? (w2 >> 24) & 0xFF : 0] : "", e.presentCount);
		g_filterState2 = w2;
	}
	if (m)
	{
		uint32_t dc = 0, dl = 0, pc = 0, pl = 0;
		LONG n = g_nCtx;
		for (LONG i = 0; i < n; ++i) { dc += g_ctxs[i].mc[MC_DROP_C]; dl += g_ctxs[i].mc[MC_DROP_L]; pc += g_ctxs[i].mc[MC_PSET_C]; pl += g_ctxs[i].mc[MC_PSET_L]; }
		VPMODE_FILTERSTATE2(m) = w2;   // before latchedWord (written after this call): a reader that sees a bit sees a state at least as new
		VPMODE_DROPPED_CITIES(m) = dc; VPMODE_DROPPED_LANDMARKS(m) = dl; VPMODE_PASSEDSET_CITIES(m) = pc; VPMODE_PASSEDSET_LANDMARKS(m) = pl;
	}
}

static void SafeModelOnPresent(const UnitFilterEnv& e, uint32_t word, bool live, uint32_t spid, VPShellMode* m)
{
	__try { ModelOnPresent(e, word, live, spid, m); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("Present (models)", GetExceptionCode()); }
}

// ===========================================================================
// Present: the latch
// ===========================================================================
static void Totals(uint32_t& dropped, uint32_t& passedSet, uint32_t& droppedMask, uint32_t& passedSetMask)
{
	dropped = passedSet = droppedMask = passedSetMask = 0;
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i)
	{
		for (int p = 0; p < UF_PROGS; ++p)
		{
			if (g_isDropPs[p]) dropped += g_ctxs[i].dropped[p];
			if (g_isMaskPs[p]) droppedMask += g_ctxs[i].dropped[p];   // ticket 4785
		}
		passedSet += g_ctxs[i].passedSet;
		passedSetMask += g_ctxs[i].passedSetMask;
	}
}

int UnitFilter_UnitMaskDropLatched(void) { return g_maskDropNow ? 1 : 0; }   // ticket 4785

// issue 4761: the names of VPFILTER_R_* (shell_shared.h) in the composite's lines; the server has its own copy
static const char* const kFilterReasonNames[VPFILTER_R_COUNT] = { "-", "path 2 refused", "path 2 not run", "an exception in a detour",
	"no DROP program recognised", "unit_filter=off", "the test table refused", "the mark table full", "no draw reached the hooks" };

// issue 4761: the filterState word (VPFILTER_WORD) at this Present. Cannot drop, in this order: unit_filter=off; the
// filter off for the process (its first reason); no hooked draw or bind since the DLL started, after the first
// kHookGracePresents Presents (hook_units=0, or the hooks not in place); no DROP program recognised (path 2 not run, or
// pending, and no creation matched). Not reported while a DROP program is known but no hooked call has come yet (the
// first Presents, as the hooks go in). Otherwise can drop.
static uint32_t FilterState(const UnitFilterEnv& e)
{
	if (e.mode == UF_MODE_OFF) return VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_MODE_OFF);
	if (g_off)
	{
		const LONG r = g_offReason;
		return VPFILTER_WORD(VPFILTER_CANNOT_DROP, r > 0 && r < (LONG)VPFILTER_R_COUNT ? (uint32_t)r : VPFILTER_R_EXCEPTION);
	}
	const bool hooked = g_nCtx > 0 || g_untracked > 0;
	if (!hooked && g_presents > kHookGracePresents) return VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_NO_HOOKS);
	bool anyDrop = false;
	for (int p = 0; p < UF_PROGS; ++p) if (g_isDropPs[p] && (g_marked[p] || g_resolved[p])) anyDrop = true;
	if (!anyDrop)
		return VPFILTER_WORD(VPFILTER_CANNOT_DROP, g_path2Done && !strcmp(g_path2, "not-run") ? VPFILTER_R_PATH2_NOT_RUN : VPFILTER_R_NO_DROP_PROGRAM);
	if (!hooked) return VPFILTER_NOT_REPORTED;
	return VPFILTER_CAN_DROP;
}

static void OnPresent(const UnitFilterEnv& e)
{
	MaybeFault(UFF_PRESENT);
	g_presents++;
	if (e.mode != g_lastMode)
	{
		if (e.mode != UF_MODE_AUTO)
			ULOG("unit_filter: unit_filter=%s - TEST ONLY: %s", kUnitFilterModeNames[e.mode],
			     e.mode == UF_MODE_ON ? "the table's pairs are dropped whatever the mode word says" : "nothing is dropped whatever the mode word says");
		else if (g_lastMode >= 0) ULOG("unit_filter: unit_filter=auto (the mode word decides)");
		g_lastMode = e.mode;
	}
	if (!g_path2Done)
	{
		if (e.exeValidated > 0 && e.dev)
		{
			RunPath2(e);
			if (!ModelsOff()) RunModelPath2(e);   // ticket 4874: the models' group, on its own
		}
		else if (e.exeValidated < 0)
		{
			g_path2Done = true;
			g_path2 = "not-run";
			ULOG("unit_filter: path 2 not run: the composite refused the EXE image (camera validation); only objects created from "
			     "now on can be recognised (path 1)");
			strcpy_s(g_unableWhy, "path 2 not run: the composite refused the EXE image (camera validation)");
			g_mpath2 = "not-run";
			strcpy_s(g_mUnableWhy, "model path 2 skipped: the composite refused the EXE image (camera validation)");
		}
	}
	int reason = UFL_LIVE;
	uint32_t raw = 0, liveSpid = 0;
	VPShellMode* m = nullptr;
	if (!e.hdr) reason = UFL_NOSECTION;
	else
	{
		m = VPSHELL_MODE(e.hdr);
		raw = m->word;
		MemoryBarrier();
		const uint32_t wpid = m->writerPid;
		// The server's liveness (issue 4761, correction of 4752 found live by 4757 #4248): with handover=ring the ring sink
		// writes magic, serverPid, state and heartbeatQpc only into the frame section's server block and the main header's
		// magic stays 0, so ring reads that block (the fields ring_source.cpp's serverOk reads, plus the heartbeat); v3 reads
		// the main header as before.
		uint32_t magic = 0, spid = 0, srvState = VPFS_READY;
		volatile int64_t* hbAt = nullptr;
		bool magicOk = false;
		if (e.ring)
		{
			if (!e.frameSrv) reason = UFL_NOSECTION;
			else
			{
				magic = e.frameSrv->magic;
				MemoryBarrier();
				magicOk = magic == VPFRAME_MAGIC && e.frameSrv->version == VPFRAME_VERSION;
				spid = e.frameSrv->serverPid;
				srvState = e.frameSrv->state;
				hbAt = const_cast<volatile int64_t*>(&e.frameSrv->heartbeatQpc);
			}
		}
		else
		{
			magic = e.hdr->magic;
			magicOk = magic == VPSHELL_MAGIC;
			spid = e.hdr->serverPid;
			hbAt = &e.hdr->heartbeatQpc;
		}
		if (reason == UFL_NOSECTION) {}   // ring: the frame section not mapped yet
		else if (!magicOk) reason = UFL_NOTINIT;
		else if (!spid || wpid != spid) reason = UFL_PID;
		else if (srvState == VPFS_FAILED || srvState == VPFS_STOPPED) reason = UFL_SRVSTATE;
		else
		{
			LARGE_INTEGER now; QueryPerformanceCounter(&now);
			const int64_t hb = InterlockedCompareExchange64((volatile LONGLONG*)hbAt, 0, 0);
			if (!e.qpcFreq || (double)(now.QuadPart - hb) * 1000.0 / (double)e.qpcFreq > (double)e.staleMs) reason = UFL_STALE;
			else if (!e.compositeOn) reason = UFL_NOTCOMPOSITE;
		}
		liveSpid = spid;
	}
	const uint32_t word = reason == UFL_LIVE ? raw : 0;
	if (reason != UFL_LIVE && (raw & VPMODE_UNITS)) g_ignored[reason]++;
	const bool set = (word & VPMODE_UNITS) != 0;
	const bool drop = !g_off && (e.mode == UF_MODE_ON || (e.mode == UF_MODE_AUTO && set));
	// ticket 4785: the mask pairs only under both bits of a live server's word (unit_filter=on alone does not drop them)
	const bool maskSet = set && (word & VPMODE_UNIT_SHADOWS) != 0;
	const bool maskDrop = drop && maskSet;
	InterlockedExchange(&g_setNow, set ? 1 : 0);
	InterlockedExchange(&g_dropNow, drop ? 1 : 0);
	InterlockedExchange(&g_maskSetNow, maskSet ? 1 : 0);
	InterlockedExchange(&g_maskDropNow, maskDrop ? 1 : 0);
	LARGE_INTEGER q; QueryPerformanceCounter(&q); InterlockedExchange64(&g_latchQpc, q.QuadPart);
	if (set) g_framesSet++;
	if (drop) g_framesDrop++;
	if (maskDrop) g_framesMaskDrop++;
	// Whether the filter can drop (issue 4761, D118): the mode block's filterState, written below before latchedWord.
	const uint32_t fstate = FilterState(e);
	const bool cannot = (fstate & VPFILTER_STATE_MASK) == VPFILTER_CANNOT_DROP;
	const uint32_t freason = (fstate & VPFILTER_REASON_MASK) >> VPFILTER_REASON_SHIFT;
	if (fstate != g_filterState)
	{
		ULOG("unit_filter: filterState 0x%X (%s%s%s) at present %lld", fstate,
		     cannot ? "cannot drop: " : (fstate & VPFILTER_STATE_MASK) == VPFILTER_CAN_DROP ? "can drop" : "not reported yet",
		     cannot ? kFilterReasonNames[freason < VPFILTER_R_COUNT ? freason : 0] : "",
		     cannot && set ? "; the server's VPMODE_UNITS is latched: the server takes the D125 step (D118)" : "", e.presentCount);
		g_filterState = fstate;
	}
	// The latch asks for drops but nothing can be dropped (the filter off, no DROP program recognised, no hook reached):
	// the server and the EXE would both draw the units. LOUD once, counted every frame (this is where path 2's refusal
	// bites). unit_filter=off (TEST ONLY) asks for no drops, so it is never counted here.
	if (e.mode != UF_MODE_OFF && (e.mode == UF_MODE_ON || set) && cannot)
	{
		g_framesUnable++;
		if (!g_unableLogged)
		{
			g_unableLogged = true;
			ULOG("unit_filter: LOUD: the latch asks for drops (%s) but the unit filter cannot drop the EXE's pass-0 unit draws: %s; "
			     "the EXE's units are drawn as well (filterState 0x%X)", e.mode == UF_MODE_ON ? "unit_filter=on" : "the server's VPMODE_UNITS",
			     g_off ? (!strncmp(g_offWhy, "path 2", 6) && g_unableWhy[0] ? g_unableWhy : g_offWhy)
			     : freason == VPFILTER_R_NO_HOOKS ? "no draw or shader bind has reached the filter's hooks (hook_units=0, or the hooks are not in place)"
			     : g_unableWhy[0] ? g_unableWhy : "no program of the drop table is recognised (no creation matched, path 2 has not run)",
			     fstate);
		}
	}
	if (word != g_lastLatched || ((raw & VPMODE_UNITS) && reason != g_lastReason))
	{
		ULOG("unit_filter: latched word 0x%X (raw 0x%X, server %s) at present %lld: %s", word, raw, kLiveNames[reason], e.presentCount,
		     maskDrop ? "the units' pass-0 and shadow-mask draws are dropped" : drop ? "the units' pass-0 draws are dropped"
		     : g_off ? "the filter is OFF, every draw passes" : "every draw passes");
		g_lastLatched = word;
		g_lastReason = reason;
	}
	// ticket 4874: the model filter's Present (bits 1 and 2 of the same latch, the identity set, the tap's frame, filterState2,
	// written before latchedWord below)
	SafeModelOnPresent(e, word, reason == UFL_LIVE, liveSpid, m);
	if (m)
	{
		uint32_t dropped, passedSet, droppedMask, passedSetMask;
		Totals(dropped, passedSet, droppedMask, passedSetMask);
		m->filterState = fstate;   // before latchedWord: a reader that sees the latched bit sees a state at least as new
		m->droppedUnitMask = droppedMask;          // ticket 4785, before latchedWord as filterState
		m->passedSetUnitMask = passedSetMask;
		MemoryBarrier();
		m->latchedWord = word;
		m->latchedPresent = (uint32_t)e.presentCount;
		m->droppedTotal = dropped;
		m->passedSetTotal = passedSet;
	}
}

void UnitFilter_OnPresent(const UnitFilterEnv& env)
{
	g_log = env.log;
	EnsureInit();
	static bool noteLogged;
	if (!noteLogged)
	{
		noteLogged = true;
		ULOG("unit_filter: %d drop programs (%d pairs), %d kept programs; unit_filter=%s%s%s", VPUNIT_DROP_PROGRAMS, VPUNIT_DROP_PAIRS,
		     VPUNIT_KEEP_PROGRAMS, kUnitFilterModeNames[env.mode], g_testTableNote[0] ? "; LOUD: " : "", g_testTableNote);
		if (g_fault) ULOG("unit_filter: unit_filter_fault=%s (TEST ONLY: one access violation in that detour)",
		                  g_fault == UFF_DRAW ? "draw" : g_fault == UFF_SET ? "set" : g_fault == UFF_CREATE ? "create" : "present");
		if (g_mSetAll) ULOG("model_filter: LOUD: model_filter_set=all - TEST ONLY: every fully hashed city and landmark draw counts as in "
		                    "the identity set (dropped without a server to draw it)");
		if (g_mfault) ULOG("model_filter: model_filter_fault=%d (TEST ONLY: one access violation in that model path)", g_mfault);
		if (g_mapCacheNote[0]) ULOG("model_filter: %s", g_mapCacheNote);   // ticket 0719
		if (!g_mHold) ULOG("model_filter: C5_SHELL_MODEL_HOLD=0 - DIAGNOSTIC: a draw is dropped the first frame its key is droppable (ticket 0788's one-frame holes come back)");
		if (g_mapCache != 1)
			ULOG("model_filter: model_map_cache=%s", g_mapCache == 0 ? "0 (every Map hands the EXE the driver's memory; the copy reads it there)"
			                                                       : "verify - TEST ONLY: the forwarded pages are read back from the driver's memory and compared");
			if (g_tailNote[0]) ULOG("model_filter: %s", g_tailNote);   // ticket 0758
			ULOG("model_filter: tail read: %s (this CPU: %s)%s", kTailReadNames[g_tailLevel], kTailReadNames[g_tailCpu],
			     g_tailVerify ? "; model_tail_stream=verify - TEST ONLY: every tail run is read again with memcpy and compared"
			     : g_tailKeySet ? "; model_tail_stream - TEST ONLY: the level forced" : "");
		if (g_geomTestFull || g_geomTestMaxBytes || g_geomTestChanged)
			ULOG("model_filter: LOUD: model_geom_test=%s - TEST ONLY: the geometry section %s", g_geomTestFull ? "full" : g_geomTestChanged ? "changed" : "oversize",
			     g_geomTestFull ? "never has room" : g_geomTestChanged ? "reads every copy as changed" : "refuses buffers over the test size");
		if (g_geomTestBytes)   // ticket 0597
			ULOG("model_filter: LOUD: model_geom_test=bytes:%u - TEST ONLY: the geometry section holds %u bytes of data", g_geomTestBytes,
			     g_geomDataBytes);
	}
	__try { OnPresent(env); }
	__except (EXCEPTION_EXECUTE_HANDLER) { UfAsanAfterSeh(); OnException("Present", GetExceptionCode()); }
}

void UnitFilter_ControlTick(void (*log)(const char* fmt, ...))
{
	if (log && !g_log) g_log = log;
	if ((!g_dropNow && !g_mDropBits) || !g_freq) return;
	LARGE_INTEGER q; QueryPerformanceCounter(&q);
	if ((double)(q.QuadPart - g_latchQpc) * 1000.0 / (double)g_freq > 1000.0)
	{
		InterlockedExchange(&g_mDropBits, 0);   // ticket 4874: the model bits as the unit bit
		if (!g_dropNow) return;
		InterlockedExchange(&g_dropNow, 0);
		InterlockedExchange(&g_setNow, 0);
		InterlockedExchange(&g_maskDropNow, 0);   // ticket 4785
		InterlockedExchange(&g_maskSetNow, 0);
		g_clearedByTick++;
		ULOG("unit_filter: no Present of the target for 1 s: the latch is cleared (every draw passes until the next Present)");
	}
}

// ===========================================================================
// The 1 s line and the totals
// ===========================================================================
struct UfSum
{
	uint32_t entry[UF_E_COUNT], seen[UF_PROGS], dropped[UF_PROGS], passed[UF_PROGS], passedSet, mismatch;
	uint32_t created[UF_PROGS], marked[UF_PROGS], resolved[UF_PROGS], refused[UF_PROGS];
	uint32_t ignored[UFL_COUNT], framesSet, framesDrop, presents, untracked, hashed, unable;
	uint32_t framesMaskDrop, passedSetMask;   // ticket 4785
};

static void Sum(UfSum& s)
{
	memset(&s, 0, sizeof(s));
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i)
	{
		const UfCtx& c = g_ctxs[i];
		for (int e = 0; e < UF_E_COUNT; ++e) s.entry[e] += c.entry[e];
		for (int p = 0; p < UF_PROGS; ++p) { s.seen[p] += c.seen[p]; s.dropped[p] += c.dropped[p]; s.passed[p] += c.passed[p]; }
		s.passedSet += c.passedSet;
		s.mismatch += c.mismatch;
		s.passedSetMask += c.passedSetMask;
	}
	s.framesMaskDrop = g_framesMaskDrop;
	for (int p = 0; p < UF_PROGS; ++p) { s.created[p] = g_created[p]; s.marked[p] = g_marked[p]; s.resolved[p] = g_resolved[p]; s.refused[p] = g_refused[p]; }
	memcpy(s.ignored, g_ignored, sizeof(s.ignored));
	s.framesSet = g_framesSet; s.framesDrop = g_framesDrop; s.presents = g_presents; s.untracked = (uint32_t)g_untracked; s.hashed = (uint32_t)g_createsHashed;
	s.unable = g_framesUnable;
}

static void LogSum(void (*log)(const char*, ...), const char* what, const UfSum& s, double el)
{
	uint32_t dropped = 0, droppedMask = 0;
	for (int p = 0; p < UF_PROGS; ++p)
	{
		if (g_isDropPs[p]) dropped += s.dropped[p];
		if (g_isMaskPs[p]) droppedMask += s.dropped[p];   // ticket 4785
	}
	char progs[1200] = "";
	for (int p = 0; p < UF_UNIT_PROGS; ++p)
	{
		char one[96];
		_snprintf_s(one, _TRUNCATE, "%s%s=c%u/m%u/r%u/f%u/s%u/d%u/p%u", p ? " " : "", g_short[p], s.created[p], s.marked[p], s.resolved[p],
		            s.refused[p], s.seen[p], s.dropped[p], s.passed[p]);
		strcat_s(progs, one);
	}
	const uint32_t fs = g_filterState == 0xFFFFFFFFu ? VPFILTER_NOT_REPORTED : g_filterState;
	log("unitfilter %s: el=%.0fms mode=%s latched=0x%X drop_now=%ld off=%ld%s%s path2=%s presents=%u frames_set=%u frames_drop=%u frames_unable=%u filter_state=%u filter_reason=%u "
	    "ignored_nosection=%u ignored_notinit=%u ignored_pid=%u ignored_stale=%u ignored_notcomposite=%u ignored_srvstate=%u dropped=%u passed_set=%u mismatch=%u "
	    "untracked=%u ctx=%ld hashed=%u dii=%u di=%u d=%u dinst=%u diiind=%u dinstind=%u dauto=%u mask_drop_now=%ld frames_mask_drop=%u "
	    "dropped_mask=%u passed_set_mask=%u progs %s",
	    what, el, g_lastMode >= 0 ? kUnitFilterModeNames[g_lastMode] : "-", g_lastLatched == 0xFFFFFFFFu ? 0u : g_lastLatched, g_dropNow, g_off,
	    g_off ? " off_why=" : "", g_off ? g_offWhy : "", g_path2, s.presents, s.framesSet, s.framesDrop, s.unable,
	    fs & VPFILTER_STATE_MASK, (fs & VPFILTER_REASON_MASK) >> VPFILTER_REASON_SHIFT,
	    s.ignored[UFL_NOSECTION], s.ignored[UFL_NOTINIT], s.ignored[UFL_PID], s.ignored[UFL_STALE], s.ignored[UFL_NOTCOMPOSITE], s.ignored[UFL_SRVSTATE],
	    dropped, s.passedSet, s.mismatch, s.untracked, g_nCtx, s.hashed,
	    s.entry[UF_E_DII], s.entry[UF_E_DI], s.entry[UF_E_D], s.entry[UF_E_DINST], s.entry[UF_E_DIIIND], s.entry[UF_E_DINSTIND], s.entry[UF_E_DAUTO],
	    g_maskDropNow, s.framesMaskDrop, droppedMask, s.passedSetMask, progs);
}

// ticket 4874: "modelfilter 1s:" and "modelfilter totals:" (the model counters, the tap's, per model program)
struct MfSum { uint32_t mc[MC_COUNT]; uint32_t bufInit, bufNoInit, texInit, texNoInit, texNoId, published, setReads, truncated, unacked,
	placeMiss, budgetFail, omFull, shFull, intFull, clFull, clStale, resFull, rbNew, rbHashed, rbFailed, rbFull, rbNotKeyable, rbPending, writeCleared,
	mapCached, mapDirect, stageFail, wwFail, verifyBad, latePages, recDiff, recNoOld,   // ticket 0719
	tailDiff;   // ticket 0758
	uint64_t rbBytes, hashBytes, hashQpc, copyBytes, copyQpc, drawQpc, pubQpc, fwdBytes, fwdQpc, verifyBytes, unwrittenBytes, recBytes, tailBytes,
	tailQpc, tailVBytes; };   // ticket 0758
static void MSum(MfSum& s)
{
	memset(&s, 0, sizeof(s));
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i) for (int k = 0; k < MC_COUNT; ++k) s.mc[k] += g_ctxs[i].mc[k];
	s.bufInit = g_bufInit; s.bufNoInit = g_bufNoInit; s.texInit = g_texInit; s.texNoInit = g_texNoInit; s.texNoId = g_texNoId;
	s.published = g_published; s.setReads = g_setReads; s.truncated = g_framesTruncated; s.unacked = g_framesUnacked;
	s.placeMiss = g_placeMiss; s.budgetFail = g_budgetFail; s.omFull = g_omFull; s.shFull = g_shFull; s.intFull = g_intFull; s.clFull = g_clFull;
	s.clStale = g_clStale; s.resFull = g_resFull;
	s.rbNew = g_rbNew; s.rbHashed = g_rbHashed; s.rbFailed = g_rbFailed; s.rbFull = g_rbFull; s.rbNotKeyable = g_rbNotKeyable; s.rbPending = g_rbPending;
	s.writeCleared = g_writeCleared; s.rbBytes = g_rbBytes;
	s.hashBytes = g_hashBytes; s.hashQpc = g_hashQpc; s.copyBytes = g_copyBytes; s.copyQpc = g_copyQpc; s.drawQpc = g_mDrawQpc; s.pubQpc = g_mPubQpc;
	s.mapCached = g_mapCached; s.mapDirect = g_mapDirect; s.stageFail = g_stageFail; s.wwFail = g_wwFail; s.verifyBad = g_verifyBad;
	s.latePages = g_latePages; s.recDiff = g_recDiff; s.recNoOld = g_recNoOld;
	s.fwdBytes = g_fwdBytes; s.fwdQpc = g_fwdQpc; s.verifyBytes = g_verifyBytes; s.unwrittenBytes = g_unwrittenBytes; s.recBytes = g_recBytes; s.tailBytes = g_tailBytes;
	s.tailDiff = g_tailDiff; s.tailQpc = g_tailQpc; s.tailVBytes = g_tailVBytes;   // ticket 0758
}
static void MLog(void (*log)(const char*, ...), const char* what, const MfSum& s, const UfSum& u, double el)
{
	char mc[900] = "";
	for (int k = 0; k < MC_COUNT; ++k) { char one[48]; _snprintf_s(one, _TRUNCATE, " %s=%u", kMcNames[k], s.mc[k]); strcat_s(mc, one); }
	char progs[900] = "";
	for (int p = UF_MODEL0; p < UF_PROGS; ++p)
	{
		char one[96];
		_snprintf_s(one, _TRUNCATE, "%s%s=c%u/m%u/r%u/f%u/s%u/d%u/p%u", p > UF_MODEL0 ? " " : "", g_short[p], u.created[p], u.marked[p], u.resolved[p],
		            u.refused[p], u.seen[p], u.dropped[p], u.passed[p]);
		strcat_s(progs, one);
	}
	const double f = (double)(g_freq ? g_freq : 1);
	const uint32_t fs2 = g_filterState2 == 0xFFFFFFFFu ? 0u : g_filterState2;
	const SetTable* t = g_set;
	log("modelfilter %s: el=%.0fms drop_bits=0x%lX tap_bits=0x%lX tap_all=%ld off=%ld%s%s path2=%s filter_state2=0x%X set_gen=%u set_keys=%u set_result=%u "
	    "set_reads=%u published=%u frame_seq=%llu truncated=%u unacked=%u frames_unable_c=%u frames_unable_l=%u%s buf_init=%u buf_noinit=%u tex_init=%u "
	    "tex_noinit=%u tex_noid=%u hash_kb=%llu hash_ms=%.2f copy_kb=%llu copy_ms=%.2f draw_ms=%.2f publish_ms=%.2f place_miss=%u budget_fail=%u "
	    "om_full=%u sh_full=%u int_full=%u cl_full=%u cl_stale=%u res_full=%u lost=%u rb_new=%u rb_hashed=%u rb_failed=%u rb_full=%u rb_notkeyable=%u rb_pending=%u rb_kb=%llu write_cleared=%u "
	    "map_cache=%d map_cached=%u map_direct=%u fwd_kb=%llu fwd_ms=%.2f unwritten_kb=%llu tail_kb=%llu stage_kb=%llu stage_fail=%u ww_fail=%u verify_kb=%llu "
	    "verify_bad=%u late_pages=%u rec_kb=%llu rec_diff=%u rec_noold=%u tail_read=%s tail_ms=%.3f tail_vkb=%llu tail_diff=%u progs %s",
	    what, el, g_mDropBits, g_mTapBits, g_mTapAll, (LONG)ModelsOff(), ModelsOff() ? " off_why=" : "", ModelsOff() ? (g_exc ? "exception" : g_moffWhy) : "",
	    g_mpath2, fs2, t ? t->generation : 0u, t ? t->count : 0u, g_setResult, s.setReads, s.published, (unsigned long long)g_frameSeq, s.truncated, s.unacked,
	    g_mFramesUnable[0], g_mFramesUnable[1], mc, s.bufInit, s.bufNoInit, s.texInit, s.texNoInit, s.texNoId,
	    (unsigned long long)(s.hashBytes / 1024), (double)s.hashQpc * 1000.0 / f, (unsigned long long)(s.copyBytes / 1024), (double)s.copyQpc * 1000.0 / f,
	    (double)s.drawQpc * 1000.0 / f, (double)s.pubQpc * 1000.0 / f, s.placeMiss, s.budgetFail, s.omFull, s.shFull, s.intFull, s.clFull, s.clStale,
	    s.resFull, g_lostDropped, s.rbNew, s.rbHashed, s.rbFailed, s.rbFull, s.rbNotKeyable, s.rbPending, (unsigned long long)(s.rbBytes / 1024), s.writeCleared,
	    g_mapCache, s.mapCached, s.mapDirect, (unsigned long long)(s.fwdBytes / 1024), (double)s.fwdQpc * 1000.0 / f, (unsigned long long)(s.unwrittenBytes / 1024), (unsigned long long)(s.tailBytes / 1024),
	    (unsigned long long)(g_stageBytes / 1024), s.stageFail, s.wwFail, (unsigned long long)(s.verifyBytes / 1024), s.verifyBad, s.latePages,
	    (unsigned long long)(s.recBytes / 1024), s.recDiff, s.recNoOld, kTailReadNames[g_tailLevel], (double)s.tailQpc * 1000.0 / f,
	    (unsigned long long)(s.tailVBytes / 1024), s.tailDiff, progs);
}
static void ModelStatsLines(void (*log)(const char*, ...), double el, const UfSum& cur, const UfSum& d)
{
	static MfSum prev;
	MfSum c, dd;
	MSum(c);
	const uint32_t* a = (const uint32_t*)&c;
	const uint32_t* b = (const uint32_t*)&prev;
	uint32_t* o = (uint32_t*)&dd;
	const size_t words32 = offsetof(MfSum, rbBytes) / 4;
	for (size_t i = 0; i < words32; ++i) o[i] = a[i] - b[i];
	dd.rbBytes = c.rbBytes - prev.rbBytes; dd.hashBytes = c.hashBytes - prev.hashBytes; dd.hashQpc = c.hashQpc - prev.hashQpc; dd.copyBytes = c.copyBytes - prev.copyBytes;
	dd.copyQpc = c.copyQpc - prev.copyQpc; dd.drawQpc = c.drawQpc - prev.drawQpc; dd.pubQpc = c.pubQpc - prev.pubQpc;
	dd.fwdBytes = c.fwdBytes - prev.fwdBytes; dd.fwdQpc = c.fwdQpc - prev.fwdQpc; dd.verifyBytes = c.verifyBytes - prev.verifyBytes;   // ticket 0719
	dd.unwrittenBytes = c.unwrittenBytes - prev.unwrittenBytes; dd.recBytes = c.recBytes - prev.recBytes; dd.tailBytes = c.tailBytes - prev.tailBytes;
	dd.tailQpc = c.tailQpc - prev.tailQpc; dd.tailVBytes = c.tailVBytes - prev.tailVBytes;   // ticket 0758
	MLog(log, "1s", dd, d, el);
	MLog(log, "totals", c, cur, el);
	prev = c;
	// ticket 4879: the geometry section's totals (only once a CPU-built mesh was seen)
	if (g_gWanted || g_geomOff)
	{
		const VPModelGeomHeader* h = g_geomHdr;
		log("modelgeom totals: section=%s entries=%u generation=%u wanted=%ld held=%ld pending=%ld evicted=%ld oversize=%ld changed=%ld "
		    "full=%ld failed=%ld notkeyable=%ld map_fail=%ld table_full=%ld copied_kb=%llu copy_ms=%.2f "
		    "section_b=%u used_b=%ld max_entries=%ld max_used_b=%ld evicted_room=%ld full_logged=%ld records=%ld compactions=%ld "
		    "compact_kb=%llu",
		    g_geomOff ? "OFF" : h ? "made" : "not yet", h ? h->entriesUsed : 0u, h ? h->generation : 0u, g_gWanted, g_gHeld, g_gPending, g_gEvicted,
		    g_gOversize, g_gChanged, g_gFull, g_gFailed, g_gNotKeyable, g_gMapFail, g_gTableFull, (unsigned long long)(g_gBytes / 1024),
		    (double)g_gQpc * 1000.0 / (double)(g_freq ? g_freq : 1),
		    g_geomDataBytes, g_gUsedBytes, g_gMaxEntries, g_gMaxBytes, g_gEvictedRoom, g_gFullLogged, g_gUsed, g_gCompactions,
		    (unsigned long long)(g_gCompactBytes / 1024));   // ticket 0597
	}
}

void UnitFilter_StatsLine(void (*log)(const char* fmt, ...), double elapsedMs)
{
	static UfSum prev;
	if (!log) return;
	EnsureInit();
	UfSum cur, d;
	Sum(cur);
	const uint32_t* a = (const uint32_t*)&cur;
	const uint32_t* b = (const uint32_t*)&prev;
	uint32_t* o = (uint32_t*)&d;
	for (size_t i = 0; i < sizeof(UfSum) / 4; ++i) o[i] = a[i] - b[i];
	LogSum(log, "1s", d, elapsedMs);
	LogSum(log, "totals", cur, elapsedMs);
	prev = cur;
	ModelStatsLines(log, elapsedMs, cur, d);
}
