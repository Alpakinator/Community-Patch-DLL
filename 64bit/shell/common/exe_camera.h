// exe_camera.h - where CivilizationV_DX11.exe keeps its camera, and how to recognise that EXE (issue 3007; both Steam
// builds since epic 0590, ticket 4964). Written 2026-11-11 for this repository (GPL-3.0-or-later). The addresses are
// named entries of the build table (exebuild/symbols/shell.tsv, cam_*; research/02-exe-internals/exe-build-table.md):
// their anchors (build 4390913) come from research/02-exe-internals/camera-and-picking.md (sections 2, 4, 6, 9, 10), and
// every other build's values from the local per-build tables (D202), compiled in through vp_exe_tables.gen.h. The byte
// patterns below are checked against both builds' golden EXEs by tools/exebuild/gen_table.py (exebuild/patterns.txt)
// and by shell/tests/test_camera.py::test_signatures_match_exe_on_disk.
//
// Used by shell_composite.dll (validation before any read; the per-frame read) and by test_host.exe (--fake-exe: a fake
// image with the same layout, so the read path is tested without the game). Addresses are VAs at ImageBase 0x400000;
// the EXE is relocated at run time (0x00740000 / 0x00F60000 seen live): always rebase on the module base.
//
// How a consumer gets an address (ticket 4964):
//   - the running build: VPCamBuild(), set once by the composite's camera validation (VPCamSetBuild) from the EXE's PE
//     header (VpExeBuildOfImage: stamp + SizeOfImage); kVpExeBuildUnknown until then, and in any module that has not
//     set it (each module has its own: test hosts set theirs for the image they lay out);
//   - a camera address: VPCAM_VA_APP ... (expressions: VpExeVa(VPCamBuild(), VPX_cam_app), 0 when the build is unknown or
//     does not carry it), or VPCAM_VA_OF(build, cam_app) for a given build;
//   - any catalogue symbol: VpExeVa(build, VPX_<name>) / VpExeAddr(build, VPX_<name>, exeBase) (vp_exe_symbols.h);
//   - a pattern table: X(symbol, description, bytes with R(symbol) operands) rows, checked with VPExeCheckSigs()
//     below; laid into a fake image with VpExeFakeHeader() and VpExeLaySigs(). Since ticket 0603 these helpers, the
//     row type and the image base (VPEXE_IMAGE_BASE) are in exebuild/include/vp_exe_sigs.h, shared with the taps'
//     shell/composite/shell_exe.h; the VPExe* names here are thin names over it.
// The table part needs vp_exe_tables.gen.h on the include path (shell/build.cmd's %EXEINC%); a unit that only uses the
// matrix helpers and thresholds below (the server's render scene) compiles without it.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#if defined(__has_include)
#if __has_include("vp_exe_tables.gen.h") && __has_include("vp_exe_sigs.h")
#define VPCAM_HAVE_EXE_TABLES 1
#endif
#endif

#ifdef VPCAM_HAVE_EXE_TABLES
#include "vp_exe_sigs.h"

// The running EXE build of this module (one value per module: an inline function's static).
inline int& VPCamBuildSlot_() { static int b = kVpExeBuildUnknown; return b; }
inline int VPCamBuild() { return VPCamBuildSlot_(); }
inline void VPCamSetBuild(int build) { VPCamBuildSlot_() = build; }

// A camera symbol's VA (ImageBase 0x400000) in a given build; 0 when not carried.
#define VPCAM_VA_OF(build, sym) VpExeVa((build), VPX_##sym)

// .data globals (camera-and-picking.md section 4), in the running build
#define VPCAM_VA_APP VPCAM_VA_OF(VPCamBuild(), cam_app)                  // Civ5App* g_Civ5App
#define VPCAM_VA_NUMVIEWS VPCAM_VA_OF(VPCamBuild(), cam_numviews)        // uint g_NumViews (Strategic View: its views)
#define VPCAM_VA_WRAPX VPCAM_VA_OF(VPCamBuild(), cam_wrapx)              // int  1 = map wraps in X
#define VPCAM_VA_GRIDPTR VPCAM_VA_OF(VPCamBuild(), cam_gridptr)          // GridHeightmap* (0 before a map loads)
#define VPCAM_VA_AABB VPCAM_VA_OF(VPCamBuild(), cam_aabb)                // float min xyz, max xyz
#define VPCAM_VA_PERIOD VPCAM_VA_OF(VPCamBuild(), cam_period)            // float wrap period (64 W - 1)
#define VPCAM_VA_WORLDH VPCAM_VA_OF(VPCamBuild(), cam_worldh)            // float world height
#define VPCAM_VA_W2H VPCAM_VA_OF(VPCamBuild(), cam_w2h)                  // D3DXMATRIX world -> hex
#define VPCAM_VA_H2W VPCAM_VA_OF(VPCamBuild(), cam_h2w)                  // D3DXMATRIX hex -> world
#define VPCAM_VA_GRID VPCAM_VA_OF(VPCamBuild(), cam_grid)                // GridHeightmap object
#define VPCAM_VA_PROJ VPCAM_VA_OF(VPCamBuild(), cam_proj)                // D3DXMATRIX g_Proj
#define VPCAM_VA_VIEW VPCAM_VA_OF(VPCamBuild(), cam_view)                // D3DXMATRIX g_View[4]
#define VPCAM_VA_VIEWPORT VPCAM_VA_OF(VPCamBuild(), cam_viewport)        // D3DVIEWPORT9 {DWORD X, Y, W, H; float MinZ, MaxZ}
#define VPCAM_VA_FCAM_VTBL VPCAM_VA_OF(VPCamBuild(), cam_fcamera_vtbl)   // FCamera vtable (.rdata): dtor, Update, GetViewProj
#endif   // VPCAM_HAVE_EXE_TABLES

// GridHeightmap fields (camera-and-picking.md section 9)
#define VPCAM_GRID_READY 0x09         // byte, 1 when ready
#define VPCAM_GRID_SAMPLES_X 0x0C     // int 64 W
#define VPCAM_GRID_SAMPLES_Y 0x10     // int 64 (H + 4)
#define VPCAM_GRID_SX 0xA8            // float sx, sy, height scale, ox, oy at +0xA8 .. +0xB8 (= VA 0xD91318 ..)

// GameViewState = *(g_Civ5App + 0xC0)
#define VPCAM_APP_GVS 0xC0
#define VPCAM_GVS_FCAMERA 0x48        // FCamera vptr
#define VPCAM_GVS_FOV 0x50            // float degrees
#define VPCAM_GVS_ASPECT 0x54
#define VPCAM_GVS_CAMWORLD 0x78       // D3DXMATRIX camera-to-world (FCamera + 0x30)
#define VPCAM_GVS_NEAR 0x118
#define VPCAM_GVS_FAR 0x11C
#define VPCAM_GVS_VIEWMODE 0x758      // 1 = 3D map, 2 = Strategic View
#define VPCAM_GVS_BYTES 0x760         // the part of the GVS a fake image needs

#ifdef VPCAM_HAVE_EXE_TABLES
// Byte patterns checked before the first read. X(symbol, description, bytes): the pattern sits at the symbol's VA in
// the running build; bytes are hex, ?? = any byte, R(symbol) = 4 bytes holding that symbol's VA in the running build
// rebased on the module base (the loader relocated these immediates); a literal E8/E9 frees the four rel32 bytes after
// it (exe-build-table.md section 4). Each one pins an address or an offset the reader uses. One row per line: parsed by
// tools/exebuild/gen_table.py (exebuild/patterns.txt) and shell/tests/test_camera.py.
#define VPCAM_SIGNATURES(X) \
	X(cam_fn_setengineui, "g_Civ5App (LoadCvGameCoreDLL_SetEngineUserInterface)", "8B 0D R(cam_app) 8B 81 D0 00 00 00") \
	X(cam_fn_updateviews, "UpdateViews head: view mode GVS+0x758, map path when 1", "55 8B EC 83 E4 F8 81 EC 10 01 00 00 53 55 56 57 8B E9 8B 85 58 07 00 00 BF 01 00 00 00 89 6C 24 10 3B C7 0F 85") \
	X(cam_updateviews_controller, "UpdateViews: camera controller GVS+0x44", "8D 75 44") \
	X(cam_updateviews_getviewproj, "UpdateViews: GetViewProj(&g_View[0], &g_Proj)", "68 R(cam_proj) 68 R(cam_view) 8B CE E8 32 DC 21 00") \
	X(cam_updateviews_numviews, "UpdateViews: g_NumViews = 1, g_MapWrapsX test", "89 3D R(cam_numviews) 39 3D R(cam_wrapx)") \
	X(cam_updateviews_period, "UpdateViews: wrap period", "F3 0F 10 05 R(cam_period)") \
	X(cam_worldanchor_site, "WorldAnchor: D3DXVec3Project(world, proj, viewport)", "68 R(cam_world_identity) 55 68 R(cam_proj) 68 R(cam_viewport) 57") \
	X(cam_fcamera_getviewproj, "FCamera::GetViewProj: camWorld at FCamera+0x30", "8B 54 24 04 56 8B F1 8D 46 30 50 8D 4C 24 0C 51 52 E8 20 3C FC FF") \
	X(cam_fcamera_vtbl, "FCamera vtable", "R(cam_fcamera_dtor) R(cam_fcamera_update) R(cam_fcamera_getviewproj)") \
	X(cam_fn_hextoworld, "HexToWorld: M_h2w, grid sx at +0xA8, ox at +0xB4", "55 8B EC 83 E4 F0 83 EC 08 8B 45 0C 56 57 8B 7D 08 68 R(cam_h2w) 50 57 8B F1 E8 B3 34 22 00 F3 0F 10 07 F3 0F 59 86 A8 00 00 00 F3 0F 5C 86 B4 00 00 00") \
	X(cam_lua_hextoworld, "Lua HexToWorld: GridHeightmap pointer", "8B 0D R(cam_gridptr)")

// One row of a pattern table (vp_exe_sigs.h's VpExeSig; rows made with VPEXE_SIG_ENTRY).
typedef VpExeSig VPExeSig;
static const VPExeSig kVPCamSigs[] = { VPCAM_SIGNATURES(VPEXE_SIG_ENTRY) };
static const int kVPCamNumSigs = (int)(sizeof(kVPCamSigs) / sizeof(kVPCamSigs[0]));

// Checks every row of a table on the image of `build` loaded at base, through `read` (0 = the in-process VirtualQuery
// reader of vp_exe_patterns.h; a tap passes its own SEH reader). Returns -1 when all match, else the first failing row,
// with `why` = "pattern '<description>' at <symbol> (VA 0x<va in this build>): <reason> [at byte k]" (vp_exe_sigs.h).
static inline int VPExeCheckSigs(int build, uintptr_t base, const VPExeSig* sigs, int n, VpExeReadFn read, void* ctx,
                                 char* why, size_t whyLen)
{
	return VpExeCheckSigs(build, base, sigs, n, read, ctx, why, whyLen);
}
#endif   // VPCAM_HAVE_EXE_TABLES

// Row-vector 4x4 helpers (D3DX convention), shared by the DLL's and the server's consistency checks.
static inline void VPCamMul(const float* a, const float* b, float* out)
{
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
		{
			double s = 0;
			for (int k = 0; k < 4; ++k) s += (double)a[r * 4 + k] * (double)b[k * 4 + c];
			out[r * 4 + c] = (float)s;
		}
}

// Issue 3015 (verification 9031, V1): every element is tested explicitly. A NaN compares false with everything, so
// the old `if (e > rot) rot = e` maxima dropped NaN entries and an all-NaN view came out as error 0 (published OK).
static inline bool VPCamAllFinite(const float* a, int n)
{
	for (int i = 0; i < n; ++i)
	{
		uint32_t u; memcpy(&u, &a[i], 4);
		if ((u & 0x7F800000u) == 0x7F800000u) return false;   // exponent all ones: inf or NaN (independent of /fp mode)
	}
	return true;
}

// camWorld * view0 must be the identity when view0 = inverse(camWorld). rot = max error of the 3x3 part and the w
// column; trans = max |translation row| in world units. Any non-finite input or product gives rot = trans = +inf.
static inline void VPCamConsistency(const float* camWorld, const float* view0, float& rot, float& trans)
{
	const float inf = INFINITY;
	if (!VPCamAllFinite(camWorld, 16) || !VPCamAllFinite(view0, 16)) { rot = inf; trans = inf; return; }
	float m[16]; VPCamMul(camWorld, view0, m);
	if (!VPCamAllFinite(m, 16)) { rot = inf; trans = inf; return; }
	rot = 0; trans = 0;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
		{
			float e = m[r * 4 + c] - (r == c ? 1.0f : 0.0f);
			if (e < 0) e = -e;
			if (!(e <= rot)) rot = e;
		}
	for (int c = 0; c < 3; ++c) { float e = m[12 + c]; if (e < 0) e = -e; if (!(e <= trans)) trans = e; }
	float w = m[15] - 1.0f; if (w < 0) w = -w; if (!(w <= rot)) rot = w;
}

// A wrap view must equal Translation(+-period, 0, 0) * view0: same rows 0..2, row 3 = row3(view0) +- period * row0.
// Returns the largest deviation (world units, against the better of the two signs); +inf for any non-finite input.
static inline float VPCamWrapError(const float* view0, const float* viewi, float period)
{
	const float inf = INFINITY;
	if (!VPCamAllFinite(view0, 16) || !VPCamAllFinite(viewi, 16) || !VPCamAllFinite(&period, 1)) return inf;
	float best = inf;
	for (int s = -1; s <= 1; s += 2)
	{
		float e = 0;
		for (int k = 0; k < 12; ++k) { float d = viewi[k] - view0[k]; if (d < 0) d = -d; if (!(d * 1000.0f <= e)) e = d * 1000.0f; }
		for (int c = 0; c < 4; ++c)
		{
			float want = view0[12 + c] + (float)s * period * view0[c];
			float d = viewi[12 + c] - want; if (d < 0) d = -d;
			if (!(d <= e)) e = d;
		}
		if (e < best) best = e;
	}
	return best;
}

// Thresholds of the consistency checks (DLL and server). A different camera (leader scene, a stale matrix) misses
// them by orders of magnitude; float rounding of a 4096-unit world stays below 0.05 units / 1e-4.
#define VPCAM_TOL_ROT 2e-3f
#define VPCAM_TOL_TRANS 0.5f
#define VPCAM_TOL_WRAP 1.0f

static inline const char* VPCamStatusName(uint32_t s)
{
	static const char* n[] = { "NONE", "OK", "OFF", "REFUSED", "NO_GVS", "BAD_GVS", "NOT_MAP", "INCONSISTENT", "TORN", "EXCEPTION" };
	return s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}
