// srv_liveterrain_exefield.h - live terrain: the server writes a changed window into the EXE's height field (epic 0465,
// Extras C, ticket 4905). Written 2026-11-19 by w6-4905 for this repository (GPL-3.0-or-later).
//
// Why: the EXE's on-demand readers of the field (Lua HexToWorld, so flags, banners and Strategic View picking; a unit at
// its next move) read the GridHeightmap 0xD91270 (research/05-graphics-composition/live-terrain.md P3, 2.4). When the
// server draws a live change (4907's hook over 4903's producer, live-terrain.md 8.1 and 9), the same samples go into
// the EXE's field, so the EXE agrees with the picture. This is a data write from outside, as 2.4's fieldwrite.py did it
// (D4, D132 R4): a process memory write (NtWriteVirtualMemory) into the EXE's heap pools only; never an image page, never a code byte.
//
// Layout (live-terrain.md P4, render/include/c5r/heightfield/heightfield.h): the u16 layer's tile directory at
// [grid+0x1C] (8 bytes per 16 x 16 tile: u16 state, u16 unused, u32 pool index) over the pool at [grid+0x28] (512 bytes
// per tile); the u8 layer the same at [grid+0x5C] / [grid+0x68] (256 bytes per tile). Sample (x, y) is in tile
// (y >> 4) * tilesX + (x >> 4) at (y & 15) * 16 + (x & 15). Pointers in the EXE are 32-bit.
//
// What one call does (all or nothing; the details and the refusal list are in live-terrain.md 8.13):
//   1. reads the GridHeightmap object (0xC8 bytes) and checks it: ready, the field's size equal to the server's field
//      (`before`), tile counts, both layers' pointers non-null and the pool sizes within their capacity;
//   2. checks every rectangle (inside the field, arrays of w * h) and every touched tile's directory entry in both layers
//      (pooled, index inside the pool; a constant tile is refused);
//   3. checks every touched run of pool tiles (the reads and, a subset of them, the writes): inside its layer's pool
//      [ptrHigh + pool, + poolSize * tileBytes) and outside the EXE's image [exeBase, exeBase + exeImageSize) (an image
//      size of 0 is refused); then with VirtualQueryEx: committed, MEM_PRIVATE, protect exactly PAGE_READWRITE (no
//      guard, no write-copy, no execute, no read-only, no image, no mapped view) for every region the run covers. Each
//      region is queried once per call (a query costs time linear in the region's size, 8.13), before any pool read,
//      so a guard page is never touched; every call queries again;
//   4. reads every touched tile of both pools back and compares it with `before` (the server's field as it was before
//      this window): any difference is a refusal (the EXE's field is not the one the window was made for); the object
//      is re-read and compared before each write;
//   5. writes the changed tiles with NtWriteVirtualMemory (one call per run of tiles whose pool slots follow each other,
//      per layer); unlike kernel32's WriteProcessMemory it never makes a read-only or executable page writable, so a
//      page that changed after step 3 makes the write fail. No VirtualProtect(Ex) and no WriteProcessMemory anywhere in
//      this module (a test scans the source). Then reads every written run back and re-reads the object: a failed write
//      or read-back restores the runs written so far from step 4's copies (rollback); a changed object (a load began)
//      is never written into again.
// A refusal or failure writes nothing (or rolls back what it wrote) and returns a LOUD line (D118); the caller then
// treats the change as "wait for reload" and does not draw it either, so the server's picture and the EXE's field never
// disagree.
//
// Threading: no shared state; one call at a time per target (the render thread, or the live-terrain worker). No D3D, no
// IPC, no DLL access. Cost: a few hundred syscalls for a window of up to 64 plots (measured in 8.13).
#pragma once
#include "c5r/heightfield/heightfield.h"
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace srvrender {
namespace liveterrain {

// EXE builds (ticket 4966, epic 0590, D202): the build comes from the remote module's PE stamp and SizeOfImage
// (exebuild/include/vp_exe_build.h), the addresses from the EXE build table (exebuild/include/vp_exe_symbols.h; the
// catalogue rows cam_* of exebuild/symbols/shell.tsv, one row per anchor, and lt_* of server.tsv): cam_grid
// (GridHeightmap; build 4390913: 0xD91270), cam_gridptr (the EXE's pointer to it, 0xD90FC4, must hold the rebased
// cam_grid) and cam_wrapx, the first field of the grid constants block 0xD90FBC (880 bytes, which
// render/src/liveterrain/producer.cpp parses at fixed offsets). An unknown build is refused,
// as is a known one whose table does not carry these, or whose block fields are not at their build-4390913 offsets.

// The addresses of one EXE build on a module base (any VA = base + (VA - 0x400000)).
struct ExeFieldAddrs
{
	int build = -1;             // kVpExeBuild4390913, kVpExeBuild26577625, ...
	uint64_t grid = 0;          // cam_grid
	uint64_t gridPtr = 0;       // cam_gridptr
	uint64_t constants = 0;     // cam_wrapx
};
// The build of (stamp, sizeOfImage) and its addresses on `base`, with the checks above. False (why: one line naming the
// build, or the stamp of an unknown one) otherwise. Exposed for the tests.
bool ExeFieldAddressesFor(uint32_t stamp, uint32_t sizeOfImage, uint64_t base, ExeFieldAddrs& out, std::string& why);

// Where to write. Filled by OpenExeFieldTarget; tests fill it by hand over a mock grid in their own process.
struct ExeFieldTarget
{
	void* process = nullptr;    // HANDLE with PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
	                            // PROCESS_QUERY_INFORMATION (or _LIMITED_)
	uint64_t exeBase = 0;       // the EXE's module base (the EXE is relocated live): any EXE VA = exeBase +
	                            // (va - 0x400000), e.g. the grid constants block 0xD90FBC
	uint64_t exeImageSize = 0;  // the module's SizeOfImage: no write target may lie in [exeBase, exeBase + exeImageSize);
	                            // 0 (unknown) refuses every write. Tests give their own program's image.
	uint64_t gridAddr = 0;      // the GridHeightmap object in that process (rebased 0xD91270)
	uint64_t gridPtrAddr = 0;   // the EXE's pointer to the grid (rebased 0xD90FC4), compared with gridAddr at every
	                            // object read; 0: not checked (tests)
	uint64_t constantsAddr = 0; // the grid constants block in that process (rebased cam_wrapx; ticket 4966)
	int exeBuild = -1;          // the EXE's build (vp_exe_build.h); -1 in the tests' mock targets
	uint64_t ptrHigh = 0;       // added to every 32-bit pointer read from the object and the directories: 0 for the EXE
	                            // (a 32-bit process); tests only, to place a mock grid in a 64-bit test process
	bool ownsHandle = false;    // CloseExeFieldTarget closes `process`
	// Tests only: called right before each write (k = 0, 1, ...), to change the target between the checks
	// and the write. Null in the product.
	void (*testBeforeWrite)(void* ctx, uint32_t k) = nullptr;
	void* testCtx = nullptr;
};

// Opens the EXE's process by pid for the field write, finds the main module's base (EnumProcessModulesEx, 32-bit
// modules), takes the build from the PE TimeDateStamp and SizeOfImage and its addresses (ExeFieldAddressesFor), checks
// that [rebased cam_gridptr] == rebased cam_grid, and fills `out` (exeBase, exeImageSize, gridAddr, gridPtrAddr,
// constantsAddr, exeBuild; ownsHandle = true). Once per load is enough (the base does not change within a process). False (why) on any
// failure: then nothing is written for this load.
bool OpenExeFieldTarget(uint32_t pid, ExeFieldTarget& out, std::string& why);
void CloseExeFieldTarget(ExeFieldTarget& t);

// The containment rule of step 3, for one run [at, at + n): inside [poolLo, poolHi) and not overlapping [imageLo,
// imageHi); imageHi <= imageLo (unknown image) refuses. False (why) otherwise. Exposed for the tests.
bool ExeFieldRunInside(uint64_t at, uint64_t n, uint64_t poolLo, uint64_t poolHi, uint64_t imageLo, uint64_t imageHi,
                       std::string& why);

// A rectangle of the window in sample coordinates (x already wrapped into 0..samplesX-1; a window across the seam comes
// split), with w * h heights and w * h materials (row-major, x fastest): 8.1's c5r::liveterrain::WindowRect.
struct ExeFieldRect
{
	uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
	const uint16_t* heights = nullptr;
	const uint8_t* materials = nullptr;
	size_t nHeights = 0, nMaterials = 0;   // must both equal w * h
};

enum class ExeFieldStatus
{
	Written = 0,     // every changed sample is in the EXE's field and read back equal (also: nothing to change)
	Refused,         // a check failed before any write: nothing written
	RolledBack,      // a write or its read-back failed: the tiles written were restored and read back equal
	RollbackFailed,  // a write failed and the restore failed too: the EXE's field is mixed (LOUD; wait for the reload)
};
const char* ExeFieldStatusName(ExeFieldStatus s);

struct ExeFieldResult
{
	ExeFieldStatus status = ExeFieldStatus::Refused;
	uint64_t samples = 0;            // samples in the window
	uint64_t heightsChanged = 0, materialsChanged = 0;
	uint32_t tiles = 0;              // 16 x 16 tiles the window touches
	uint32_t writes = 0;             // writes made (rollback writes included)
	uint32_t regionChecks = 0;       // VirtualQueryEx calls
	double ms = 0;
	double msPrepare = 0, msWrite = 0;   // from the start to the first write; the writes (the rest is the read-back)
	std::string line;               // one log line; starts with "LOUD: " unless status == Written
	bool ok() const { return status == ExeFieldStatus::Written; }
};

// The entry point (4907's hook calls it for a live result BEFORE it applies the window to its own field):
//   before: the server's field as it is before this window (the scene's HeightGrid; heights and materials).
//   Written -> apply the window to the server's field and draw it; anything else -> the change waits for the reload
//   (log `line`, do not apply).
ExeFieldResult WriteExeFieldWindow(const ExeFieldTarget& t, const c5r::heightfield::HeightGrid& before,
                                   const ExeFieldRect* rects, size_t nRects);

// Convenience for any rectangle type with x0, y0, w, h and std::vector heights / materials (8.1's WindowRect, 4907's
// Rect).
template<class R>
ExeFieldResult WriteExeFieldWindow(const ExeFieldTarget& t, const c5r::heightfield::HeightGrid& before,
                                   const std::vector<R>& window)
{
	std::vector<ExeFieldRect> v(window.size());
	for(size_t i = 0; i < window.size(); ++i)
	{
		const R& r = window[i];
		v[i].x0 = (uint32_t)r.x0;
		v[i].y0 = (uint32_t)r.y0;
		v[i].w = (uint32_t)r.w;
		v[i].h = (uint32_t)r.h;
		v[i].heights = r.heights.data();
		v[i].materials = r.materials.data();
		v[i].nHeights = r.heights.size();
		v[i].nMaterials = r.materials.size();
	}
	return WriteExeFieldWindow(t, before, v.data(), v.size());
}

}  // namespace liveterrain
}  // namespace srvrender
