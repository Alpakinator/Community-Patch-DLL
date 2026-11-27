// vp_farm_points.h - the farm buildings' points from the server (x64) to the farm guard in the shell (x86, inside the EXE):
// one entry per farm plot of the server's own fields, the point where the EXE's farm placer is to put that plot's building
// (ticket 0657, D231 follow-up). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// Why: with [Renderer] ServerFarmFields = 1 the EXE's farm manager has no field curves, so its placer keeps each building
// at the plot centre; stock puts it at the centroid of one of the plot's own fields (cities-and-landmarks.md 2.6.2). The
// server computes that rule on its own fields (c5r::farmfields::stockPlacement) and publishes the point here; the farm
// guard (shell/composite/farm_guard.cpp, owned by the proxy) hands it to the EXE's placer as a transient curve inside its
// pass-throughs on the farm manager's two placing listeners, and holds a farm end until this load's points are here.
//
// This header holds no EXE address, no symbol of the EXE build table and no game name (D83, D202). It includes nothing but
// the C standard headers, so the proxy (x86, VC9 headers), the composite (x86) and the server (x64) include it as it is.
//
// ---- Named object (the session's Local\ namespace; <base> = the version-3 section name, VPSHELL_SECTION) -------------
//   <base>.Farm   this section, VPFARM_SECTION_BYTES; created by the server (CreateFileMappingW, page file) when its farm
//                 producer is on (render_farmfields = 1); opened read-only by the guard (OpenFileMappingW), never created
//                 there. A fresh section is zero-filled (magic 0 = nothing published yet).
//
// ---- One writer, a seqlock -----------------------------------------------------------------------------------------
// The server's render thread is the only writer. Writer: seq += 1 (odd), MemoryBarrier, the header fields and the entries,
// MemoryBarrier, seq += 1 (even). Reader: s1 = seq (even and non-zero, else retry or give up), MemoryBarrier, copy the
// header and look up the entries it needs, MemoryBarrier, s2 = seq; the copy is good only when s1 == s2. A reader never
// writes. Every field is 32-bit aligned (an x86 reader reads each 32-bit word atomically).
//
// ---- Meaning --------------------------------------------------------------------------------------------------------
//   loadSeq   the load the entries belong to: the camera block's gridSeq of that load (VPShellCamera::gridSeq, which the
//             server's height-field tap carries as VPShellTap::loadSeq); the guard uses entries only while its own camera
//             grid seq equals it (a second load in one process never takes the first load's points).
//   state     VPFARM_PRODUCING: the load is known, its farm production is not in force yet (count may be 0);
//             VPFARM_DONE: the entries are the buildings of the production in force for loadSeq (a farm the EXE places that
//             is missing here is not one the server has laid out yet, or not one it will lay out).
//   entries   sorted by (hexY, hexX) ascending, unique: hexX = grid x - (y - (y & 1)) / 2 (the EXE's hex x of a plot, the
//             farm event's +0x18), hexY = grid y. kind VPFARM_KIND_POINT: (wx, wy) in the EXE's world frame (the frame of
//             its plot centres and field curves: sample x * sx - ox, sample y * sy - oy, the height field's constants);
//             kind VPFARM_KIND_CENTRE: no field of the plot takes part in the stock rule, the plot centre is right.
#pragma once
#include <stddef.h>

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int vpfarm_u32;
typedef int vpfarm_i32;
typedef unsigned short vpfarm_u16;
typedef short vpfarm_i16;
#else
#include <stdint.h>
typedef uint32_t vpfarm_u32;
typedef int32_t vpfarm_i32;
typedef uint16_t vpfarm_u16;
typedef int16_t vpfarm_i16;
#endif

#define VPFARM_MAGIC 0x4D524656u          /* 'VFRM' */
#define VPFARM_VERSION 1u
#define VPFARM_SECTION_SUFFIX L".Farm"
#define VPFARM_CAPACITY 32768u            /* entries (farm plots of one load; a huge 43-civ save holds about 2,000) */
#define VPFARM_HEADER_BYTES 64u
#define VPFARM_ENTRY_BYTES 16u
#define VPFARM_SECTION_BYTES (VPFARM_HEADER_BYTES + VPFARM_CAPACITY * VPFARM_ENTRY_BYTES)   /* 524,352 */

enum VPFarmState
{
	VPFARM_NONE = 0,        // nothing published (a fresh section)
	VPFARM_PRODUCING = 1,   // loadSeq is known, its production is not in force yet
	VPFARM_DONE = 2         // the entries are the production in force for loadSeq
};

enum VPFarmKind
{
	VPFARM_KIND_CENTRE = 1,   // the plot centre (no field takes part)
	VPFARM_KIND_POINT = 2     // (wx, wy)
};

struct VPFarmHeader
{
	vpfarm_u32 magic;         // VPFARM_MAGIC once the server initialised the section
	vpfarm_u32 version;       // VPFARM_VERSION
	volatile vpfarm_u32 seq;  // the seqlock (odd while the server writes)
	vpfarm_u32 loadSeq;       // the camera grid seq of the load
	vpfarm_u32 state;         // VPFarmState
	vpfarm_u32 count;         // entries in use (<= VPFARM_CAPACITY)
	vpfarm_u32 generation;    // + 1 at every publish (a new list or a new state)
	vpfarm_u32 serverPid;     // the writer's process id
	vpfarm_u32 points;        // entries of kind POINT
	vpfarm_u32 dropped;       // farm plots not published (beyond VPFARM_CAPACITY, or a plot outside the 16-bit hex range)
	vpfarm_u32 reserved[6];
};

struct VPFarmEntry
{
	vpfarm_i16 hexX, hexY;
	vpfarm_u32 kind;          // VPFarmKind
	float wx;                 // world x (kind POINT)
	float wy;                 // world y
};

#ifdef __cplusplus
static_assert(sizeof(VPFarmHeader) == VPFARM_HEADER_BYTES, "layout");
static_assert(sizeof(VPFarmEntry) == VPFARM_ENTRY_BYTES, "layout");
#endif

#define VPFARM_ENTRIES(hdrPtr) ((const VPFarmEntry*)((const char*)(hdrPtr) + VPFARM_HEADER_BYTES))

// (hexY, hexX) order of the entries
static inline int VPFarmKeyCmp(vpfarm_i32 ax, vpfarm_i32 ay, vpfarm_i32 bx, vpfarm_i32 by)
{
	if (ay != by) return ay < by ? -1 : 1;
	if (ax != bx) return ax < bx ? -1 : 1;
	return 0;
}

// The entry of (hexX, hexY) among n sorted entries, or NULL. Reads only; the caller checks the seqlock around it.
static inline const VPFarmEntry* VPFarmFind(const VPFarmEntry* e, vpfarm_u32 n, vpfarm_i32 hexX, vpfarm_i32 hexY)
{
	vpfarm_u32 lo = 0, hi = n;
	while (lo < hi)
	{
		const vpfarm_u32 mid = lo + (hi - lo) / 2;
		const int c = VPFarmKeyCmp(e[mid].hexX, e[mid].hexY, hexX, hexY);
		if (c == 0) return e + mid;
		if (c < 0) lo = mid + 1; else hi = mid;
	}
	return 0;
}
