// decal_tap.cpp - the decal tap inside shell_composite.dll (epic 0166, issue 4102; route (a)).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Contract: decal_tap.h; layout: shell/common/
// shell_decal_tap.h. What the EXE's objects are, and why each byte pattern below is checked: research/05-graphics-
// composition/terrain-decals.md sections 1, 5, 6 and 8 (behaviour, D83; nothing of the EXE is in this file but the
// patterns' few bytes, as in bake_tap.cpp and water_tap.cpp).
//
// What it does (section 9 of the research file):
//   POLL (every Present while a map is loaded): one guarded read of TerrainDecalSystem's header (TDS bake_ts, 0xC0
//      bytes) and of its batch vector (0x34 bytes per batch: about 1 KB on the huge save). A batch whose counters moved
//      (free head, live, slots, listed, list head and tail, vertices used, pool and chunk pointers) is marked; so is
//      every batch when the batch count or vector moves. Every decal_net_ms the live records of every batch are hashed
//      (the safety net for a remove and an add that leave the counters equal).
//   READ (once the signal has been still for decal_settle_frames Presents, or decal_max_wait_ms after the first move;
//      within decal_budget_ms per Present): per marked batch its record pool and its used vertex bytes copied out of the
//      EXE in one Present, its counters read again after the copy (a batch that moved meanwhile is read again later: a
//      torn read); then, from that copy and over as many Presents as the budget needs, every live record (bit 31 of
//      +0x04) is checked (triangles, its vertex run inside the used part of the chunk, a finite rectangle, texture
//      handles inside the handle pool) and kept with its vertex bytes and a content hash (pass, priority, handles,
//      rectangle, triangles, vertex bytes: slots and vertex offsets are not identity, 1.2); a batch whose entry moved
//      between its copy and the end of that work is read again.
//   PUBLISH: the decals of every batch in batch order, then the EXE's list order; the names of their textures (8: the
//      resource manager's map of type 8, TDS+0x4F0's blank spec, the farm-type table as '@farm_types.xml/...'
//      references); the content-hash multiset is compared with the last published one, and only a different set
//      makes a generation: the image (header, records, names, vertices, the added and removed rectangles against the
//      generation the server last acknowledged, the content hashes) built step by step over Presents (StepBuild, issue
//      4110: the set per batch, its merge, the names, the diff, the records and vertices per batch), its FNV-1a 64 and the
//      section `Local\VPShellDecal.<pid>.<generation>` (views of at most 256 KiB, unmapped at once) (StepPublish); every
//      step only while its estimated cost still fits decal_budget_ms (Fits / Learn); then the control block (seqlock)
//      says READY. No read runs during a build: it works on the tap's own copies of the batches (one snapshot); a batch
//      that moves meanwhile restarts the build (at most DT_BUILD_RESTARTS times in a row, then the snapshot is
//      published and the batch read after it). The previous generation's section is closed; the current one when the
//      server acknowledges it.
// Safety: every read of EXE memory goes through SafeRead (bounds + SEH): a fault disables the tap for the process
// (FAILED SEH). A failed pattern check, an EXE build the patch does not know, a symbol its table does not carry, or an
// EXE the composite did not validate, refuses the EXE for good (FAILED PATTERN). Addresses: the EXE build table (epic
// 0590, D202; shell_exe.h): bake_ts, decal_resmgr, decal_farm_owner and the pattern sites below, by name; their VAs are
// in the local per-build tables only. A failed layout check of the records is read again up to DT_LAYOUT_TRIES times (it can be a torn read),
// then FAILED LAYOUT until the records pass again; a texture handle without a name: FAILED NAMES (no generation) until
// it has one. Each FAILED is one LOUD log line (repeats rate-limited); the last good image stays published; the server
// decides what an install failure means (4105, D118). It calls no EXE function and writes nothing into the EXE.
//
// Test seam (like water_test): decal_test=<mode> (ini or VPSHELL_DECAL_TEST) builds a synthetic EXE (a TDS header, two
// batches with pools and chunks, a resource manager with its maps, a farm-type table) in this DLL's memory and runs the
// same code on it; decal_test=huge builds the huge save's size instead (17,800 decals in 19 batches, BuildHugeWorld),
// and decal_test_midbuild=N changes one of its batches during each of the next N builds (the restart's test). A file
// `shell_composite.decalchange` next to the DLL removes one decal and adds one in test mode (huge: frees or restores one),
// `shell_composite.decalbreak` makes a record of batch 1 run past its chunk (each deleted when seen). decal_test=patterns
// (ticket 4965) runs only the real pattern check, on a fake image of the build decal_test_build=<steam build> (default
// 4390913; "0": a header no build has) holding every pattern, one broken by decal_test_pattern=<k> (-1 none);
// decal_test_drop=<symbol> makes that symbol not carried; then nothing is read.
#include "decal_tap.h"
#include "shell_exe.h"
#include "shell_decal_tap.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- EXE layout (DX11; addresses from the build table, epic 0590: shell_exe.h; terrain-decals.md 1 and 8) ---------------
// bake_ts: TerrainDecalSystem (TerrainSystem+0); decal_resmgr: the resource manager's FSingleton pointer;
// decal_farm_owner: the farm system's owner, its farm-type table at +DT_FARM_OFF.
#define DT_FARM_OFF 0x24u           // the farm-type table in the farm system's owner
#define DT_HDR_BYTES 0xC0u          // TDS +0x00..+0xBC (counters, world rectangle, large list, batch vector)
#define DT_BATCH 0x34u
#define DT_REC 0x34u
#define DT_NIL 0x0FFFFFFFu
#define DT_CHUNK_VERTS 0xFFFFu      // 0x7FFF8 bytes of 8-byte vertices
#define DT_MAX_BATCHES 1024u
#define DT_MAX_SLOTS 0x10000u
#define DT_MAX_HANDLE 0x3000u       // the texture handle pool
#define DT_MAX_TREE (1u << 17)
#define DT_FARM_BYTES 0x458u
#define DT_FARM_CATS 8u
#define DT_VIEW_BYTES (4u * 1024 * 1024)
#define DT_LAYOUT_TRIES 3
#define DT_STACK 96
// the tile grid and tile+0x300 (issue 4111; terrain-decals.md 14; terrain-rendering-inventory.md 5.4)
#define DT_TS_TR 0x75FA4u           // TerrainSystem (= TDS) +0x75FA4: the TerrainRenderer (TR)
#define DT_TR_TX 0x2310u            // tiles in x; +0x2314 in y; +0x2318 the count
#define DT_TR_GRID 0x1AE0u          // the tile pointers, column-major (index x * tiles in y + y)
#define DT_T_AREA 0x300u            // tile+0x300: "area changed since load", written 1 by the area invalidation
#define DT_AREA_CHUNK 1024u         // tiles read per step

// Byte patterns checked before the first read of the real EXE (R(name) = 4 bytes holding the symbol's address in the
// running build; gen_table.py checks every row on both builds, exebuild/patterns.txt). Function names (AddDecal,
// RemoveDecal, the decal draw, the farm reader ...) are the research's (terrain-decals.md); the addresses are the build
// table's.
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kDtSigs[] = {
	X(VPX_decal_rm_tds_site, "TDS (mov ecx before a call of RemoveDecal)", "B9 R(bake_ts) E8 83 3A 24 00")
	X(VPX_decal_add_batchvec, "AddDecal: batch vector TDS+0xB4, vertices used batch+0x24, limit 0xFFFF", "8B 85 B4 00 00 00 8B 54 30 24 03 C6 03 D1 81 FA FF FF 00 00")
	X(VPX_decal_add_batches, "AddDecal: 0x34-byte batches, batch count TDS+0xB8", "83 EE 34 47 83 C6 34 3B BD B8 00 00 00")
	X(VPX_decal_add_chunk, "AddDecal: vertex chunk of 0x7FFF8 bytes", "68 F8 FF 07 00")
	X(VPX_decal_add_blank, "AddDecal: the blank spec TDS+0x4F0", "8B 47 0C 3B 85 F0 04 00 00")
	X(VPX_decal_rm_records, "RemoveDecal: 0x34-byte records in the pool batch+0x08, rectangle +0x24/+0x2C", "6B F6 34 03 B7 B4 00 00 00 85 C9 74 19 8B C3 6B C0 34 03 46 08 6A 00 6A 00 8D 50 2C 52 83 C0 24 50")
	X(VPX_decal_draw_flags, "decal draw: record flags +0x08 bit 0", "8B 48 08 F6 C1 01")
	X(VPX_decal_draw_rect, "decal draw: rectangle record+0x24..+0x30, priority +0x20", "F3 0F 10 45 30 D9 45 24 F3 0F 10 4D 2C F3 0F 10 55 28 D9 58 50 F3 0F 11 50 54 F3 0F 11 48 58 F3 0F 11 40 5C D9 45 20 D9 58")
	X(VPX_decal_draw_tex, "decal draw: texture handles record+0x14, +0x18", "66 8B 45 14 66 89 41 18 66 8B 55 18")
	X(VPX_decal_draw_tex2, "decal draw: texture handle record+0x1C", "66 8B 4D 1C")
	X(VPX_decal_draw_verts, "decal draw: first vertex +0x0C, triangles +0x10, vertex buffer batch+0x20", "8B 45 0C 8B 8C 24 88 00 00 00 89 41 10 8B 55 10 8B 84 24 88 00 00 00 8B 4C 24 10 89 50 0C 8B 51 20")
	X(VPX_decal_texunload_mgr, "texture unload: the resource manager, the map of type 8, its head", "A3 R(decal_resmgr) 6A 08 8B C8 E8 95 B9 FF FF 85 C0 74 31 8B 70 04")
	X(VPX_decal_texunload_node, "texture unload: node +0x10 -> resource +0x1C -> handle +0x08", "8B 48 10 8B 41 1C 85 C0 74 05 39 78 08")
	X(VPX_decal_resmaps, "resource maps: the type map at manager+0x40, head +0x44", "8D 5E 40 51 8B CB 89 7C 24 1C E8 5E EC FF FF 8B 44 24 0C 3B 46 44")
	X(VPX_decal_mapfind, "map find: node key +0x0C, right +0x08, left +0x00, is-nil +0x15", "39 70 0C 73 05 8B 40 08 EB 04 8B D0 8B 00 80 78 15 00")
	X(VPX_decal_startup_blank, "TDS Startup: blank_texture_spec_decal.dds into TDS+0x4F0", "68 R(decal_s_blank_spec) E8 AC 19 1B 00 8D 7E 6C 8B CF 89 86 F0 04 00 00")
	X(VPX_decal_farminit, "farm init: blank_texture.dds into +0x454, then farm_types.xml (the farm reader)", "68 R(decal_s_blank) E8 B0 96 17 00 68 R(decal_s_farm_types) 8B CB 89 83 54 04 00 00 E8 5E A3 04 00")
	X(VPX_decal_farmsys_at24, "the farm system at +0x24 of its owner", "8D 4E 24 E8 3D 99 2D 00")
	X(VPX_decal_farm_owner_ctor, "the farm system's owner (its static constructor)", "B9 R(decal_farm_owner) E8 86 6F CE FF")
	X(VPX_decal_farmread_cats, "farm reader: 8 category names, ranges at +0x24", "BE R(decal_farm_cats) 8D 57 24 89 74 24 28 89 54 24 18 C7 44 24 2C 08 00 00 00")
	X(VPX_decal_farmread_variants, "farm reader: 16-byte variants at +0x44, count +0x48", "8B 46 04 C1 E0 04 03 06 74 13 8B 4C 24 34 8B 54 24 38 89 08 89 50 04 89 58 08 89 78 0C FF 46 04")
	X(VPX_decal_s_blank_spec, "string blank_texture_spec_decal.dds", "62 6C 61 6E 6B 5F 74 65 78 74 75 72 65 5F 73 70 65 63 5F 64 65 63 61 6C 2E 64 64 73 00")
	X(VPX_decal_s_blank, "string blank_texture.dds", "62 6C 61 6E 6B 5F 74 65 78 74 75 72 65 2E 64 64 73 00")
	X(VPX_decal_s_farm_types, "string farm_types.xml", "66 61 72 6D 5F 74 79 70 65 73 2E 78 6D 6C 00")
	X(VPX_decal_farm_cats, "the 8 farm category names, 32 bytes apart (Crops_Half_Built .. Crops_Damage)", "43 72 6F 70 73 5F 48 61 6C 66 5F 42 75 69 6C 74 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 41 66 72 69 63 61 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 41 6D 65 72 69 63 61 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 41 73 69 61 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 45 75 72 6F 70 65 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 57 68 65 61 74 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 57 68 65 61 74 5F 48 42 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 43 72 6F 70 73 5F 44 61 6D 61 67 65 00")
	// issue 4111: the tile grid and tile+0x300 (the area invalidation is what writes it)
	X(VPX_bake_tr_site, "TR = TS+0x75FA4, condition byte TS+0x75FDC", "56 8B F1 8B 8E A4 5F 07 00 85 C9 74 2C 80 BE DC 5F 07 00 00")
	X(VPX_decal_area_tx, "area invalidation (this = TR): tiles in x TR+0x2310", "8B 86 10 23 00 00 8B C8 89 4C 24 0C")
	X(VPX_decal_area_grid, "area invalidation: tile pointers TR+0x1AE0, column-major (x * TR+0x2314 + y)", "8B 86 14 23 00 00 8B 8E E0 1A 00 00 0F AF C3")
	X(VPX_decal_area_set, "area invalidation: tile+0x300 = 1 (area changed)", "C7 81 00 03 00 00 01 00 00 00")
	// issue 0366: the spatial index of the small decals, the large list and the draw's walk and sort (terrain-decals.md 18)
	X(VPX_decal_add_index, "AddDecal: the spatial index is TDS+0x6C", "8D 45 6C 89 84 24 98 00 00 00")
	X(VPX_decal_add_large, "AddDecal: a decal is large when a side exceeds 256", "F3 0F 10 05 R(decal_k_256) 0F 2F D8 BF FF FF FF 0F C7 44 24 2C FF FF FF 1F")
	X(VPX_decal_add_item, "AddDecal: a small decal's item from the pool (index+0x24), linked by the index insert", "8B BC 24 98 00 00 00 8B 4F 24 8D 44 24 2C 50 E8 E3 CD 2F 00 8B F0 56 8B CF E8 09 11 20 00")
	X(VPX_decal_pool_items, "index item pool: items used +0x0C, the item array +0x08", "8B 7E 0C 8D 5E 08")
	X(VPX_decal_ins_items, "index insert: 24-byte items, point +0x08 / +0x0C", "8B 4E 24 8B 51 08 57 8D 04 5B D9 44 C2 08 8D 3C C2")
	X(VPX_decal_ins_head, "index insert: at the head of its cell (heads +0x18; next << 1, previous none)", "8B 4E 18 8B 14 81 8B 0F 03 D2 83 E1 01 0B D1 89 17 C7 47 04 FF FF FF 0F")
	X(VPX_decal_cell, "index cell: (p - min) x inverse (+0x10 / +0x14), single precision", "F3 0F 10 51 10 F3 0F 10 01 F3 0F 10 25 R(decal_k_cell) F3 0F 10 49 04 F3 0F 10 59 14 0F 57 C4 F3 0F 58 45 08 F3 0F 59 C2")
	X(VPX_decal_walk, "index walk: columns inner, rows outer (heads +0x18, cols +0x1C)", "8B 71 08 BA 01 00 00 00 53 01 51 10 39 71 10 7C 10 8B 01 01 51 14 89 41 10 8B 41 14 3B 41 0C 7D 1E 8B 41 28 8B 58 1C 0F AF 59 14 03 59 10 8B 40 18 8B 04 98")
	X(VPX_decal_query_end, "index query: the cell range ends at trunc(...) + 1", "F3 0F 2C C1 40 3B C1 0F 9C C2")
	X(VPX_decal_query_take, "index query: an item is taken when min <= point < max", "F3 0F 10 44 D0 08 0F 2F 46 18 F3 0F 10 4C D0 0C 8D 04 D0 72 1A F3 0F 10 56 20 0F 2F D0 76 10 0F 2F 4E 1C 72 0A F3 0F 10 46 24 0F 2F C1 77 17")
	X(VPX_decal_draw_grow, "decal draw: the tile rectangle grown by 256", "F3 0F 10 25 R(decal_k_256) 53 55 56 57 8B D9")
	X(VPX_decal_draw_query, "decal draw: the query on the index TDS+0x6C", "F3 0F 5C D4 F3 0F 5C DC F3 0F 58 C4 F3 0F 58 CC 8D 4B 6C")
	X(VPX_decal_draw_large, "decal draw: the large list from its head TDS+0xAC", "8B BB AC 00 00 00 89 7C 24 24 81 FF FF FF FF 0F")
	X(VPX_decal_draw_next, "decal draw: the large list's next (node array TDS+0x9C, node +0x00)", "8B 93 9C 00 00 00 8B 0C 17")
	X(VPX_decal_sort, "the draw's sort: insertion sort at 32 entries or fewer", "B8 AB AA AA 2A F7 E9 D1 FA 8B C2 C1 E8 1F 03 C2 83 F8 20 0F 8E B0 00 00 00")
	X(VPX_decal_bake_rect, "tile bake: the decal tile rectangle (+0xDC, +0xE0) - 1 .. + the x extent +0xD4", "8B 85 DC 00 00 00 DE C9 8D 48 FF F3 0F 2A C1 8B 8D E0 00 00 00 D8 F9 83 EC 18 8D 51 FF F3 0F 2A CA 8B 95 D4 00 00 00 03 C2")
};
#undef X
#define DT_NSIGS ((int)(sizeof(kDtSigs) / sizeof(kDtSigs[0])))
static const int kDtNeed[] = { VPX_bake_ts, VPX_decal_resmgr, VPX_decal_farm_owner };   // read besides the pattern sites
static int g_build = kVpExeBuildUnknown;   // the EXE build the patterns matched on
static const char* const kFarmCats[DT_FARM_CATS] = { "Crops_Half_Built", "Crops_Africa", "Crops_America", "Crops_Asia",
	"Crops_Europe", "Crops_Wheat", "Crops_Wheat_HB", "Crops_Damage" };
static const char* const kAttrs[3] = { "albedo", "heightmap", "spec" };

// ---- state -------------------------------------------------------------------------------------------------------------
struct Decal                     // one live decal as read
{
	uint64_t hash;
	float rect[4];
	float priority;
	uint32_t pass;
	uint32_t tex[3];             // the EXE's handles
	uint32_t tris;
	uint32_t vOff;               // byte offset of its vertices in its batch's vertex store
	uint32_t slot;               // 0366: its slot in the batch's record pool (the index and the large list name it so)
};
struct Batch
{
	unsigned char sig[DT_BATCH]; // the batch's vector entry as last read completely
	Decal* d; uint32_t n, cap;
	unsigned char* v; uint32_t vBytes, vCap;
	uint64_t poolHash;           // hash of the live records (the safety net)
	bool valid, moved;
	uint32_t layoutTries;
	uint32_t readSeq;            // raised by every complete read and every invalidation (the build's snapshot check)
	// a read in two parts (4110): the copy out of the EXE (ReadBatchCopy), then its checks and hashes (ReadBatchWork)
	unsigned char* rawPool; unsigned char* rawVerts; uint32_t* rawOrder;
	uint32_t rawN, rawI, rawVUsed, rawVerts_n; uint64_t rawPh; unsigned char rawSig[DT_BATCH]; bool rawPending;
	// 0649: live records outside the world's x range (OutsideX), left out of the set: their slots (the walk skips the index
	// items and large-list nodes that name them), how many, the first one's rectangle
	uint32_t* outSlots; uint32_t outN, outCap; float outFirst[4];
};
struct Pub { uint64_t hash; float rect[4]; };   // a published decal for the diff

static Batch* g_b; static uint32_t g_nb, g_capB;
static unsigned char g_hdr[DT_HDR_BYTES], g_vec[DT_MAX_BATCHES * DT_BATCH];
static uint64_t g_sigHash; static bool g_haveSig;
static uint32_t g_quiet; static int64_t g_firstMoveQpc;
static bool g_init, g_dead, g_patternsOk;
static uint32_t g_state = VPDTAP_NONE, g_fail;
static uint32_t g_generation, g_loadSeq;
static HANDLE g_ctlMap; static VPShellDecalTapControl* g_ctl;
static HANDLE g_secCur, g_secPrev; static uint32_t g_secCurGen;
static wchar_t g_imageName[64];
static uint64_t g_imageBytes, g_fnv; static int64_t g_readyQpc;
static float g_copyMs, g_pollMs;
static uint32_t g_publishCount, g_refused, g_sehCount;
static Pub* g_pub; static uint32_t g_nPub;          // the current generation's set, sorted by hash
static unsigned char* g_areaPub; static uint32_t g_areaPubX, g_areaPubY, g_areaPubSet;   // 4111: its tiles' +0x300 (y * x + x)
static unsigned char* g_walkPub; static uint32_t g_walkPubBytes, g_walkPubSmall, g_walkPubLarge;   // 0366: its walk block
static uint32_t g_walkMisses; static bool g_walkDead;   // 0366: walk checks that missed; a fault: no walk for good
// 0406: a miss against a snapshot that its counters call intact, while the published generation has the walk, holds the
// build (the batch it names read again, the build repeated) instead of publishing without the block; at most
// g_walkHoldMax in a row (ini decal_walk_holds, env VPSHELL_DECAL_WALK_HOLDS, 0..16, default 4; 0 = the 0366 behaviour;
// terrain-decals-walk.md 2)
static uint32_t g_walkHoldMax = 4;
static uint32_t g_walkHolds, g_walkHoldRun;   // holds in all; holds of the publication in progress
static uint32_t g_walkMissBatch;              // the batch the last miss names (DT_NIL: none)
static bool g_walkRetry;                      // a hold asks the poll for a new build
static Pub* g_base; static uint32_t g_nBase; static uint32_t g_baseGen;   // the last acknowledged generation's set
static uint32_t g_lastAck, g_lastAdded, g_lastRemoved, g_changedSince;
static int64_t g_netQpc;
static double g_readCpuMs; static bool g_reading;
static double g_qpcMs;
static double g_pollSum; static uint32_t g_pollN;
static void (*g_log)(const char*, ...);
static uint32_t g_logs;
static char g_why[256], g_lastLoud[256]; static uint32_t g_loudRepeat;
static wchar_t g_dir[MAX_PATH];
static uint32_t g_budgetMs = 2, g_settleFrames = 2, g_maxWaitMs = 500, g_netMs = 1000;
static uint64_t g_maxBytes = VPSHELL_DECAL_MAX_BYTES;
static int g_test; static uint32_t g_testChanges; static bool g_testBroken;
static bool g_namesFailed;
static uint32_t g_pubVersion, g_wantVersion;
static uint32_t g_hdrTries; static bool g_firstPending; static uint32_t g_netNext = 0xFFFFFFFFu;

// A publication in progress (issue 4110: the build spread over Presents, then the hash and the section as before).
// ST_BUILD walks the steps B_* below on the tap's own copies of the batches, which no read changes while a stage is in
// progress (the snapshot); a batch that moves meanwhile restarts the build (at most DT_BUILD_RESTARTS times in a row,
// then the snapshot is published and the moved batch read after it).
enum { ST_NONE, ST_BUILD, ST_HASH, ST_SECTION };
enum { B_AREA, B_WALK, B_SET, B_MERGE, B_SAME, B_USED, B_NAMES, B_DIFF, B_ALLOC, B_RECORDS, B_FINISH };
struct Stage
{
	int stage, step; uint32_t cur;              // build step; its batch or merge-pair cursor
	uint32_t nb;                                // batches of the snapshot
	uint32_t* readSeq;                          // each batch's readSeq when the build began (the snapshot check)
	uint32_t* runs; uint32_t* runs2; uint32_t nRuns, nRuns2;   // sorted runs of `set` (boundaries) for the merge
	Pub* set; Pub* tmp; Pub* ch; uint32_t* nameOf;
	unsigned char* img; uint64_t total, pos, fnv; HANDLE sec;
	uint64_t decalOff, nameOff, vertOff, chOff, hashOff, areaOff;
	unsigned char* area; uint32_t areaX, areaY, areaSet;   // 4111: the tiles' +0x300 (per tile 0 / 1, y * areaX + x)
	unsigned char* walk; uint64_t walkOff; uint32_t walkBytes, walkSmall, walkLarge;   // 0366: the walk block (null: none)
	uint32_t n, vBytes, used, nNames, nv, nChanged, added, removed, presents, steps, baseGen;
	bool midDone, limitLogged;
	int64_t startQpc; float buildMs, cpuMs; char why[64];
};
static Stage g_st;
#define DT_BUILD_RESTARTS 3
static uint32_t g_buildRestarts;             // restarts of the current publication (reset when it ends)
// per-step cost estimates (ms per unit, decaying maximum): a step runs only when its estimate still fits the budget,
// except the first step of a Present (progress); see Fits / Learn
struct Cost { double per; double winMax; const char* name; };
static Cost g_cRead = { 0, 0, "read" }, g_cSet = { 0, 0, "set" }, g_cMerge = { 0, 0, "merge" }, g_cSame = { 0, 0, "same" },
	g_cUsed = { 0, 0, "used" }, g_cNames = { 0, 0, "names" }, g_cDiff = { 0, 0, "diff" }, g_cAlloc = { 0, 0, "alloc" },
	g_cRec = { 0, 0, "records" }, g_cFinish = { 0, 0, "finish" }, g_cHash = { 0, 0, "hash" }, g_cSec = { 0, 0, "section" },
	g_cWork = { 0, 0, "check+hash" }, g_cArea = { 0, 0, "area" }, g_cWalk = { 0, 0, "walk" }, g_cWalkRead = { 0, 0, "walk read" };
static Cost* const kCosts[] = { &g_cRead, &g_cWork, &g_cArea, &g_cWalkRead, &g_cWalk, &g_cSet, &g_cMerge, &g_cSame, &g_cUsed, &g_cNames, &g_cDiff, &g_cAlloc,
	&g_cRec, &g_cFinish, &g_cHash, &g_cSec };
static bool g_stepDone;                       // a step of read, build or publish ran in this Present
// the cost of every Present from the first read of a change to READY (or to "no change"): logged when it ends
static bool g_winActive, g_winEnd; static double g_winMax, g_winSum; static uint32_t g_winN, g_winRestarts, g_winGen;
static char g_winWhat[48];
static int64_t g_advQpc;                      // when Advance began in this Present
static uint32_t g_midBuild;                   // test: changes of the huge world made during a build (decal_test=huge)
static bool g_midAtArea;                      // 0406 test: those changes come at the build's first step (before the walk)

struct Addr { uintptr_t tds, resmgr, farm, trSlot; };
static Addr A;

enum { T_NONE, T_OK, T_PASTCHUNK, T_NONAME, T_HUGECOUNT, T_GARBAGE, T_BADRECT, T_HUGE, T_BADGRID, T_BADTILE, T_NOTR, T_WALK, T_WALKMISS,
	T_WALKFAULT, T_WALKSTALE, T_WALKMISSLATE, T_PATTERNS, T_SEAM, T_SEAMY, T_COUNT };
static const char* kTestNames[T_COUNT] = { "", "ok", "pastchunk", "noname", "hugecount", "garbage", "badrect", "huge",
	"badgrid", "badtile", "notr", "walk", "walkmiss", "walkfault", "walkstale", "walkmisslate", "patterns", "seam", "seamy" };
static int g_testPattern = -1;                  // decal_test=patterns: the pattern broken in the fake image (-1 none)
static char g_testBuild[32], g_testDrop[96];    // its build (steam id; "0" = no known build), a symbol made not carried
static const char* kFailNames[] = { "NONE", "PATTERN", "SEH", "LAYOUT", "TOO_BIG", "SECTION", "NAMES", "ACK_TIMEOUT" };

#define DLOG(...) do { if (g_log && g_logs < 2000) { g_logs++; g_log(__VA_ARGS__); } } while (0)

static int64_t Qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static double Ms(int64_t d) { return d * g_qpcMs; }

// ---- guarded reads (bake_tap.cpp / water_tap.cpp: a loop under SEH, never memcpy on a wild address) -------------------
#if defined(__SANITIZE_ADDRESS__)
__declspec(no_sanitize_address)
#endif
static bool SafeRead(uintptr_t a, void* out, size_t n, bool* fault)
{
	if (a < 0x10000 || n == 0 || n > 0x800000 || a + n < a || (uint64_t)a + n > 0xFFFF0000ull) return false;
	__try
	{
		unsigned char* d = (unsigned char*)out;
		size_t i = 0;
		if (!(a & 3) && n >= 64)
		{
			volatile const uint32_t* s4 = (volatile const uint32_t*)a;
			for (; i + 4 <= n; i += 4) { uint32_t v = s4[i / 4]; memcpy(d + i, &v, 4); }
		}
		volatile const unsigned char* s = (volatile const unsigned char*)a;
		for (; i < n; ++i) d[i] = s[i];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*fault = true;
		return false;
	}
}

static inline uint32_t U32(const unsigned char* b, uint32_t off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
static inline float F32(const unsigned char* b, uint32_t off) { float v; memcpy(&v, b + off, 4); return v; }
static inline uint16_t U16(const unsigned char* b, uint32_t off) { uint16_t v; memcpy(&v, b + off, 2); return v; }
static void Put32(unsigned char* b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }
static void PutF(unsigned char* b, uint32_t off, float v) { memcpy(b + off, &v, 4); }

static uint64_t Fnv(uint64_t h, const void* p, size_t n)
{
	const unsigned char* c = (const unsigned char*)p;
	for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 0x100000001b3ull; }
	return h;
}

// The safety net's hash of one record's bytes +0x08..+0x33 (11 dwords): a word-wise multiply-xor, about 4x cheaper than
// byte-wise FNV over the ~0.8 MB of live records of the huge save; only compared with itself (ReadBatch against NetCheck).
static inline uint64_t RecHash(uint64_t h, const unsigned char* r)
{
	for (uint32_t o = 8; o < DT_REC; o += 4) { uint32_t w; memcpy(&w, r + o, 4); h = (h ^ w) * 0x100000001b3ull; }
	return h;
}

// A string of the EXE's heap, read page by page (a name near a page end must not fault on the next page).
static bool SafeStr(uintptr_t a, char* out, uint32_t cap, bool* fault)
{
	uint32_t n = 0;
	while (n + 1 < cap)
	{
		uint32_t pageLeft = 0x1000u - (uint32_t)((a + n) & 0xFFF);
		uint32_t want = cap - 1 - n < pageLeft ? cap - 1 - n : pageLeft;
		if (want > 64) want = 64;
		if (!SafeRead(a + n, out + n, want, fault)) return false;
		for (uint32_t i = 0; i < want; ++i) if (!out[n + i]) return true;
		n += want;
	}
	out[cap - 1] = 0;
	return true;   // cut at cap - 1 (VPSHELL_DECAL_NAME_CHARS: no texture name of the game is that long)
}

// ---- LOUD lines (D132): a new text at once, the same text again only every 1000th time --------------------------------
static void Loud(const char* fmt, ...)
{
	char b[400];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, ap); va_end(ap);
	if (!strcmp(b, g_lastLoud) && (++g_loudRepeat % 1000) != 0) return;
	if (strcmp(b, g_lastLoud)) { strncpy_s(g_lastLoud, b, _TRUNCATE); g_loudRepeat = 0; }
	if (g_log) g_log("decal_tap: LOUD: %s", b);
}

// ---- the synthetic EXE (test mode) -----------------------------------------------------------------------------------
// shell/tests/test_decal_tap.py recomputes it. Decal spec k: batch, priority, pass, handles, rectangle, triangles; the
// vertex bytes of decal id i are byte j = (13 j + 7 i + 1) & 0xFF. Resource map names: handle 100 + i -> kTestTex[i];
// farm table: Crops_Africa child 0 = handles 200, 201, spec 202 (the farm default); TDS+0x4F0 = 1975.
struct TestDecal { uint32_t id, batch; float prio; uint32_t pass, tex[3]; float rect[4]; uint32_t tris; bool live; uint32_t slot; };

// ---- the synthetic spatial index and large list (issue 0366; terrain-decals.md 18) ------------------------------------
// TestIndexBuild lays the live decals in add order (ascending age) as AddDecal does: a small one (both sides at most 256)
// as an item at the head of the list of the cell of its rectangle's centre, a large one at the tail of the large list.
// Item 0 is a free item (bit 0) that no list holds. The index's grid is cols x rows over the world rectangle TDS+0x6C;
// its inverse cell size cols / (max - min) in single precision. shell/tests/test_decal_tap.py recomputes it. Test
// modes: walkmiss (an item names a slot that holds no live record: no walk block), walkfault (the item pool pointer
// unmapped: no walk block for the life of the process); 0406: walkmisslate (walkmiss's item from the first change on, after
// a generation with the walk), walkstale (the change also rewrites decal 2 in batch 0 in place, the batch's counters
// equal, as the EXE did in play: terrain-decals-walk.md 1).
// the cell of a point on one axis as the index cell function (decal_cell) computes it (single precision, truncation, clamped); also the tap's check
static float WalkMul(float a, float b) { volatile float r = a * b; return r; }
static float WalkSub(float a, float b) { volatile float r = a - b; return r; }
static int32_t WalkTrunc(float v)   // cvttss2si: out of range (or NaN) gives 0x80000000
{
	if (!(v > -2147483648.0f && v < 2147483648.0f)) return INT32_MIN;
	return (int32_t)v;
}
static uint32_t WalkCell(float p, float origin, float inv, uint32_t n)   // the index cell function (decal_cell) on one axis
{
	int32_t c = WalkTrunc(WalkMul(WalkSub(p, origin), inv));
	if (c > (int32_t)n - 1) c = (int32_t)n - 1;
	if (c < 0) c = 0;
	return (uint32_t)c;
}

struct TestIndex { uint32_t cols, rows; uint32_t* heads; unsigned char* items; uint32_t capItems; unsigned char* nodes; uint32_t capNodes; };
struct TestIxEntry { uint32_t batch, slot, age; float rect[4]; };
static void* g_walkReserved;
static int CmpIxAge(const void* a, const void* b)
{
	const uint32_t x = ((const TestIxEntry*)a)->age, y = ((const TestIxEntry*)b)->age;
	return x < y ? -1 : x > y ? 1 : 0;
}
static bool TestIndexAlloc(TestIndex& x, uint32_t cols, uint32_t rows, uint32_t capItems, uint32_t capNodes)
{
	x.cols = cols; x.rows = rows; x.capItems = capItems; x.capNodes = capNodes;
	x.heads = (uint32_t*)calloc((size_t)cols * rows, 4);
	x.items = (unsigned char*)calloc(capItems, 24);
	x.nodes = (unsigned char*)calloc(capNodes, 16);
	return x.heads && x.items && x.nodes;
}
static void TestIndexBuild(unsigned char* tds, TestIndex& x, TestIxEntry* e, uint32_t n)
{
	const float minx = F32(tds, 0x6C), miny = F32(tds, 0x70), maxx = F32(tds, 0x74), maxy = F32(tds, 0x78);
	volatile float spanx = maxx - minx, spany = maxy - miny;
	volatile float invx = (float)x.cols / spanx, invy = (float)x.rows / spany;
	PutF(tds, 0x7C, invx); PutF(tds, 0x80, invy);
	Put32(tds, 0x84, (uint32_t)(uintptr_t)x.heads); Put32(tds, 0x88, x.cols); Put32(tds, 0x8C, x.rows);
	Put32(tds, 0x90, g_test == T_WALKFAULT ? (uint32_t)(uintptr_t)g_walkReserved : (uint32_t)(uintptr_t)(tds + 0x4DC));
	for (uint32_t c = 0; c < x.cols * x.rows; ++c) x.heads[c] = DT_NIL;
	qsort(e, n, sizeof(TestIxEntry), CmpIxAge);
	memset(x.items, 0, (size_t)x.capItems * 24);
	Put32(x.items, 0, (DT_NIL << 1) | 1u);    // item 0: free
	uint32_t nItems = 1, nNodes = 0, head = DT_NIL, tail = DT_NIL;
	for (uint32_t i = 0; i < n && nItems < x.capItems && nNodes < x.capNodes; ++i)
	{
		const float* r = e[i].rect;
		volatile float w = r[2] - r[0], h = r[3] - r[1];
		if (w > 256.0f || h > 256.0f)
		{
			unsigned char* nd = x.nodes + 16 * nNodes;
			Put32(nd, 0, DT_NIL); Put32(nd, 4, tail); Put32(nd, 8, e[i].batch); Put32(nd, 12, e[i].slot);
			if (tail != DT_NIL) Put32(x.nodes + 16 * tail, 0, nNodes); else head = nNodes;
			tail = nNodes++;
			continue;
		}
		volatile float sx = r[0] + r[2], sy = r[1] + r[3];
		volatile float px = sx * 0.5f, py = sy * 0.5f;
		const uint32_t c = WalkCell(px, minx, invx, x.cols) + WalkCell(py, miny, invy, x.rows) * x.cols;
		unsigned char* it = x.items + 24 * nItems;
		Put32(it, 0, x.heads[c] << 1); Put32(it, 4, DT_NIL); PutF(it, 8, px); PutF(it, 12, py);
		Put32(it, 16, e[i].batch); Put32(it, 20, e[i].slot);
		if (x.heads[c] != DT_NIL) Put32(x.items + 24 * x.heads[c], 4, nItems);
		x.heads[c] = nItems++;
	}
	if ((g_test == T_WALKMISS || (g_test == T_WALKMISSLATE && g_testChanges)) && nItems > 1)
		Put32(x.items + 24 * (nItems - 1), 20, 15);   // a slot that holds no live record
	unsigned char* pool = tds + 0x4DC;
	Put32(pool, 0, 0); Put32(pool, 4, nItems - 1); Put32(pool, 8, (uint32_t)(uintptr_t)x.items); Put32(pool, 0xC, nItems); Put32(pool, 0x10, x.capItems);
	Put32(tds, 0x9C, (uint32_t)(uintptr_t)x.nodes); Put32(tds, 0xA8, nNodes); Put32(tds, 0xAC, head); Put32(tds, 0xB0, tail);
}
static const char* const kTestTex[] = { "RoadsandRails_new_D.dds", "RoadsandRails_new_H.dds", "RoadsandRails_new_SREF.dds",
	"riverbank_D.dds", "grey.dds", "CityBase_Diff_anc.dds", "CityBase_HT.dds", "CityBase_SREF.dds" };
struct TestWorld
{
	unsigned char tds[0x500];
	unsigned char vec[4 * DT_BATCH];
	unsigned char pool[2][16 * DT_REC];
	unsigned char chunk[2][4096];
	// the resource manager: object, the type map (head + 2 nodes), the texture map (head + 8 nodes), resources, objects
	unsigned char mgr[0x60];
	unsigned char typeHead[0x18], typeNode[2][0x18];
	unsigned char texMap[0xC], texHead[0x18], texNode[8][0x18];
	unsigned char res[8][0x20], obj[8][0x0C];
	char names[8][40];
	unsigned char farm[DT_FARM_BYTES];
	TestDecal decals[16]; uint32_t nDecals;
	void* reserved;
	TestIndex ix;                // 0366: the spatial index (14 x 10 cells) and the large list
};
static TestWorld* g_tw;

// ---- the synthetic tile grid (issue 4111): a TR with 56 x 40 tile pointers (column-major), each tile's +0x300 --------
// Both test worlds use it. TestMarkRect marks the tiles floor(min / 64) .. floor(max / 64) of a rectangle, as the area invalidation
// does with a decal's rectangle. The small world marks every live decal but the river bed (priority 0.4: the EXE leaves
// +0x300 at 0 under some river decals, terrain-decals.md 13.5) and tile (40, 30) alone (a terrain edit); its change marks
// the removed and the added decal's rectangles; `shell_composite.areachange` marks tile (20, 20) alone (only the flags
// change). The huge world marks every tile whose column-major index is a multiple of 5. Test modes: badgrid (the count
// is not tiles x * tiles y), badtile (tile (3, 5) holds 7), notr (no TerrainRenderer: no area block).
// shell/tests/test_decal_tap.py recomputes it.
#define TG_TX 56u
#define TG_TY 40u
#define TG_TILE 0x304u
struct TestGrid { uint32_t trPtr; unsigned char tr[0x2340]; uint32_t ptrs[TG_TX * TG_TY]; unsigned char* tiles; };
static TestGrid* g_tg;

static void TestSetArea(uint32_t x, uint32_t y, uint32_t v)
{
	if (g_tg && x < TG_TX && y < TG_TY) Put32(g_tg->tiles + (size_t)(x * TG_TY + y) * TG_TILE, DT_T_AREA, v);
}

static void TestMarkRect(const float r[4])
{
	const int x0 = (int)floorf(r[0] / 64.0f), x1 = (int)floorf(r[2] / 64.0f), y0 = (int)floorf(r[1] / 64.0f), y1 = (int)floorf(r[3] / 64.0f);
	for (int y = y0 < 0 ? 0 : y0; y <= y1; ++y)
		for (int x = x0 < 0 ? 0 : x0; x <= x1; ++x) TestSetArea((uint32_t)x, (uint32_t)y, 1);
}

static bool BuildTestGrid()
{
	g_tg = (TestGrid*)calloc(1, sizeof(TestGrid));
	if (!g_tg) return false;
	g_tg->tiles = (unsigned char*)calloc(TG_TX * TG_TY, TG_TILE);
	if (!g_tg->tiles) return false;
	Put32(g_tg->tr, DT_TR_TX, TG_TX); Put32(g_tg->tr, DT_TR_TX + 4, TG_TY);
	Put32(g_tg->tr, DT_TR_TX + 8, g_test == T_BADGRID ? TG_TX * TG_TY + 1 : TG_TX * TG_TY);
	Put32(g_tg->tr, DT_TR_GRID, (uint32_t)(uintptr_t)g_tg->ptrs);
	for (uint32_t i = 0; i < TG_TX * TG_TY; ++i) g_tg->ptrs[i] = (uint32_t)(uintptr_t)(g_tg->tiles + (size_t)i * TG_TILE);
	g_tg->trPtr = g_test == T_NOTR ? 0u : (uint32_t)(uintptr_t)g_tg->tr;
	A.trSlot = (uintptr_t)&g_tg->trPtr;
	return true;
}

static void TestVerts(unsigned char* p, uint32_t id, uint32_t n)
{
	for (uint32_t j = 0; j < n; ++j) p[j] = (unsigned char)((13 * j + 7 * id + 1) & 0xFF);
}

// Lays the live decals of g_tw->decals into the batches: pools (slot = position, a dead slot kept free), lists, chunks.
static void TestLayout()
{
	TestWorld& w = *g_tw;
	for (uint32_t b = 0; b < 2; ++b)
	{
		memset(w.pool[b], 0, sizeof(w.pool[b])); memset(w.chunk[b], 0, sizeof(w.chunk[b]));
		uint32_t slot = 0, live = 0, verts = 0, prev = DT_NIL, head = DT_NIL, freeHead = DT_NIL;
		for (uint32_t k = 0; k < w.nDecals; ++k)
		{
			TestDecal& t = w.decals[k];
			if (t.batch != b) continue;
			unsigned char* r = w.pool[b] + DT_REC * slot;
			t.slot = slot;
			if (!t.live) { Put32(r, 0, freeHead); freeHead = slot; slot++; continue; }   // a freed slot
			if (prev != DT_NIL) Put32(w.pool[b] + DT_REC * prev, 0, slot); else head = slot;
			Put32(r, 0, DT_NIL); Put32(r, 4, 0x80000000u | (prev == DT_NIL ? DT_NIL : prev));
			Put32(r, 8, 1u | (t.pass << 1) | 0xAB00u);      // bit 0 uploaded, bit 1 pass, garbage above (as the EXE)
			Put32(r, 0xC, verts); Put32(r, 0x10, t.tris);
			for (int i = 0; i < 3; ++i) Put32(r, 0x14 + 4 * i, t.tex[i]);
			PutF(r, 0x20, t.prio);
			for (int i = 0; i < 4; ++i) PutF(r, 0x24 + 4 * i, t.rect[i]);
			uint32_t nb = 24 * t.tris;
			if (verts * 8 + nb <= sizeof(w.chunk[b])) TestVerts(w.chunk[b] + verts * 8, t.id, nb);
			verts += 3 * t.tris;
			prev = slot; slot++; live++;
		}
		if ((g_test == T_PASTCHUNK || g_testBroken) && b == 1) Put32(w.pool[1], DT_REC + 0xC, 9000);   // slot 1's run past the chunk
		unsigned char* v = w.vec + DT_BATCH * b;
		memset(v, 0, DT_BATCH);
		Put32(v, 0x00, freeHead); Put32(v, 0x04, live); Put32(v, 0x08, (uint32_t)(uintptr_t)w.pool[b]); Put32(v, 0x0C, slot);
		Put32(v, 0x10, 16); Put32(v, 0x14, live); Put32(v, 0x18, head); Put32(v, 0x1C, prev == DT_NIL ? DT_NIL : prev);
		Put32(v, 0x20, 5 + b); Put32(v, 0x24, verts); Put32(v, 0x28, (uint32_t)(uintptr_t)w.chunk[b]);
	}
	// 0366: the index of the live decals, added in the order of the spec list (age = position)
	if (w.ix.heads)
	{
		TestIxEntry e[16]; uint32_t n = 0;
		for (uint32_t k = 0; k < w.nDecals; ++k)
			if (w.decals[k].live) { e[n].batch = w.decals[k].batch; e[n].slot = w.decals[k].slot; e[n].age = k; memcpy(e[n].rect, w.decals[k].rect, 16); n++; }
		TestIndexBuild(w.tds, w.ix, e, n);
	}
}

static void TestTreeNode(unsigned char* n, unsigned char* head, unsigned char* right, uint32_t key, uint32_t val)
{
	memset(n, 0, 0x18);
	Put32(n, 0, (uint32_t)(uintptr_t)head); Put32(n, 4, (uint32_t)(uintptr_t)head); Put32(n, 8, (uint32_t)(uintptr_t)right);
	Put32(n, 0xC, key); Put32(n, 0x10, val);
}

static bool BuildTestWorld()
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return false;
	TestWorld& w = *g_tw;
	const TestDecal ds[] = {
		{ 1, 0, 0.7f, 0, { 100, 101, 102 }, { 10.0f, 20.0f, 74.5f, 60.25f }, 4, true },       // route curve
		{ 2, 0, 0.5f, 0, { 200, 201, 202 }, { 100.0f, 100.0f, 140.0f, 132.0f }, 2, true },    // farm field
		{ 3, 0, 0.6f, 0, { 0, 0, 1975 }, { 96.0f, 90.0f, 150.0f, 140.0f }, 3, true },         // farm track (handle 0)
		{ 9, 1, 0.0f, 0, { 105, 106, 107 }, { 0.0f, 0.0f, 1.0f, 1.0f }, 1, false },          // a freed slot
		{ 4, 1, 0.4f, 0, { 103, 104, 1975 }, { 300.0f, 200.0f, 420.0f, 230.0f }, 5, true },   // river bed
		{ 5, 1, 0.0f, 0, { 105, 106, 107 }, { 500.0f, 500.0f, 564.0f, 564.0f }, 2, true },    // city ground
		{ 6, 1, 0.1f, 1, { 103, 103, 1975 }, { 600.0f, 600.0f, 610.0f, 612.0f }, 1, true },   // pass 1 (macro shadow)
	};
	w.nDecals = sizeof(ds) / sizeof(ds[0]);
	memcpy(w.decals, ds, sizeof(ds));
	if (g_test == T_WALK)   // 0366: a large decal, a tie in decal 1's cell (added later: the head) and one in the next cell
	{
		w.decals[w.nDecals++] = { 10, 0, 0.6f, 0, { 100, 101, 102 }, { 1000.0f, 800.0f, 1400.0f, 900.0f }, 2, true };
		w.decals[w.nDecals++] = { 11, 1, 0.7f, 0, { 100, 101, 102 }, { 20.0f, 30.0f, 60.0f, 50.0f }, 1, true };
		w.decals[w.nDecals++] = { 12, 1, 0.7f, 0, { 100, 101, 102 }, { 270.0f, 30.0f, 300.0f, 50.0f }, 1, true };
	}
	if (g_test == T_SEAM)   // 0649: records outside the world's x range as AddDecal clamps them (left out of the set)
	{
		w.decals[w.nDecals++] = { 13, 0, 0.7f, 0, { 100, 101, 102 }, { 3590.25f, 1505.4f, 3583.0f, 1564.42f }, 2, true };   // past the east edge
		w.decals[w.nDecals++] = { 14, 1, 0.7f, 0, { 100, 101, 102 }, { 0.0f, 300.0f, -12.5f, 340.0f }, 1, true };          // past the west edge
		w.decals[w.nDecals++] = { 15, 1, 0.7f, 0, { 100, 101, 102 }, { 3600.0f, 1000.0f, 3583.0f, 1300.0f }, 1, true };    // east, 300 high: large list
	}
	if (g_test == T_SEAMY)  // 0649 negative: the same form in y (past the north edge) still fails the layout check
		w.decals[w.nDecals++] = { 13, 0, 0.7f, 0, { 100, 101, 102 }, { 1000.0f, 2570.0f, 1040.0f, 2559.0f }, 2, true };
	if (g_test == T_WALKFAULT) g_walkReserved = VirtualAlloc(nullptr, 1 << 16, MEM_RESERVE, PAGE_NOACCESS);
	if (!TestIndexAlloc(w.ix, 14, 10, 64, 16)) return false;
	if (g_test == T_NONAME) w.decals[5].tex[2] = 150;                                     // a handle no table names
	if (g_test == T_BADRECT) w.decals[0].rect[2] = -5.0f;
	PutF(w.tds, 0x6C, 0.0f); PutF(w.tds, 0x70, 0.0f); PutF(w.tds, 0x74, 3583.0f); PutF(w.tds, 0x78, 2559.0f);
	Put32(w.tds, 0xAC, DT_NIL); Put32(w.tds, 0xB0, DT_NIL);
	Put32(w.tds, 0xB4, (uint32_t)(uintptr_t)w.vec); Put32(w.tds, 0xB8, g_test == T_HUGECOUNT ? 100000u : 2u); Put32(w.tds, 0xBC, 4);
	Put32(w.tds, 0x4F0, 1975);
	if (g_test == T_GARBAGE)
	{
		w.reserved = VirtualAlloc(nullptr, 1 << 16, MEM_RESERVE, PAGE_NOACCESS);
		Put32(w.tds, 0xB4, (uint32_t)(uintptr_t)w.reserved);
	}
	TestLayout();
	if (!BuildTestGrid()) return false;
	for (uint32_t k = 0; k < w.nDecals; ++k) if (w.decals[k].live && w.decals[k].prio != 0.4f) TestMarkRect(w.decals[k].rect);
	TestSetArea(40, 30, 1);
	if (g_test == T_BADTILE) TestSetArea(3, 5, 7);
	// the resource manager: type map {0 -> nothing, 8 -> the texture map}, a chain of nodes (in order by right links)
	memset(w.typeHead, 0, 0x18); w.typeHead[0x15] = 1;
	TestTreeNode(w.typeNode[0], w.typeHead, w.typeNode[1], 0, 0);
	TestTreeNode(w.typeNode[1], w.typeHead, w.typeHead, 8, (uint32_t)(uintptr_t)w.texMap);
	Put32(w.typeHead, 4, (uint32_t)(uintptr_t)w.typeNode[0]);
	Put32(w.mgr, 0x44, (uint32_t)(uintptr_t)w.typeHead); Put32(w.mgr, 0x48, 2);
	memset(w.texHead, 0, 0x18); w.texHead[0x15] = 1;
	for (uint32_t i = 0; i < 8; ++i)
	{
		strcpy_s(w.names[i], kTestTex[i]);
		Put32(w.obj[i], 8, 100 + i);
		Put32(w.res[i], 0x1C, (uint32_t)(uintptr_t)w.obj[i]);
		TestTreeNode(w.texNode[i], w.texHead, i + 1 < 8 ? w.texNode[i + 1] : w.texHead, (uint32_t)(uintptr_t)w.names[i],
			(uint32_t)(uintptr_t)w.res[i]);
	}
	Put32(w.texHead, 4, (uint32_t)(uintptr_t)w.texNode[0]);
	Put32(w.texMap, 4, (uint32_t)(uintptr_t)w.texHead); Put32(w.texMap, 8, 8);
	// the farm table: Crops_Half_Built child 0 (198, 199), Crops_Africa child 0 (200, 201), spec = the default 202
	Put32(w.farm, 0x24, 0x00010000u); Put32(w.farm, 0x28, 0x00010001u);
	for (uint32_t k = 2; k < DT_FARM_CATS; ++k) Put32(w.farm, 0x24 + 4 * k, 0x00000002u);
	Put32(w.farm, 0x44, (uint32_t)(uintptr_t)(w.farm + 0x50)); Put32(w.farm, 0x48, 2); Put32(w.farm, 0x4C, 64);
	unsigned char* v = w.farm + 0x50;
	PutF(v, 0, 0.5f); Put32(v, 4, 198); Put32(v, 8, 199); Put32(v, 12, 202);
	PutF(v, 16, 0.5f); Put32(v, 20, 200); Put32(v, 24, 201); Put32(v, 28, 202);
	Put32(w.farm, 0x454, 202);
	return true;
}

// The test's trigger: the river bed (id 4) is removed and a road piece (id 7) added to batch 1.
static void TestChange()
{
	TestWorld& w = *g_tw;
	for (uint32_t k = 0; k < w.nDecals; ++k) if (w.decals[k].id == 4) { w.decals[k].live = false; TestMarkRect(w.decals[k].rect); }
	if (w.nDecals < 16)
	{
		w.decals[w.nDecals++] = { 7, 1, 0.7f, 0, { 100, 101, 102 }, { 700.0f, 300.0f, 760.0f, 340.0f }, 2, true };
		TestMarkRect(w.decals[w.nDecals - 1].rect);
	}
	if (g_test == T_WALKSTALE)   // 0406: decal 2 (batch 0, slot 1) rewritten in place: same slot, same triangles, counters equal
		for (uint32_t k = 0; k < w.nDecals; ++k)
			if (w.decals[k].id == 2) { const float r[4] = { 104.0f, 98.0f, 146.0f, 136.0f }; memcpy(w.decals[k].rect, r, 16); TestMarkRect(r); }
	TestLayout();
}

static void FreeTestWorld()
{
	if (!g_tw) return;
	if (g_tw->reserved) VirtualFree(g_tw->reserved, 0, MEM_RELEASE);
	free(g_tw->ix.heads); free(g_tw->ix.items); free(g_tw->ix.nodes);
	if (g_walkReserved) { VirtualFree(g_walkReserved, 0, MEM_RELEASE); g_walkReserved = nullptr; }
	free(g_tw); g_tw = nullptr;
}

// ---- the synthetic huge world (decal_test=huge, issue 4110) --------------------------------------------------------------
// The huge save's size (terrain-decals.md 2.1, 9.3: 17.7k decals in 19 batches, 0.79 M vertices, 256 names): 17,800 decals,
// decal g in batch g % 19 at position g / 19 (slot = position, but position >= 3 is one slot further: slot 3 is free);
// with k = g % 148: priority (k % 8) / 8, pass 1 for k = 3, handles (100 + 2k % 256, 100 + (2k + 1) % 256, k odd ? 1975 :
// 100 + 5k % 256), handles 0, 0, 1975 for k = 147; triangles 1 + 11 g % 28; rectangle x0 = 37 g % 3300, y0 = 53 g % 2300,
// x1 = x0 + 10 + g % 200 + 0.5, y1 = y0 + 10 + 3 g % 150 + 0.25; vertex bytes as the small world's (id = g). The texture
// map: 3,000 nodes, node i holds handle h = 100 + i (i < 256) or 2000 + i, named "hw_texture_%04u.dds" (h); TDS+0x4F0 = 1975. shell/tests/test_decal_tap.py
// recomputes it. HugeToggle flips decal HW_TOGGLE (batch 5) between live and freed (the batch is laid out again).
#define HW_NB 19u
#define HW_N 17800u
#define HW_MAP 3000u
#define HW_SLOTCAP 1024u
#define HW_TOGGLE (5u + 19u * 7u)
struct HugeWorld
{
	unsigned char tds[0x500];
	unsigned char vec[32 * DT_BATCH];
	unsigned char* pool[HW_NB]; unsigned char* chunk[HW_NB];
	unsigned char mgr[0x60], typeHead[0x18], typeNode[2][0x18], texMap[0xC], texHead[0x18];
	unsigned char (*texNode)[0x18]; unsigned char (*res)[0x20]; unsigned char (*obj)[0x0C]; char (*names)[24];
	unsigned char farm[DT_FARM_BYTES];
	bool toggledOff;
	uint32_t restored;           // 0366: times the toggled decal came back (it is then the newest add)
	TestIndex ix; TestIxEntry* ixe;   // 0366: the spatial index (56 x 40 cells)
};
static HugeWorld* g_hw;

static void HugeDecal(uint32_t g, TestDecal& t)
{
	const uint32_t k = g % 148u;
	t.id = g; t.batch = g % HW_NB; t.prio = (float)(k % 8u) * 0.125f; t.pass = k == 3u ? 1u : 0u;
	if (k == 147u) { t.tex[0] = 0; t.tex[1] = 0; t.tex[2] = 1975; }
	else { t.tex[0] = 100 + (2 * k) % 256; t.tex[1] = 100 + (2 * k + 1) % 256; t.tex[2] = (k & 1) ? 1975u : 100 + (5 * k) % 256; }
	t.tris = 1 + (11 * g) % 28;
	const float x0 = (float)((37 * g) % 3300), y0 = (float)((53 * g) % 2300);
	t.rect[0] = x0; t.rect[1] = y0; t.rect[2] = x0 + (float)(10 + g % 200) + 0.5f; t.rect[3] = y0 + (float)(10 + (3 * g) % 150) + 0.25f;
	t.live = !(g == HW_TOGGLE && g_hw && g_hw->toggledOff);
}

static void HugeLayoutBatch(uint32_t b)
{
	HugeWorld& w = *g_hw;
	memset(w.pool[b], 0, HW_SLOTCAP * DT_REC);
	uint32_t slot = 0, live = 0, verts = 0, prev = DT_NIL, head = DT_NIL, freeHead = DT_NIL;
	for (uint32_t g = b, pos = 0; g < HW_N; g += HW_NB, ++pos)
	{
		if (pos == 3) { Put32(w.pool[b] + DT_REC * slot, 0, freeHead); freeHead = slot; slot++; }   // the free slot 3
		TestDecal t; HugeDecal(g, t);
		unsigned char* r = w.pool[b] + DT_REC * slot;
		if (!t.live) { Put32(r, 0, freeHead); freeHead = slot; slot++; continue; }
		if (prev != DT_NIL) Put32(w.pool[b] + DT_REC * prev, 0, slot); else head = slot;
		Put32(r, 0, DT_NIL); Put32(r, 4, 0x80000000u | (prev == DT_NIL ? DT_NIL : prev));
		Put32(r, 8, 1u | (t.pass << 1) | 0xAB00u);
		Put32(r, 0xC, verts); Put32(r, 0x10, t.tris);
		for (int i = 0; i < 3; ++i) Put32(r, 0x14 + 4 * i, t.tex[i]);
		PutF(r, 0x20, t.prio);
		for (int i = 0; i < 4; ++i) PutF(r, 0x24 + 4 * i, t.rect[i]);
		TestVerts(w.chunk[b] + verts * 8, g, 24 * t.tris);
		verts += 3 * t.tris;
		prev = slot; slot++; live++;
	}
	unsigned char* v = w.vec + DT_BATCH * b;
	memset(v, 0, DT_BATCH);
	Put32(v, 0x00, freeHead); Put32(v, 0x04, live); Put32(v, 0x08, (uint32_t)(uintptr_t)w.pool[b]); Put32(v, 0x0C, slot);
	Put32(v, 0x10, HW_SLOTCAP); Put32(v, 0x14, live); Put32(v, 0x18, head); Put32(v, 0x1C, prev == DT_NIL ? DT_NIL : prev);
	Put32(v, 0x20, 5 + b); Put32(v, 0x24, verts); Put32(v, 0x28, (uint32_t)(uintptr_t)w.chunk[b]);
}

// 0366: the index of the huge world: decal g added in the order of g, slot = position (+ 1 from position 3: slot 3 is
// free); the toggled decal, once back, is the newest add
static void HugeIndex()
{
	HugeWorld& w = *g_hw;
	uint32_t n = 0;
	for (uint32_t g = 0; g < HW_N; ++g)
	{
		TestDecal t; HugeDecal(g, t);
		if (!t.live) continue;
		const uint32_t pos = g / HW_NB;
		TestIxEntry& e = w.ixe[n++];
		e.batch = g % HW_NB; e.slot = pos + (pos >= 3 ? 1u : 0u);
		e.age = (g == HW_TOGGLE && w.restored) ? HW_N + w.restored : g;
		memcpy(e.rect, t.rect, 16);
	}
	TestIndexBuild(w.tds, w.ix, w.ixe, n);
}

// The toggled decal in the index as the EXE does it, without a rebuild (the rebuild of 17.8k items took milliseconds of
// the test's Present under ASan): off = RemoveDecal's unlink (its list unlink: neighbours joined, the head moved, bit 0 free),
// on = AddDecal's insert at the head of its cell (the newest add). Its item and cell are found once.
static uint32_t g_hwItem = DT_NIL, g_hwCell;
static void HugeIndexToggle(bool off)
{
	TestIndex& x = g_hw->ix;
	unsigned char* pool = g_hw->tds + 0x4DC;
	if (g_hwItem == DT_NIL)
	{
		const uint32_t pos = HW_TOGGLE / HW_NB, slot = pos + (pos >= 3 ? 1u : 0u), used = U32(pool, 0xC);
		for (uint32_t i = 1; i < used; ++i)
			if (U32(x.items + 24 * i, 16) == HW_TOGGLE % HW_NB && U32(x.items + 24 * i, 20) == slot) g_hwItem = i;
		if (g_hwItem == DT_NIL) return;
		unsigned char* it = x.items + 24 * g_hwItem;
		g_hwCell = WalkCell(F32(it, 8), F32(g_hw->tds, 0x6C), F32(g_hw->tds, 0x7C), x.cols) +
			WalkCell(F32(it, 12), F32(g_hw->tds, 0x70), F32(g_hw->tds, 0x80), x.rows) * x.cols;
	}
	unsigned char* it = x.items + 24 * g_hwItem;
	if (off)
	{
		const uint32_t next = U32(it, 0) >> 1, prev = U32(it, 4);
		if (next != DT_NIL) Put32(x.items + 24 * next, 4, prev);
		if (prev == DT_NIL) x.heads[g_hwCell] = next;
		else Put32(x.items + 24 * prev, 0, (next << 1) | (U32(x.items + 24 * prev, 0) & 1u));
		Put32(it, 0, (DT_NIL << 1) | 1u); Put32(it, 4, DT_NIL);
		Put32(pool, 4, U32(pool, 4) - 1);
	}
	else
	{
		const uint32_t head = x.heads[g_hwCell];
		Put32(it, 0, head << 1); Put32(it, 4, DT_NIL);
		if (head != DT_NIL) Put32(x.items + 24 * head, 4, g_hwItem);
		x.heads[g_hwCell] = g_hwItem;
		Put32(pool, 4, U32(pool, 4) + 1);
	}
}

static void HugeToggle()
{
	g_hw->toggledOff = !g_hw->toggledOff;
	if (!g_hw->toggledOff) g_hw->restored++;
	HugeLayoutBatch(HW_TOGGLE % HW_NB);
	HugeIndexToggle(g_hw->toggledOff);
}

static bool BuildHugeWorld()
{
	g_hw = (HugeWorld*)calloc(1, sizeof(HugeWorld));
	if (!g_hw) return false;
	HugeWorld& w = *g_hw;
	for (uint32_t b = 0; b < HW_NB; ++b)
	{
		w.pool[b] = (unsigned char*)calloc(HW_SLOTCAP, DT_REC);
		w.chunk[b] = (unsigned char*)calloc(DT_CHUNK_VERTS, 8);
		if (!w.pool[b] || !w.chunk[b]) return false;
	}
	w.texNode = (unsigned char (*)[0x18])calloc(HW_MAP, 0x18); w.res = (unsigned char (*)[0x20])calloc(HW_MAP, 0x20);
	w.obj = (unsigned char (*)[0x0C])calloc(HW_MAP, 0x0C); w.names = (char (*)[24])calloc(HW_MAP, 24);
	if (!w.texNode || !w.res || !w.obj || !w.names) return false;
	PutF(w.tds, 0x6C, 0.0f); PutF(w.tds, 0x70, 0.0f); PutF(w.tds, 0x74, 3583.0f); PutF(w.tds, 0x78, 2559.0f);
	Put32(w.tds, 0xAC, DT_NIL); Put32(w.tds, 0xB0, DT_NIL);
	Put32(w.tds, 0xB4, (uint32_t)(uintptr_t)w.vec); Put32(w.tds, 0xB8, HW_NB); Put32(w.tds, 0xBC, 32);
	Put32(w.tds, 0x4F0, 1975);
	for (uint32_t b = 0; b < HW_NB; ++b) HugeLayoutBatch(b);
	w.ixe = (TestIxEntry*)malloc(sizeof(TestIxEntry) * HW_N);
	if (!w.ixe || !TestIndexAlloc(w.ix, 56, 40, HW_N + 8, 8)) return false;
	HugeIndex();
	if (!BuildTestGrid()) return false;
	for (uint32_t i = 0; i < TG_TX * TG_TY; i += 5) TestSetArea(i / TG_TY, i % TG_TY, 1);
	memset(w.typeHead, 0, 0x18); w.typeHead[0x15] = 1;
	TestTreeNode(w.typeNode[0], w.typeHead, w.typeNode[1], 0, 0);
	TestTreeNode(w.typeNode[1], w.typeHead, w.typeHead, 8, (uint32_t)(uintptr_t)w.texMap);
	Put32(w.typeHead, 4, (uint32_t)(uintptr_t)w.typeNode[0]);
	Put32(w.mgr, 0x44, (uint32_t)(uintptr_t)w.typeHead); Put32(w.mgr, 0x48, 2);
	memset(w.texHead, 0, 0x18); w.texHead[0x15] = 1;
	for (uint32_t i = 0; i < HW_MAP; ++i)
	{
		const uint32_t hnd = i < 256 ? 100 + i : 2000 + i;   // not 1975 (the blank spec, named by TDS+0x4F0 as in the EXE)
		_snprintf_s(w.names[i], 24, _TRUNCATE, "hw_texture_%04u.dds", hnd);
		Put32(w.obj[i], 8, hnd);
		Put32(w.res[i], 0x1C, (uint32_t)(uintptr_t)w.obj[i]);
		TestTreeNode(w.texNode[i], w.texHead, i + 1 < HW_MAP ? w.texNode[i + 1] : w.texHead, (uint32_t)(uintptr_t)w.names[i],
			(uint32_t)(uintptr_t)w.res[i]);
	}
	Put32(w.texHead, 4, (uint32_t)(uintptr_t)w.texNode[0]);
	Put32(w.texMap, 4, (uint32_t)(uintptr_t)w.texHead); Put32(w.texMap, 8, HW_MAP);
	// the farm table as the small world's (none of its handles is used here)
	Put32(w.farm, 0x24, 0x00010000u); Put32(w.farm, 0x28, 0x00010001u);
	for (uint32_t k = 2; k < DT_FARM_CATS; ++k) Put32(w.farm, 0x24 + 4 * k, 0x00000002u);
	Put32(w.farm, 0x44, (uint32_t)(uintptr_t)(w.farm + 0x50)); Put32(w.farm, 0x48, 2); Put32(w.farm, 0x4C, 64);
	unsigned char* v = w.farm + 0x50;
	PutF(v, 0, 0.5f); Put32(v, 4, 198); Put32(v, 8, 199); Put32(v, 12, 202);
	PutF(v, 16, 0.5f); Put32(v, 20, 200); Put32(v, 24, 201); Put32(v, 28, 202);
	Put32(w.farm, 0x454, 202);
	return true;
}

// ---- set-up --------------------------------------------------------------------------------------------------------
// The build of the image at base (its header), every symbol the tap reads carried, every pattern matching: g_build set.
static bool CheckPatterns(uint32_t base)
{
	const int build = ShellExeResolve(base, kDtNeed, (int)(sizeof(kDtNeed) / sizeof(kDtNeed[0])), g_why, sizeof(g_why));
	if (build < 0 || !ShellExeCheck(build, base, kDtSigs, DT_NSIGS, g_why, sizeof(g_why))) return false;
	g_build = build;
	return true;
}

// decal_test=patterns: a fake image of the build decal_test_build (default 4390913; "0": a header no build has) with every
// pattern's bytes at that build's addresses (pattern k broken), checked as the EXE; decal_test_drop names a symbol the
// table then does not carry. The image is never freed (the process ends with the test).
static bool TestPatterns()
{
	const bool forget = !strcmp(g_testBuild, "0");
	const int build = ShellExeBuildBySteam(g_testBuild[0] && !forget ? g_testBuild : "4390913");
	if (build < 0) { _snprintf_s(g_why, _TRUNCATE, "decal_test_build=%s names no known build", g_testBuild); return false; }
	if (g_testDrop[0] && !ShellExeTestDrop(g_testDrop)) { _snprintf_s(g_why, _TRUNCATE, "decal_test_drop=%s names no symbol", g_testDrop); return false; }
	const uint32_t size = ShellExeSpan(build, kDtSigs, DT_NSIGS);
	unsigned char* img = size ? (unsigned char*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
	if (!img) { _snprintf_s(g_why, _TRUNCATE, "test image could not be made"); return false; }
	const uint32_t base = (uint32_t)(uintptr_t)img;
	if (!ShellExeLay(build, base, size, kDtSigs, DT_NSIGS, g_testPattern)) { _snprintf_s(g_why, _TRUNCATE, "test image could not be laid out"); return false; }
	if (forget) ShellExeTestForgetBuild(base);
	DLOG("decal_tap: test image of %s at 0x%08X (%u bytes)%s", VpExeBuildLabel(build), base, size, forget ? ", its header changed to no known build" : "");
	return CheckPatterns(base);
}

static void ReadKeys()
{
	if (!g_dir[0])
	{
		HMODULE m = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ReadKeys, &m);
		GetModuleFileNameW(m, g_dir, MAX_PATH);
		wchar_t* sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0;
	}
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	auto get = [&](const wchar_t* key, const wchar_t* env, wchar_t* out, DWORD n) {
		out[0] = 0;
		if (!GetEnvironmentVariableW(env, out, n)) GetPrivateProfileStringW(L"composite", key, L"", out, n, ini);
	};
	auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
	wchar_t v[96];
	get(L"decal_budget_ms", L"VPSHELL_DECAL_BUDGET_MS", v, 96); if (v[0]) g_budgetMs = (uint32_t)clamp(_wtoi(v), 1, 1000);
	get(L"decal_settle_frames", L"VPSHELL_DECAL_SETTLE_FRAMES", v, 96); if (v[0]) g_settleFrames = (uint32_t)clamp(_wtoi(v), 0, 10000);
	get(L"decal_max_wait_ms", L"VPSHELL_DECAL_MAX_WAIT_MS", v, 96); if (v[0]) g_maxWaitMs = (uint32_t)clamp(_wtoi(v), 0, 60000);
	get(L"decal_net_ms", L"VPSHELL_DECAL_NET_MS", v, 96); if (v[0]) g_netMs = (uint32_t)clamp(_wtoi(v), 50, 600000);
	get(L"decal_walk_holds", L"VPSHELL_DECAL_WALK_HOLDS", v, 96); if (v[0]) g_walkHoldMax = (uint32_t)clamp(_wtoi(v), 0, 16);   // 0406
	get(L"decal_test_midbuild", L"VPSHELL_DECAL_TEST_MIDBUILD", v, 96); if (v[0]) g_midBuild = (uint32_t)clamp(_wtoi(v), 0, 1000000);
	get(L"decal_test_pattern", L"VPSHELL_DECAL_TEST_PATTERN", v, 96); g_testPattern = v[0] ? _wtoi(v) : -1;
	get(L"decal_test_build", L"VPSHELL_DECAL_TEST_BUILD", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testBuild, sizeof(g_testBuild), nullptr, nullptr);
	get(L"decal_test_drop", L"VPSHELL_DECAL_TEST_DROP", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testDrop, sizeof(g_testDrop), nullptr, nullptr);
	get(L"decal_max_kb", L"VPSHELL_DECAL_MAX_KB", v, 96);
	if (v[0]) g_maxBytes = (uint64_t)clamp(_wtoi(v), 1, (int)(VPSHELL_DECAL_MAX_BYTES / 1024)) * 1024;
	wchar_t t[96] = L"";
	get(L"decal_test", L"VPSHELL_DECAL_TEST", t, 96);
	char a[96]; WideCharToMultiByte(CP_ACP, 0, t, -1, a, 96, nullptr, nullptr);
	g_test = T_NONE;
	for (int i = 1; i < T_COUNT; ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) DLOG("decal_tap: decal_test=%s is not a known mode - ignored", a);
}

// ---- the control block -----------------------------------------------------------------------------------------------
static bool OpenControl()
{
	if (g_ctl) return true;
	wchar_t name[64]; _snwprintf_s(name, _TRUNCATE, VPSHELL_DECALTAP_CONTROL_FMT, GetCurrentProcessId());
	g_ctlMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, VPSHELL_DECALTAP_CONTROL_BYTES, name);
	if (!g_ctlMap) { DLOG("decal_tap: control section '%ls' could not be created, error %lu", name, GetLastError()); return false; }
	g_ctl = (VPShellDecalTapControl*)MapViewOfFile(g_ctlMap, FILE_MAP_WRITE, 0, 0, VPSHELL_DECALTAP_CONTROL_BYTES);
	if (!g_ctl) { CloseHandle(g_ctlMap); g_ctlMap = nullptr; DLOG("decal_tap: control section view failed, error %lu", GetLastError()); return false; }
	DLOG("decal_tap: control block '%ls' (%u bytes)", name, VPSHELL_DECALTAP_CONTROL_BYTES);
	g_wantVersion++;   // published once at once (state NONE until a map is loaded)
	return true;
}

static void Publish()
{
	if (!g_ctl) return;
	VPShellDecalTapControl* t = g_ctl;
	InterlockedIncrement((volatile LONG*)&t->seq);
	MemoryBarrier();
	t->magic = VPSHELL_DECAL_MAGIC; t->state = g_state; t->failReason = g_fail; t->exePid = GetCurrentProcessId();
	t->generation = g_generation; t->loadSeq = g_loadSeq; t->decalCount = g_nPub; t->imageBytes = g_imageBytes;
	t->fnv1a64 = g_fnv; t->readyQpc = g_readyQpc; t->copyMs = g_copyMs; t->pollMs = g_pollMs;
	t->publishCount = g_publishCount; t->refusedReads = g_refused; t->sehCount = g_sehCount;
	t->changedSince = g_changedSince; t->added = g_lastAdded; t->removed = g_lastRemoved;
	t->heldSections = (g_secCur ? 1u : 0u) + (g_secPrev ? 1u : 0u);
	t->areaSet = g_generation ? g_areaPubSet : 0; t->areaTilesX = g_generation ? g_areaPubX : 0; t->areaTilesY = g_generation ? g_areaPubY : 0;
	t->walkSmall = g_generation ? g_walkPubSmall : 0; t->walkLarge = g_generation ? g_walkPubLarge : 0; t->walkMisses = g_walkMisses;
	t->walkHolds = g_walkHolds;   // 0406
	wcsncpy_s(t->imageName, g_imageName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&t->seq);
	g_pubVersion = g_wantVersion;
}

static void SetState(uint32_t st, uint32_t fail) { g_state = st; g_fail = fail; g_wantVersion++; }

// FAILED with its reason and one LOUD line; forGood = the tap stops reading this EXE for the life of the process.
static void Fail(uint32_t fail, const char* why, bool forGood)
{
	SetState(VPDTAP_FAILED, fail);
	Loud("FAILED %s: %s%s", fail < 8 ? kFailNames[fail] : "?", why,
		forGood ? " - DISABLED for the life of the process; the last good image stays" : " - the last good image stays; retried");
	if (forGood) g_dead = true;
}

// ---- reading -----------------------------------------------------------------------------------------------------------
static bool BatchesGrow(uint32_t n)
{
	if (n <= g_capB) return true;
	Batch* nb = (Batch*)realloc(g_b, n * sizeof(Batch));
	if (!nb) return false;
	memset(nb + g_capB, 0, (n - g_capB) * sizeof(Batch));
	g_b = nb; g_capB = n;
	return true;
}

static bool Finite(float f) { return f == f && f > -1e30f && f < 1e30f; }

// 1 ok, 0 layout (g_why), -1 fault
static int ReadHeader(uint32_t* nb)
{
	bool fault = false;
	if (!SafeRead(A.tds, g_hdr, DT_HDR_BYTES, &fault)) { _snprintf_s(g_why, _TRUNCATE, "TDS header at 0x%08X unreadable", (uint32_t)A.tds); return fault ? -1 : 0; }
	*nb = U32(g_hdr, 0xB8);
	const uint32_t cap = U32(g_hdr, 0xBC), vec = U32(g_hdr, 0xB4);
	if (*nb > DT_MAX_BATCHES || *nb > cap || (*nb && !vec))
	{ _snprintf_s(g_why, _TRUNCATE, "absurd batch count %u (capacity %u, vector 0x%08X)", *nb, cap, vec); return 0; }
	if (*nb && !SafeRead(vec, g_vec, (size_t)*nb * DT_BATCH, &fault))
	{ _snprintf_s(g_why, _TRUNCATE, "fault reading %u batches at 0x%08X", *nb, vec); return fault ? -1 : 0; }
	return 1;
}

static bool Fits(int64_t t0, const Cost& c, double units);
static void Learn(Cost& c, int64_t s0, double units);

// Reading a batch has two parts (issue 4110): ReadBatchCopy copies its record pool and the used part of its vertex chunk
// out of the EXE, walks its list, checks the counts and re-reads its vector entry (a torn read) in one Present: the only
// part that reads EXE memory. ReadBatchWork then checks every listed record and computes its content hash from that copy,
// a share per Present within the budget (the hash of ~0.33 MB of vertex bytes per batch is the larger cost on huge).
static void RawFree(Batch& B)
{
	free(B.rawPool); B.rawPool = nullptr; free(B.rawVerts); B.rawVerts = nullptr; free(B.rawOrder); B.rawOrder = nullptr;
	B.rawPending = false;
}

// 1 copied (ReadBatchWork goes on), 0 layout (g_why), -1 fault, 2 moved during the copy (torn).
static int ReadBatchCopy(uint32_t b)
{
	bool fault = false;
	Batch& B = g_b[b];
	RawFree(B);
	B.valid = false; B.readSeq++;
	const unsigned char* e = g_vec + DT_BATCH * b;
	const uint32_t live = U32(e, 4), pool = U32(e, 8), slots = U32(e, 0xC), capS = U32(e, 0x10), listed = U32(e, 0x14),
		head = U32(e, 0x18), verts = U32(e, 0x24), chunk = U32(e, 0x28);
	if (slots > capS || capS > DT_MAX_SLOTS || live > slots || listed > slots || (slots && !pool))
	{ _snprintf_s(g_why, _TRUNCATE, "batch %u: absurd counts (slots %u capacity %u live %u listed %u pool 0x%08X)", b, slots, capS, live, listed, pool); return 0; }
	if (verts > DT_CHUNK_VERTS || (verts && !chunk))
	{ _snprintf_s(g_why, _TRUNCATE, "batch %u: %u vertices used of a chunk of %u (chunk 0x%08X)", b, verts, DT_CHUNK_VERTS, chunk); return 0; }
	unsigned char* pr = slots ? (unsigned char*)malloc((size_t)slots * DT_REC) : nullptr;
	if (slots && !pr) { _snprintf_s(g_why, _TRUNCATE, "out of memory (pool of %u)", slots); return 0; }
	if (slots && !SafeRead(pool, pr, (size_t)slots * DT_REC, &fault))
	{ free(pr); _snprintf_s(g_why, _TRUNCATE, "batch %u: fault reading %u records at 0x%08X", b, slots, pool); return fault ? -1 : 0; }
	unsigned char* cv = verts ? (unsigned char*)malloc((size_t)verts * 8u) : nullptr;
	if (verts && (!cv || !SafeRead(chunk, cv, (size_t)verts * 8u, &fault)))
	{ free(pr); free(cv); _snprintf_s(g_why, _TRUNCATE, "batch %u: fault reading %u vertices at 0x%08X", b, verts, chunk); return fault ? -1 : 0; }
	// a torn read: the counters must be what they were before the copy
	unsigned char again[DT_BATCH];
	const uint32_t vec = U32(g_hdr, 0xB4);
	if (!SafeRead(vec + DT_BATCH * b, again, DT_BATCH, &fault))
	{ free(pr); free(cv); _snprintf_s(g_why, _TRUNCATE, "batch %u: fault re-reading its entry", b); return fault ? -1 : 0; }
	if (memcmp(again, e, 0x2C)) { free(pr); free(cv); return 2; }
	// the list order (the EXE's walk); every live record must be on it
	uint32_t* order = slots ? (uint32_t*)malloc(sizeof(uint32_t) * (slots + 1)) : nullptr;
	if (slots && !order) { free(pr); free(cv); _snprintf_s(g_why, _TRUNCATE, "out of memory (list of %u)", slots); return 0; }
	uint32_t nOrder = 0;
	for (uint32_t node = head, guard = 0; node != DT_NIL; ++guard)
	{
		if (node >= slots || guard > listed) { _snprintf_s(g_why, _TRUNCATE, "batch %u: list node %u past %u slots or loop (%u listed)", b, node, slots, listed); free(pr); free(cv); free(order); return 0; }
		order[nOrder++] = node;
		node = U32(pr, DT_REC * node);
	}
	uint32_t nLive = 0;
	for (uint32_t s = 0; s < slots; ++s) nLive += (U32(pr, DT_REC * s + 4) >> 31);
	if (nLive != live || nOrder != listed || nLive != nOrder)
	{ _snprintf_s(g_why, _TRUNCATE, "batch %u: %u live records, the batch says %u live, %u listed, the list holds %u", b, nLive, live, listed, nOrder); free(pr); free(cv); free(order); return 0; }
	if (B.cap < nOrder)
	{
		Decal* nd = (Decal*)realloc(B.d, sizeof(Decal) * nOrder);
		if (!nd) { _snprintf_s(g_why, _TRUNCATE, "out of memory (decals)"); free(pr); free(cv); free(order); return 0; }
		B.d = nd; B.cap = nOrder;
	}
	if (B.vCap < verts * 8u)
	{
		unsigned char* nv = (unsigned char*)realloc(B.v, (size_t)verts * 8u);
		if (!nv) { _snprintf_s(g_why, _TRUNCATE, "out of memory (vertices)"); free(pr); free(cv); free(order); return 0; }
		B.v = nv; B.vCap = verts * 8u;
	}
	B.rawPool = pr; B.rawVerts = cv; B.rawOrder = order; B.rawN = nOrder; B.rawI = 0; B.rawVUsed = 0; B.rawVerts_n = verts;
	B.rawPh = 0xCBF29CE484222325ull; B.n = 0; B.outN = 0;
	memcpy(B.rawSig, e, DT_BATCH);
	B.rawPending = true;
	return 1;
}

// 0649: AddDecal takes the bounding box of the decal's vertices and clamps each side on its own: min up to the world's
// min (TDS+0x6C), max down to the world's max (TDS+0x74). A decal whose vertices all lie past the east edge (a route
// piece laid toward a neighbour across the wrap seam) keeps its min x past the edge and gets max x = the world's max: min x
// > max x; past the west edge, min x = the world's min and max x below it. The draw takes a record only when its
// rectangle overlaps the tile's ([ox - 1, ox + 64] in the last column), so it never draws one whose min x lies more than one
// sample past the east edge (or whose max x lies more than one sample before the west edge). True for exactly that form,
// finite, with y in order: such a record is left out of the set. Any other inverted rectangle (y, or x not clamped at an
// edge) still fails the layout check (an unknown EXE build, D118). terrain-decals.md 21.
static bool OutsideX(const float* r, float wx0, float wx1)
{
	if (!Finite(r[0]) || !Finite(r[1]) || !Finite(r[2]) || !Finite(r[3]) || !Finite(wx0) || !Finite(wx1)) return false;
	if (!(r[0] > r[2]) || r[1] > r[3]) return false;
	return (r[2] == wx1 && r[0] > wx1) || (r[0] == wx0 && r[2] < wx0);
}

// Checks and hashes the copied records from rawI on while the budget allows (at least one record). 1 done (the batch is
// valid), 3 not done yet (more in a later Present), 0 layout (g_why), 2 the batch moved since its copy (read again).
static int ReadBatchWork(uint32_t b, int64_t t0)
{
	Batch& B = g_b[b];
	const uint32_t verts = B.rawVerts_n;
	const unsigned char* pr = B.rawPool;
	while (B.rawI < B.rawN)
	{
		const int64_t s0 = Qpc();
		uint32_t end = B.rawI + 64 < B.rawN ? B.rawI + 64 : B.rawN, bytes = 0;
		for (uint32_t i = B.rawI; i < end; ++i) bytes += DT_REC + 24u * U32(pr, DT_REC * B.rawOrder[i] + 0x10);
		if (!Fits(t0, g_cWork, bytes)) return 3;
		for (uint32_t i = B.rawI; i < end; ++i)
		{
			const uint32_t slot = B.rawOrder[i];
			const unsigned char* r = pr + DT_REC * slot;
			if (!(U32(r, 4) >> 31)) { _snprintf_s(g_why, _TRUNCATE, "batch %u: list node %u is not live", b, slot); RawFree(B); return 0; }
			Decal& d = B.d[B.n];
			d.slot = slot;
			const uint32_t first = U32(r, 0xC), tris = U32(r, 0x10);
			d.pass = (U32(r, 8) >> 1) & 1u; d.tris = tris; d.priority = F32(r, 0x20);
			for (int k = 0; k < 3; ++k) d.tex[k] = U32(r, 0x14 + 4 * k);
			for (int k = 0; k < 4; ++k) d.rect[k] = F32(r, 0x24 + 4 * k);
			if (!tris || tris * 3u > 0xFFFEu || (uint64_t)first + 3ull * tris > verts)
			{ _snprintf_s(g_why, _TRUNCATE, "batch %u slot %u: a record past its chunk (first vertex %u + 3 x %u triangles, %u vertices used)", b, slot, first, tris, verts); RawFree(B); return 0; }
			if (Finite(d.priority) && OutsideX(d.rect, F32(g_hdr, 0x6C), F32(g_hdr, 0x74)))
			{
				// 0649: a decal whose vertices all lie past the world's east (west) edge, as AddDecal clamps it: the EXE's
				// draw never takes it (terrain-decals.md 21). Left out of the set, counted; its slot kept for the walk.
				if (B.outN >= B.outCap)
				{
					const uint32_t c = B.outCap ? B.outCap * 2 : 8;
					uint32_t* ns = (uint32_t*)realloc(B.outSlots, sizeof(uint32_t) * c);
					if (!ns) { _snprintf_s(g_why, _TRUNCATE, "out of memory (records outside the world)"); RawFree(B); return 0; }
					B.outSlots = ns; B.outCap = c;
				}
				if (!B.outN) memcpy(B.outFirst, d.rect, 16);
				B.outSlots[B.outN++] = slot;
				B.rawPh = RecHash(B.rawPh, r);
				continue;
			}
			if (!Finite(d.priority) || !Finite(d.rect[0]) || !Finite(d.rect[1]) || !Finite(d.rect[2]) || !Finite(d.rect[3]) ||
				d.rect[0] > d.rect[2] || d.rect[1] > d.rect[3])
			{ _snprintf_s(g_why, _TRUNCATE, "batch %u slot %u: rectangle (%g, %g, %g, %g) or priority %g not valid", b, slot, d.rect[0], d.rect[1], d.rect[2], d.rect[3], d.priority); RawFree(B); return 0; }
			if (d.tex[0] >= DT_MAX_HANDLE || d.tex[1] >= DT_MAX_HANDLE || d.tex[2] >= DT_MAX_HANDLE)
			{ _snprintf_s(g_why, _TRUNCATE, "batch %u slot %u: texture handle %u / %u / %u outside the handle pool (0x%X)", b, slot, d.tex[0], d.tex[1], d.tex[2], DT_MAX_HANDLE); RawFree(B); return 0; }
			const uint32_t nbv = 24u * tris;
			memcpy(B.v + B.rawVUsed, B.rawVerts + (size_t)first * 8u, nbv);
			d.vOff = B.rawVUsed; B.rawVUsed += nbv;
			uint64_t h = Fnv(0xCBF29CE484222325ull, &d.pass, 4);
			h = Fnv(h, &d.priority, 4); h = Fnv(h, d.tex, 12); h = Fnv(h, d.rect, 16); h = Fnv(h, &d.tris, 4);
			d.hash = Fnv(h, B.v + d.vOff, nbv);
			B.rawPh = RecHash(B.rawPh, r);
			B.n++;
		}
		B.rawI = end;
		Learn(g_cWork, s0, bytes);
	}
	// the batch must not have moved since its copy (the poll of this Present read its entry into g_vec)
	const bool moved = memcmp(B.rawSig, g_vec + DT_BATCH * b, 0x2C) != 0;
	B.vBytes = B.rawVUsed; B.poolHash = B.rawPh;
	memcpy(B.sig, B.rawSig, DT_BATCH);
	RawFree(B);
	if (moved) { B.n = 0; B.outN = 0; return 2; }
	B.valid = true; B.moved = false; B.layoutTries = 0; B.readSeq++;
	return 1;
}

// The live records of a batch, hashed as ReadBatchWork hashes them (the safety net): true when equal to the last read.
static int NetCheck(uint32_t b)
{
	bool fault = false;
	Batch& B = g_b[b];
	const unsigned char* e = B.sig;
	const uint32_t pool = U32(e, 8), slots = U32(e, 0xC), head = U32(e, 0x18);
	if (!slots) return 1;
	unsigned char* pr = (unsigned char*)malloc((size_t)slots * DT_REC);
	if (!pr) return 1;
	if (!SafeRead(pool, pr, (size_t)slots * DT_REC, &fault)) { free(pr); return fault ? -1 : 0; }
	uint64_t ph = 0xCBF29CE484222325ull;
	for (uint32_t node = head, guard = 0; node != DT_NIL && node < slots && guard <= slots; ++guard)
	{
		ph = RecHash(ph, pr + DT_REC * node);
		node = U32(pr, DT_REC * node);
	}
	free(pr);
	return ph == B.poolHash ? 1 : 0;
}

// ---- the area block (issue 4111): tile+0x300 of every tile ---------------------------------------------------------------
// A sweep reads the tile grid (TS+0x75FA4 -> TR: TR+0x2310 / +0x2314 / +0x2318 tiles in x, in y, the count; TR+0x1AE0
// the tile pointers, column-major) and every tile's +0x300, DT_AREA_CHUNK tiles per step within the budget, then the grid
// again (a grid that moved meanwhile, a new load, starts the sweep again). The build runs one at its first step (B_AREA)
// so each generation carries the flags of its time; between builds an idle sweep every decal_net_ms finds flags that
// changed without a decal change (a terrain edit invalidates through the area invalidation too) and starts a build. No
// TerrainRenderer: no block (the server keeps its own rule). A count other than x * y, more than 1024 tiles per axis, a
// tile without an object or a value other than 0 / 1: a layout failure.
struct AreaSweep
{
	bool active, done;
	uintptr_t tr, grid; uint32_t tx, ty, n, pos;
	uint32_t* ptrs; unsigned char* bits;        // bits: per tile 0 / 1 at y * tx + x
};
static AreaSweep g_sw;
static bool g_swIdle;                          // the sweep in progress is the idle one (not a build's)
static uint32_t g_areaRestarts;

static void AreaFree()
{
	free(g_sw.ptrs); free(g_sw.bits);
	memset(&g_sw, 0, sizeof(g_sw));
}

// 1 begun (or done at once without a TerrainRenderer: no block), 0 layout (g_why), -1 fault.
static int AreaBegin()
{
	bool fault = false;
	AreaFree();
	uint32_t tr = 0;
	if (!SafeRead(A.trSlot, &tr, 4, &fault)) { _snprintf_s(g_why, _TRUNCATE, "the TerrainRenderer pointer (TS+0x75FA4) unreadable"); return fault ? -1 : 0; }
	if (!tr) { g_sw.done = true; return 1; }
	unsigned char d[12]; uint32_t grid = 0;
	if (!SafeRead(tr + DT_TR_TX, d, sizeof(d), &fault) || !SafeRead(tr + DT_TR_GRID, &grid, 4, &fault))
	{ _snprintf_s(g_why, _TRUNCATE, "tile grid of TR 0x%08X unreadable", tr); return fault ? -1 : 0; }
	const uint32_t tx = U32(d, 0), ty = U32(d, 4), n = U32(d, 8);
	if (!tx || !ty || tx > VPSHELL_DECAL_MAX_TILES_XY || ty > VPSHELL_DECAL_MAX_TILES_XY || n != tx * ty || !grid)
	{ _snprintf_s(g_why, _TRUNCATE, "tile grid out of range: %u x %u tiles, count %u, pointers 0x%08X", tx, ty, n, grid); return 0; }
	g_sw.ptrs = (uint32_t*)malloc((size_t)n * 4); g_sw.bits = (unsigned char*)calloc(n, 1);
	if (!g_sw.ptrs || !g_sw.bits) { AreaFree(); _snprintf_s(g_why, _TRUNCATE, "out of memory (area of %u tiles)", n); return 0; }
	if (!SafeRead(grid, g_sw.ptrs, (size_t)n * 4, &fault))
	{ AreaFree(); _snprintf_s(g_why, _TRUNCATE, "fault reading %u tile pointers at 0x%08X", n, grid); return fault ? -1 : 0; }
	g_sw.tr = tr; g_sw.grid = grid; g_sw.tx = tx; g_sw.ty = ty; g_sw.n = n; g_sw.pos = 0; g_sw.active = true;
	return 1;
}

// One Present's share: 1 done, 3 more to do, 2 the grid moved (begin again), 0 layout (g_why), -1 fault.
static int AreaStep(int64_t t0)
{
	bool fault = false;
	if (g_sw.done) return 1;
	while (g_sw.pos < g_sw.n)
	{
		const uint32_t end = g_sw.pos + DT_AREA_CHUNK < g_sw.n ? g_sw.pos + DT_AREA_CHUNK : g_sw.n;
		if (!Fits(t0, g_cArea, end - g_sw.pos)) return 3;
		const int64_t s0 = Qpc();
		for (uint32_t i = g_sw.pos; i < end; ++i)
		{
			const uint32_t x = i / g_sw.ty, y = i % g_sw.ty, p = g_sw.ptrs[i];
			uint32_t v = 0;
			if (!p) { _snprintf_s(g_why, _TRUNCATE, "tile (%u, %u) has no object", x, y); return 0; }
			if (!SafeRead(p + DT_T_AREA, &v, 4, &fault)) { _snprintf_s(g_why, _TRUNCATE, "tile (%u, %u) at 0x%08X unreadable", x, y, p); return fault ? -1 : 0; }
			if (v > 1) { _snprintf_s(g_why, _TRUNCATE, "tile (%u, %u): +0x300 = %u, not 0 or 1", x, y, v); return 0; }
			g_sw.bits[(size_t)y * g_sw.tx + x] = (unsigned char)v;
		}
		Learn(g_cArea, s0, end - g_sw.pos);
		g_sw.pos = end;
	}
	// the grid must be the one the sweep began with
	uint32_t tr = 0, grid = 0; unsigned char d[12];
	if (!SafeRead(A.trSlot, &tr, 4, &fault)) return fault ? -1 : 0;
	if (tr != g_sw.tr) return 2;
	if (!SafeRead(tr + DT_TR_TX, d, sizeof(d), &fault) || !SafeRead(tr + DT_TR_GRID, &grid, 4, &fault)) return fault ? -1 : 0;
	if (grid != g_sw.grid || U32(d, 0) != g_sw.tx || U32(d, 4) != g_sw.ty || U32(d, 8) != g_sw.n) return 2;
	g_sw.active = false; g_sw.done = true;
	return 1;
}

// ---- the walk block (issue 0366; terrain-decals.md 18) ---------------------------------------------------------------------
// The EXE's draw orders a tile's decals by the walk of its spatial index (TDS+0x6C: a grid of cells, each a list of the
// small decals' items, newest add first) and the large list (TDS+0x9C nodes from the head +0xAC, add order), then sorts
// by priority (an unstable introsort). The build reads both at its step B_WALK and gives every record of its snapshot its
// cell and rank (or its rank in the large list). Every item must name a live record of the snapshot exactly once, with
// the point AddDecal stores (the rectangle's centre), in the cell the index cell function computes for it, small or large as AddDecal
// decides; else the snapshot and the index disagree (the EXE changed in between: its batches moved too, and a later
// build follows), and this generation goes out without the block (counted in walkMisses, one log line). A fault: the
// walk is off for the life of the process (LOUD once); the decals go on.
// The walk in steps (the huge save: 17.6k items; one step under ASan cost 6-13 ms): WalkBegin copies everything it needs
// out of the EXE in one Present (the index header, the item pool's header, every cell head, the used items, the large
// list's (batch, slot) pairs) and maps (batch, slot) to the snapshot's records; WalkStep then checks and ranks the copied
// cells' items, DT_WALK_CHUNK (512) items per step within decal_budget_ms; WalkEnd takes the large list and builds the block.
// Each returns 1 done (or begun), 3 more to do (WalkStep), 0 no block this time (g_why), -1 a fault (g_why).
#define DT_WALK_CHUNK 512u
struct WalkWork
{
	bool active;
	float ox, oy, invx, invy;
	uint32_t cols, rows, cells, used, n, cell, nSmall, largeCount;
	uint32_t* heads; unsigned char* items;            // the copies
	uint32_t* large; uint32_t nLarge;                 // (batch, slot) pairs of the large list, from its head
	uint32_t* slotBase; uint32_t* map;                // (batch, slot) -> record index of the image
	const Decal** rec;                                // record index -> its decal in the snapshot
	unsigned char* done; unsigned char* out;
};
static WalkWork g_wk;

static void WalkFree()
{
	free(g_wk.heads); free(g_wk.items); free(g_wk.large); free(g_wk.slotBase); free(g_wk.map); free(g_wk.rec);
	free(g_wk.done); free(g_wk.out);
	memset(&g_wk, 0, sizeof(g_wk));
}

static uint32_t WalkLookup(uint32_t batch, uint32_t slot)
{
	if (batch >= g_st.nb || slot >= g_wk.slotBase[batch + 1] - g_wk.slotBase[batch]) return DT_NIL;
	return g_wk.map[g_wk.slotBase[batch] + slot];
}

static bool WalkLarge(const Decal& d) { return WalkSub(d.rect[2], d.rect[0]) > 256.0f || WalkSub(d.rect[3], d.rect[1]) > 256.0f; }

// 0649: true when (batch, slot) is a live record of the snapshot left out as outside the world (OutsideX): the EXE's index
// or large list holds it, the set does not; the walk passes over it (it takes no rank: the draw never takes it either).
static bool WalkOut(uint32_t batch, uint32_t slot)
{
	if (batch >= g_st.nb) return false;
	const Batch& B = g_b[batch];
	for (uint32_t i = 0; i < B.outN; ++i) if (B.outSlots[i] == slot) return true;
	return false;
}

static int WalkBegin()
{
	bool fault = false;
	WalkFree();
	g_walkMissBatch = DT_NIL;
	unsigned char ix[0x28];
	if (!SafeRead(A.tds + 0x6C, ix, sizeof(ix), &fault)) { _snprintf_s(g_why, _TRUNCATE, "the spatial index TDS+0x6C unreadable"); return fault ? -1 : 0; }
	const float ox = F32(ix, 0), oy = F32(ix, 4), invx = F32(ix, 0x10), invy = F32(ix, 0x14);
	const uint32_t headsAt = U32(ix, 0x18), cols = U32(ix, 0x1C), rows = U32(ix, 0x20), poolAt = U32(ix, 0x24);
	if (!cols || !rows || (uint64_t)cols * rows > VPSHELL_DECAL_WALK_MAX_CELLS || !headsAt || !poolAt || !Finite(ox) || !Finite(oy) ||
		!Finite(invx) || !Finite(invy) || !(invx > 0) || !(invy > 0))
	{
		_snprintf_s(g_why, _TRUNCATE, "spatial index out of range: %u x %u cells, heads 0x%08X, pool 0x%08X, origin %g, %g, inverse %g, %g",
			cols, rows, headsAt, poolAt, ox, oy, invx, invy);
		return 0;
	}
	unsigned char pl[0x14];
	if (!SafeRead(poolAt, pl, sizeof(pl), &fault)) { _snprintf_s(g_why, _TRUNCATE, "the index's item pool at 0x%08X unreadable", poolAt); return fault ? -1 : 0; }
	const uint32_t itemsAt = U32(pl, 8), used = U32(pl, 0xC);
	if (used > 0x800000u / 24u || (used && !itemsAt)) { _snprintf_s(g_why, _TRUNCATE, "index item pool: %u items at 0x%08X", used, itemsAt); return 0; }
	unsigned char lh[0x18];   // TDS+0x9C..+0xB3: the large list's node array, ..., count +0xA8, head +0xAC, tail +0xB0
	if (!SafeRead(A.tds + 0x9C, lh, sizeof(lh), &fault)) { _snprintf_s(g_why, _TRUNCATE, "the large list TDS+0x9C unreadable"); return fault ? -1 : 0; }
	const uint32_t nodesAt = U32(lh, 0), largeCount = U32(lh, 0xC), largeHead = U32(lh, 0x10);
	uint32_t nOut = 0;   // 0649: live records left out of the set; the large list may name them
	for (uint32_t b = 0; b < g_st.nb; ++b) nOut += g_b[b].outN;
	const uint32_t cells = cols * rows, n = g_st.n, nAll = g_st.n + nOut;
	WalkWork& w = g_wk;
	w.ox = ox; w.oy = oy; w.invx = invx; w.invy = invy; w.cols = cols; w.rows = rows; w.cells = cells; w.used = used; w.n = n;
	w.largeCount = largeCount;
	w.heads = (uint32_t*)malloc((size_t)cells * 4);
	w.items = used ? (unsigned char*)malloc((size_t)used * 24) : nullptr;
	w.large = (uint32_t*)malloc(sizeof(uint32_t) * 2 * ((size_t)(largeCount < nAll ? largeCount : nAll) + 1));
	w.slotBase = (uint32_t*)malloc(sizeof(uint32_t) * (g_st.nb + 1));
	w.rec = (const Decal**)malloc(sizeof(Decal*) * (n ? n : 1));
	w.out = (unsigned char*)calloc(1, sizeof(VPShellDecalWalkHeader) + (size_t)n * sizeof(VPShellDecalWalk) + 16);
	w.done = (unsigned char*)calloc(n ? n : 1, 1);
	if (!w.heads || (used && !w.items) || !w.large || !w.slotBase || !w.rec || !w.out || !w.done)
	{ _snprintf_s(g_why, _TRUNCATE, "out of memory (walk of %u cells, %u items)", cells, used); WalkFree(); return 0; }
	// the copies out of the EXE, all in this Present
	if (!SafeRead(headsAt, w.heads, (size_t)cells * 4, &fault)) { _snprintf_s(g_why, _TRUNCATE, "fault reading %u cell heads at 0x%08X", cells, headsAt); WalkFree(); return fault ? -1 : 0; }
	if (used && !SafeRead(itemsAt, w.items, (size_t)used * 24, &fault)) { _snprintf_s(g_why, _TRUNCATE, "fault reading %u index items at 0x%08X", used, itemsAt); WalkFree(); return fault ? -1 : 0; }
	for (uint32_t node = largeHead; node != DT_NIL;)
	{
		if (w.nLarge >= largeCount || w.nLarge > nAll || !nodesAt) { _snprintf_s(g_why, _TRUNCATE, "the large list is longer than its count %u, or a loop", largeCount); WalkFree(); return 0; }
		unsigned char nd[16];
		if (!SafeRead(nodesAt + node * 16u, nd, 16, &fault)) { _snprintf_s(g_why, _TRUNCATE, "fault reading large-list node %u at 0x%08X", node, nodesAt); WalkFree(); return fault ? -1 : 0; }
		w.large[2 * w.nLarge] = U32(nd, 8); w.large[2 * w.nLarge + 1] = U32(nd, 0xC); w.nLarge++;
		node = U32(nd, 0);
	}
	// (batch, slot) -> record index of the image (records in batch order, then each batch's list order)
	uint32_t total = 0;
	for (uint32_t b = 0, k = 0; b < g_st.nb; ++b)
	{
		uint32_t top = 0;
		for (uint32_t i = 0; i < g_b[b].n; ++i, ++k) { if (g_b[b].d[i].slot + 1 > top) top = g_b[b].d[i].slot + 1; w.rec[k] = &g_b[b].d[i]; }
		w.slotBase[b] = total; total += top;
	}
	w.slotBase[g_st.nb] = total;
	w.map = (uint32_t*)malloc(sizeof(uint32_t) * (total ? total : 1));
	if (!w.map) { _snprintf_s(g_why, _TRUNCATE, "out of memory (walk map of %u)", total); WalkFree(); return 0; }
	for (uint32_t i = 0; i < total; ++i) w.map[i] = DT_NIL;
	for (uint32_t b = 0, k = 0; b < g_st.nb; ++b)
		for (uint32_t i = 0; i < g_b[b].n; ++i, ++k) w.map[w.slotBase[b] + g_b[b].d[i].slot] = k;
	w.active = true;
	return 1;
}

static int WalkStep(int64_t t0)
{
	WalkWork& w = g_wk;
	VPShellDecalWalk* e = (VPShellDecalWalk*)(w.out + sizeof(VPShellDecalWalkHeader));
	while (w.cell < w.cells)
	{
		if (!Fits(t0, g_cWalk, DT_WALK_CHUNK)) return 3;
		const int64_t s0 = Qpc();
		uint32_t done = 0;
		for (; w.cell < w.cells && done < DT_WALK_CHUNK; ++w.cell)
		{
			const uint32_t c = w.cell;
			uint32_t rank = 0;
			for (uint32_t node = w.heads[c], guard = 0; node != DT_NIL; ++guard, ++done)
			{
				if (node >= w.used || guard > w.used) { _snprintf_s(g_why, _TRUNCATE, "cell %u: item %u past the %u items used, or a loop", c, node, w.used); return 0; }
				const unsigned char* it = w.items + (size_t)node * 24;
				const uint32_t w0 = U32(it, 0), batch = U32(it, 0x10), slot = U32(it, 0x14);
				if (w0 & 1) { _snprintf_s(g_why, _TRUNCATE, "cell %u: item %u is free", c, node); return 0; }
				const uint32_t k = WalkLookup(batch, slot);
				if (k == DT_NIL && WalkOut(batch, slot)) { node = w0 >> 1; continue; }   // 0649: left out of the set
				if (k == DT_NIL || w.done[k]) { g_walkMissBatch = batch; _snprintf_s(g_why, _TRUNCATE, "cell %u: item %u names batch %u slot %u, %s", c, node, batch, slot, k == DT_NIL ? "not a record of the snapshot" : "a record listed twice"); return 0; }
				const Decal& d = *w.rec[k];
				const float px = F32(it, 8), py = F32(it, 0xC);
				const float cx = WalkMul(d.rect[0] + d.rect[2], 0.5f), cy = WalkMul(d.rect[1] + d.rect[3], 0.5f);
				const uint32_t pc = WalkCell(px, w.ox, w.invx, w.cols) + WalkCell(py, w.oy, w.invy, w.rows) * w.cols;
				const bool large = WalkLarge(d);
				if (memcmp(&px, &cx, 4) || memcmp(&py, &cy, 4) || large || pc != c)
				{
					g_walkMissBatch = batch;
					_snprintf_s(g_why, _TRUNCATE, "cell %u: item %u (batch %u slot %u) has the point %g, %g; the record's centre %g, %g, %s, its cell %u",
						c, node, batch, slot, px, py, cx, cy, large ? "large" : "small", pc);
					return 0;
				}
				e[k].cell = c; e[k].rank = rank++; e[k].point[0] = px; e[k].point[1] = py;
				w.done[k] = 1; w.nSmall++;
				node = w0 >> 1;
			}
		}
		Learn(g_cWalk, s0, done ? done : 1);
	}
	return 1;
}

static int WalkEnd()
{
	WalkWork& w = g_wk;
	VPShellDecalWalk* e = (VPShellDecalWalk*)(w.out + sizeof(VPShellDecalWalkHeader));
	uint32_t kept = 0;   // 0649: large-list nodes of records in the set (the ranks); nodes of records left out take none
	for (uint32_t r = 0; r < w.nLarge; ++r)
	{
		const uint32_t batch = w.large[2 * r], slot = w.large[2 * r + 1], k = WalkLookup(batch, slot);
		if (k == DT_NIL && WalkOut(batch, slot)) continue;
		if (k == DT_NIL || w.done[k]) { g_walkMissBatch = batch; _snprintf_s(g_why, _TRUNCATE, "large-list node %u names batch %u slot %u, %s", r, batch, slot, k == DT_NIL ? "not a record of the snapshot" : "a record listed twice"); return 0; }
		if (!WalkLarge(*w.rec[k])) { g_walkMissBatch = batch; _snprintf_s(g_why, _TRUNCATE, "large-list node %u (batch %u slot %u) is a small decal", r, batch, slot); return 0; }
		e[k].cell = VPSHELL_DECAL_WALK_LARGE; e[k].rank = kept++; e[k].point[0] = 0; e[k].point[1] = 0;
		w.done[k] = 1;
	}
	if (w.nLarge != w.largeCount) { _snprintf_s(g_why, _TRUNCATE, "the large list holds %u nodes, its count says %u", w.nLarge, w.largeCount); return 0; }
	w.nLarge = kept;
	if (w.nSmall + w.nLarge != w.n)
	{
		// 0406: the batch of the first record found in neither (records are in batch order)
		uint32_t first = 0;
		while (first < w.n && w.done[first]) first++;
		for (uint32_t b = 0, k = 0; b < g_st.nb; k += g_b[b].n, ++b) if (first >= k && first < k + g_b[b].n) { g_walkMissBatch = b; break; }
		_snprintf_s(g_why, _TRUNCATE, "%u of %u records are in neither the index nor the large list (the first: record %u, batch %u slot %u)",
			w.n - w.nSmall - w.nLarge, w.n, first, g_walkMissBatch, first < w.n ? w.rec[first]->slot : DT_NIL);
		return 0;
	}
	VPShellDecalWalkHeader hd = {};
	hd.origin[0] = w.ox; hd.origin[1] = w.oy; hd.inverse[0] = w.invx; hd.inverse[1] = w.invy; hd.cols = w.cols; hd.rows = w.rows;
	hd.smallCount = w.nSmall; hd.largeCount = w.nLarge;
	memcpy(w.out, &hd, sizeof(hd));
	free(g_st.walk);
	g_st.walk = w.out; w.out = nullptr;
	g_st.walkBytes = (uint32_t)(sizeof(VPShellDecalWalkHeader) + (size_t)w.n * sizeof(VPShellDecalWalk));
	g_st.walkSmall = w.nSmall; g_st.walkLarge = w.nLarge;
	return 1;
}

static bool AreaEqualsPublished()
{
	if (g_sw.tx != g_areaPubX || g_sw.ty != g_areaPubY) return false;
	return !g_sw.n || !memcmp(g_sw.bits, g_areaPub, g_sw.n);
}

// ---- names ---------------------------------------------------------------------------------------------------------------
static uint16_t* g_nameIdx;      // handle -> 1 + index into g_names (0 = none yet)
static VPShellDecalName* g_names; static uint32_t g_nNames, g_capNames;
static bool* g_used;              // handle in use by this generation

static uint32_t AddName(uint32_t h, const char* path)
{
	if (g_nNames >= g_capNames)
	{
		uint32_t c = g_capNames ? g_capNames * 2 : 64;
		VPShellDecalName* n = (VPShellDecalName*)realloc(g_names, c * sizeof(VPShellDecalName));
		if (!n) return 0;
		g_names = n; g_capNames = c;
	}
	VPShellDecalName& e = g_names[g_nNames++];
	memset(&e, 0, sizeof(e));
	e.handle = h;
	strncpy_s(e.path, path, _TRUNCATE);
	g_nameIdx[h] = (uint16_t)g_nNames;
	return g_nNames;
}

// In-order walk of an MSVC std::map tree (terrain-decals.md 8.1), visit(node bytes) for each node. 1 ok, 0 layout, -1 fault.
template <typename F>
static int WalkTree(uint32_t headAddr, uint32_t size, const char* what, F visit)
{
	bool fault = false;
	if (!headAddr || size > DT_MAX_TREE) { _snprintf_s(g_why, _TRUNCATE, "%s: head 0x%08X, size %u", what, headAddr, size); return 0; }
	unsigned char head[0x18];
	if (!SafeRead(headAddr, head, sizeof(head), &fault)) { _snprintf_s(g_why, _TRUNCATE, "%s: head unreadable", what); return fault ? -1 : 0; }
	uint32_t stack[DT_STACK]; unsigned char nodes[DT_STACK][0x18]; int sp = 0;
	uint32_t node = U32(head, 4), seen = 0;
	for (;;)
	{
		while (node && node != headAddr)
		{
			if (sp >= DT_STACK) { _snprintf_s(g_why, _TRUNCATE, "%s: tree deeper than %d", what, DT_STACK); return 0; }
			if (!SafeRead(node, nodes[sp], 0x18, &fault)) { _snprintf_s(g_why, _TRUNCATE, "%s: node 0x%08X unreadable", what, node); return fault ? -1 : 0; }
			if (nodes[sp][0x15]) break;
			stack[sp] = node; sp++;
			node = U32(nodes[sp - 1], 0);
			if (++seen > size + 4) { _snprintf_s(g_why, _TRUNCATE, "%s: walk longer than its size %u", what, size); return 0; }
		}
		if (!sp) break;
		sp--;
		int v = visit(nodes[sp]);
		if (v != 1) return v;
		node = U32(nodes[sp], 8);
	}
	return 1;
}

// Names every handle in g_used (the map first, then the blank specs and the farm table). 1 ok (all named), 0 a handle
// without a name or a layout failure (g_why), -1 fault.
static int ResolveNames()
{
	bool fault = false;
	uint32_t mgr = 0;
	if (!SafeRead(A.resmgr, &mgr, 4, &fault) || !mgr) { _snprintf_s(g_why, _TRUNCATE, "resource manager not readable (0x%08X)", mgr); return fault ? -1 : 0; }
	unsigned char m[12];
	if (!SafeRead(mgr + 0x40, m, sizeof(m), &fault)) { _snprintf_s(g_why, _TRUNCATE, "resource manager maps unreadable"); return fault ? -1 : 0; }
	uint32_t texMap = 0;
	int rc = WalkTree(U32(m, 4), U32(m, 8), "resource type map", [&](const unsigned char* n) {
		if (U32(n, 0xC) == 8) texMap = U32(n, 0x10);
		return 1;
	});
	if (rc != 1) return rc;
	if (!texMap) { _snprintf_s(g_why, _TRUNCATE, "the resource manager has no texture map (type 8)"); return 0; }
	unsigned char tm[12];
	if (!SafeRead(texMap, tm, sizeof(tm), &fault)) { _snprintf_s(g_why, _TRUNCATE, "texture map unreadable"); return fault ? -1 : 0; }
	uint32_t wanted = 0;
	for (uint32_t h = 1; h < DT_MAX_HANDLE; ++h) wanted += g_used[h] && !g_nameIdx[h];
	if (wanted)
	{
		char nm[VPSHELL_DECAL_NAME_CHARS];
		rc = WalkTree(U32(tm, 4), U32(tm, 8), "texture map", [&](const unsigned char* n) {
			const uint32_t key = U32(n, 0xC), res = U32(n, 0x10);
			uint32_t obj = 0, h = 0;
			if (!res) return 1;
			if (!SafeRead(res + 0x1C, &obj, 4, &fault)) { _snprintf_s(g_why, _TRUNCATE, "resource 0x%08X unreadable", res); return fault ? -1 : 0; }
			if (!obj) return 1;
			if (!SafeRead(obj + 8, &h, 4, &fault)) { _snprintf_s(g_why, _TRUNCATE, "texture object 0x%08X unreadable", obj); return fault ? -1 : 0; }
			if (h >= DT_MAX_HANDLE || !g_used[h] || g_nameIdx[h]) return 1;
			if (!SafeStr(key, nm, sizeof(nm), &fault)) { _snprintf_s(g_why, _TRUNCATE, "name of handle %u unreadable", h); return fault ? -1 : 0; }
			if (!nm[0]) return 1;
			return AddName(h, nm) ? 1 : 0;
		});
		if (rc != 1) return rc;
	}
	uint32_t blank = 0;
	if (!SafeRead(A.tds + 0x4F0, &blank, 4, &fault)) return fault ? -1 : 0;
	if (blank && blank < DT_MAX_HANDLE && g_used[blank] && !g_nameIdx[blank]) AddName(blank, "blank_texture_spec_decal.dds");
	// the farm-type table (8.3)
	unsigned char f[DT_FARM_BYTES];
	if (!SafeRead(A.farm, f, sizeof(f), &fault)) { _snprintf_s(g_why, _TRUNCATE, "farm-type table unreadable"); return fault ? -1 : 0; }
	const uint32_t tab = U32(f, 0x44), n = U32(f, 0x48), cap = U32(f, 0x4C), def = U32(f, 0x454);
	if (n > cap || cap > 4096 || (n && !tab)) { _snprintf_s(g_why, _TRUNCATE, "farm-type table: count %u capacity %u at 0x%08X", n, cap, tab); return 0; }
	if (def && def < DT_MAX_HANDLE && g_used[def] && !g_nameIdx[def]) AddName(def, "blank_texture.dds");
	if (n)
	{
		unsigned char* v = (unsigned char*)malloc((size_t)n * 16);
		if (!v) return 0;
		if (!SafeRead(tab, v, (size_t)n * 16, &fault)) { free(v); _snprintf_s(g_why, _TRUNCATE, "farm variants unreadable"); return fault ? -1 : 0; }
		uint32_t total = 0;
		for (uint32_t k = 0; k < DT_FARM_CATS && rc == 1; ++k)
		{
			const uint32_t first = U16(f, 0x24 + 4 * k), cnt = U16(f, 0x26 + 4 * k);
			if (first + cnt > n) { _snprintf_s(g_why, _TRUNCATE, "farm category %s: %u + %u past %u variants", kFarmCats[k], first, cnt, n); rc = 0; break; }
			total += cnt;
			for (uint32_t j = 0; j < cnt; ++j)
				for (uint32_t a = 0; a < 3; ++a)
				{
					const uint32_t h = U32(v, 16 * (first + j) + 4 + 4 * a);
					if (h && h < DT_MAX_HANDLE && g_used[h] && !g_nameIdx[h])
					{
						char ref[VPSHELL_DECAL_NAME_CHARS];
						_snprintf_s(ref, _TRUNCATE, "@farm_types.xml/%s/%u/%s", kFarmCats[k], j, kAttrs[a]);
						AddName(h, ref);
					}
				}
		}
		free(v);
		if (rc == 1 && total != n) { _snprintf_s(g_why, _TRUNCATE, "farm categories hold %u variants, the table %u", total, n); rc = 0; }
		if (rc != 1) return rc;
	}
	for (uint32_t h = 1; h < DT_MAX_HANDLE; ++h)
		if (g_used[h] && !g_nameIdx[h]) { _snprintf_s(g_why, _TRUNCATE, "texture handle %u has no name (not in the resource map, not a blank spec, not a farm type)", h); return 0; }
	return 1;
}

// ---- publication -----------------------------------------------------------------------------------------------------------
static int CmpPub(const void* a, const void* b)
{
	const uint64_t x = ((const Pub*)a)->hash, y = ((const Pub*)b)->hash;
	return x < y ? -1 : x > y ? 1 : 0;
}

static void CloseSections(bool cur)
{
	if (g_secPrev) { CloseHandle(g_secPrev); g_secPrev = nullptr; }
	if (cur && g_secCur) { CloseHandle(g_secCur); g_secCur = nullptr; }
}

// ---- the step budget (4110) -------------------------------------------------------------------------------------------
// A step runs when the Present's time so far plus its estimate (cost per unit x units) stays within decal_budget_ms, or
// when it is the Present's first step (every Present that has work makes progress). The estimate is a decaying maximum
// of the measured cost per unit, so one slow step makes the next Presents careful and a quiet run lowers it again.
static bool Fits(int64_t t0, const Cost& c, double units)
{
	if (!g_stepDone) return true;
	return Ms(Qpc() - t0) + c.per * units <= (double)g_budgetMs;
}

static void Learn(Cost& c, int64_t s0, double units)
{
	g_stepDone = true;
	if (units < 1) units = 1;
	const double ms = Ms(Qpc() - s0), v = ms / units;
	c.per = v >= c.per ? v : c.per * 0.8 + v * 0.2;
	if (ms > c.winMax) c.winMax = ms;
}

// The cost of the Presents of one publication (or of one read that found no change): from the first read to READY.
static void WindowBegin()
{
	if (g_winActive) return;
	g_winActive = true; g_winEnd = false; g_winMax = 0; g_winSum = 0; g_winN = 0; g_winRestarts = 0;
	for (Cost* c : kCosts) c->winMax = 0;
}

static void WindowEnd(const char* what, uint32_t gen)
{
	if (!g_winActive) return;
	g_winEnd = true; g_winGen = gen; g_winRestarts = g_buildRestarts;
	strncpy_s(g_winWhat, what, _TRUNCATE);
}

static void DropStage()
{
	free(g_st.img); g_st.img = nullptr;
	free(g_st.set); g_st.set = nullptr;
	free(g_st.tmp); g_st.tmp = nullptr;
	free(g_st.ch); g_st.ch = nullptr;
	free(g_st.nameOf); g_st.nameOf = nullptr;
	free(g_st.readSeq); g_st.readSeq = nullptr;
	free(g_st.runs); g_st.runs = nullptr;
	free(g_st.runs2); g_st.runs2 = nullptr;
	free(g_st.area); g_st.area = nullptr;
	free(g_st.walk); g_st.walk = nullptr;
	WalkFree();
	if (g_st.sec) { CloseHandle(g_st.sec); g_st.sec = nullptr; }
	g_st.stage = ST_NONE;
}

// Starts a build of the generation from every batch read (the snapshot); StepBuild does the work over the next Presents.
// Nothing is published when the content set equals the current one.
static void Compose(const char* why)
{
	uint32_t n = 0, vBytes = 0;
	for (uint32_t b = 0; b < g_nb; ++b) { n += g_b[b].n; vBytes += g_b[b].vBytes; }
	if (n > VPSHELL_DECAL_MAX_DECALS) { _snprintf_s(g_why, _TRUNCATE, "%u decals, at most %u", n, VPSHELL_DECAL_MAX_DECALS); Fail(VPDTAP_FAIL_TOO_BIG, g_why, false); WindowEnd("failed", g_generation); return; }
	DropStage();
	memset(&g_st, 0, sizeof(g_st));
	g_st.set = (Pub*)malloc(sizeof(Pub) * (n ? n : 1));
	g_st.tmp = (Pub*)malloc(sizeof(Pub) * (n ? n : 1));
	g_st.readSeq = (uint32_t*)malloc(sizeof(uint32_t) * (g_nb + 1));
	g_st.runs = (uint32_t*)malloc(sizeof(uint32_t) * (g_nb + 2));
	g_st.runs2 = (uint32_t*)malloc(sizeof(uint32_t) * (g_nb + 2));
	if (!g_st.set || !g_st.tmp || !g_st.readSeq || !g_st.runs || !g_st.runs2)
	{ DropStage(); Fail(VPDTAP_FAIL_TOO_BIG, "out of memory (decal set)", false); WindowEnd("failed", g_generation); return; }
	g_st.stage = ST_BUILD; g_st.step = B_AREA; g_st.cur = 0; g_st.nb = g_nb; g_st.n = n; g_st.vBytes = vBytes;
	AreaFree(); g_swIdle = false;   // the build reads the tiles' +0x300 itself (B_AREA)
	uint32_t k = 0;
	for (uint32_t b = 0; b < g_nb; ++b) { g_st.readSeq[b] = g_b[b].readSeq; g_st.runs[b] = k; k += g_b[b].n; }
	g_st.runs[g_nb] = n; g_st.nRuns = g_nb;
	g_st.fnv = 0xCBF29CE484222325ull;
	g_st.startQpc = Qpc();
	g_walkRetry = false;   // 0406: a held build's new build has begun
	strncpy_s(g_st.why, why, _TRUNCATE);
}

// The tap's own check before the image leaves the build: every batch of the snapshot still holds the copy the build
// began with (no read, no invalidation since), and the batch count did not shrink.
static bool SnapshotIntact()
{
	if (g_nb < g_st.nb) return false;
	for (uint32_t b = 0; b < g_st.nb; ++b) if (!g_b[b].valid || g_b[b].readSeq != g_st.readSeq[b]) return false;
	return true;
}

// One Present's share of the build. Returns false when the budget is used up (or the build ended without a stage).
static bool StepBuild(int64_t t0)
{
	while (g_st.stage == ST_BUILD)
	{
		const int64_t s0 = Qpc();
		switch (g_st.step)
		{
		case B_AREA:  // 4111: every tile's +0x300, the area of this generation (a sweep, DT_AREA_CHUNK tiles per step)
		{
			if (g_test == T_HUGE && g_midAtArea && g_midBuild && !g_st.midDone)
			{
				// 0406 test: the EXE changes a batch of the snapshot before the build reaches its walk (as at a load)
				g_midBuild--; g_st.midDone = true;
				HugeToggle();
				DLOG("decal_tap: test: batch %u changed before the walk (decal %u %s)", HW_TOGGLE % HW_NB, HW_TOGGLE, g_hw->toggledOff ? "freed" : "back");
				return false;
			}
			int r = 1;
			if (!g_sw.active && !g_sw.done) { r = AreaBegin(); g_stepDone = true; }
			if (r == 1) r = AreaStep(t0);
			if (r == 3) return false;
			if (r == 2)
			{
				AreaFree();
				if (++g_areaRestarts <= 8) break;
				_snprintf_s(g_why, _TRUNCATE, "the tile grid moved during 8 sweeps in a row");
				r = 0;
			}
			if (r <= 0)
			{
				AreaFree(); DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0; g_areaRestarts = 0;
				if (r < 0) { g_sehCount++; Fail(VPDTAP_FAIL_SEH, g_why, true); }
				else Fail(VPDTAP_FAIL_LAYOUT, g_why, false);
				WindowEnd("failed", g_generation);
				return false;
			}
			g_areaRestarts = 0;
			g_st.area = g_sw.bits; g_sw.bits = nullptr; g_st.areaX = g_sw.tx; g_st.areaY = g_sw.ty; g_st.areaSet = 0;
			for (uint32_t i = 0; i < g_sw.n; ++i) g_st.areaSet += g_st.area[i];
			AreaFree();
			g_st.step = B_WALK; g_st.cur = 0;
			break;
		}
		case B_WALK:  // 0366: the spatial index and the large list against this snapshot (the walk block), in steps
		{
			if (!g_walkDead)
			{
				int r = 1;
				if (!g_wk.active)
				{
					const double units = (double)g_st.n * 24 + 4096;
					if (!Fits(t0, g_cWalkRead, units)) return false;
					r = WalkBegin();
					Learn(g_cWalkRead, s0, units);
				}
				if (r == 1) r = WalkStep(t0);
				if (r == 3) return false;
				if (r == 1) r = WalkEnd();
				WalkFree();
				if (r < 0) { g_walkDead = true; g_walkMisses++; Loud("walk: %s - no walk block for the life of the process (the decals go on)", g_why); }
				else if (r == 0)
				{
					g_walkMisses++; g_wantVersion++;
					// 0406 (terrain-decals-walk.md): a miss means the snapshot and the index disagree. When a batch moved
					// after the build began (the restart limit reached: the snapshot is published stale by design, as at
					// a load), or the published generation has no walk either (the tie rule does not change), the
					// generation goes out without the block as before. Otherwise the snapshot only looks intact (the
					// EXE replaced a record with the batch's counters equal: the safety net finds it later): hold the
					// build, read the batch the miss names again and build again, at most g_walkHoldMax times in a row.
					bool moved = g_nb != g_st.nb;
					for (uint32_t b = 0; b < g_nb && !moved; ++b) moved = g_b[b].moved;
					if (!moved && g_walkPub && g_walkHoldRun < g_walkHoldMax)
					{
						g_walkHoldRun++; g_walkHolds++;
						const bool named = g_walkMissBatch < g_nb && g_walkMissBatch < g_st.nb;
						if (named) g_b[g_walkMissBatch].moved = true;
						g_walkRetry = true;
						DLOG("decal_tap: walk check missed: %s - the build is held: %s and the build repeated (hold %u of %u; the "
							"published generation %u keeps its walk block)", g_why, named ? "the batch it names is read again" :
							"no batch named, the index is read again", g_walkHoldRun, g_walkHoldMax, g_generation);
						DropStage();
						return false;
					}
					DLOG("decal_tap: no walk block for this generation: %s (%s)", g_why,
						moved ? "a batch moved after the build began and the restart limit was reached: the snapshot is older "
						"than the index" : !g_generation ? "nothing published yet" : !g_walkPub ?
						"the published generation has no walk block either" :
						"the walk check missed after the holds");
				}
			}
			g_st.step = B_SET; g_st.cur = 0;
			break;
		}
		case B_SET:   // per batch: its hashes and rectangles into its run of the set, sorted
		{
			if (g_st.cur >= g_st.nb) { g_st.step = B_MERGE; g_st.cur = 0; g_st.runs2[0] = 0; g_st.nRuns2 = 0; break; }
			const uint32_t b = g_st.cur, n = g_b[b].n;
			if (!Fits(t0, g_cSet, n)) return false;
			Pub* p = g_st.set + g_st.runs[b];
			for (uint32_t i = 0; i < n; ++i) { p[i].hash = g_b[b].d[i].hash; memcpy(p[i].rect, g_b[b].d[i].rect, 16); }
			qsort(p, n, sizeof(Pub), CmpPub);
			g_st.cur++;
			Learn(g_cSet, s0, n);
			break;
		}
		case B_MERGE:  // bottom-up: one pair of adjacent runs per step, a pass at a time (set -> tmp, then swapped)
		{
			if (g_st.nRuns <= 1) { g_st.step = B_SAME; break; }
			const uint32_t r = 2 * g_st.cur;
			if (r >= g_st.nRuns)
			{
				Pub* t = g_st.set; g_st.set = g_st.tmp; g_st.tmp = t;
				uint32_t* rr = g_st.runs; g_st.runs = g_st.runs2; g_st.runs2 = rr;
				g_st.nRuns = g_st.nRuns2; g_st.runs[g_st.nRuns] = g_st.n; g_st.cur = 0; g_st.nRuns2 = 0;
				break;
			}
			const uint32_t a0 = g_st.runs[r], a1 = g_st.runs[r + 1], b1 = r + 2 <= g_st.nRuns ? g_st.runs[r + 2] : a1;
			const uint32_t len = b1 - a0;
			if (!Fits(t0, g_cMerge, len)) return false;
			uint32_t i = a0, j = a1, o = a0;
			while (i < a1 && j < b1) g_st.tmp[o++] = g_st.set[j].hash < g_st.set[i].hash ? g_st.set[j++] : g_st.set[i++];
			while (i < a1) g_st.tmp[o++] = g_st.set[i++];
			while (j < b1) g_st.tmp[o++] = g_st.set[j++];
			g_st.runs2[g_st.nRuns2++] = a0;
			g_st.cur++;
			Learn(g_cMerge, s0, len);
			break;
		}
		case B_SAME:   // the content set against the published one: equal = no generation
		{
			const uint32_t n = g_st.n;
			if (!Fits(t0, g_cSame, n)) return false;
			const uint32_t na = g_st.areaX * g_st.areaY;
			const bool sameArea = g_st.areaX == g_areaPubX && g_st.areaY == g_areaPubY && (!na || !memcmp(g_st.area, g_areaPub, na));
			const bool sameWalk = (!g_st.walk && !g_walkPub) || (g_st.walk && g_walkPub && g_st.walkBytes == g_walkPubBytes &&
				!memcmp(g_st.walk, g_walkPub, g_st.walkBytes));   // 0366
			const bool same = g_generation && !g_namesFailed && n == g_nPub && !memcmp(g_st.set, g_pub, sizeof(Pub) * n) && sameArea && sameWalk &&
				g_state != VPDTAP_FAILED;
			Learn(g_cSame, s0, n);
			if (same)
			{
				DropStage();
				WindowEnd("no change of the content set", g_generation);
				g_buildRestarts = 0; g_walkHoldRun = 0;
				return false;
			}
			g_st.step = B_USED;
			break;
		}
		case B_USED:   // every handle in use
		{
			if (!Fits(t0, g_cUsed, g_st.n)) return false;
			memset(g_used, 0, sizeof(bool) * DT_MAX_HANDLE);
			for (uint32_t b = 0; b < g_st.nb; ++b)
				for (uint32_t i = 0; i < g_b[b].n; ++i)
					for (int t = 0; t < 3; ++t) g_used[g_b[b].d[i].tex[t]] = true;
			g_used[0] = false;
			Learn(g_cUsed, s0, g_st.n);
			g_st.step = B_NAMES;
			break;
		}
		case B_NAMES:  // their names (one walk of the texture map; names already known are kept)
		{
			if (!Fits(t0, g_cNames, 1)) return false;
			const int rn = ResolveNames();
			Learn(g_cNames, s0, 1);
			if (rn != 1)
			{
				DropStage();
				g_buildRestarts = 0; g_walkHoldRun = 0;
				if (rn < 0) { g_sehCount++; Fail(VPDTAP_FAIL_SEH, g_why, true); WindowEnd("failed", g_generation); return false; }
				g_namesFailed = true;
				Fail(VPDTAP_FAIL_NAMES, g_why, false);
				WindowEnd("failed", g_generation);
				return false;
			}
			g_namesFailed = false;
			g_st.step = B_DIFF;
			break;
		}
		case B_DIFF:   // the changed rectangles against the last acknowledged generation (every rectangle when none)
		{
			const uint32_t n = g_st.n;
			if (!Fits(t0, g_cDiff, (double)n + g_nBase)) return false;
			g_st.ch = (Pub*)malloc(sizeof(Pub) * ((size_t)n + g_nBase + 1));
			if (!g_st.ch) { DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0; Fail(VPDTAP_FAIL_TOO_BIG, "out of memory (changed set)", false); WindowEnd("failed", g_generation); return false; }
			Pub* ch = g_st.ch; const Pub* set = g_st.set;
			uint32_t nChanged = 0, added = 0, removed = 0;
			if (!g_baseGen)
				for (uint32_t i = 0; i < n; ++i) { ch[nChanged++] = set[i]; added++; }
			else
			{
				uint32_t i = 0, j = 0;
				while (i < n || j < g_nBase)
				{
					if (j >= g_nBase || (i < n && set[i].hash < g_base[j].hash)) { ch[nChanged++] = set[i++]; added++; }
					else if (i >= n || g_base[j].hash < set[i].hash) { ch[nChanged++] = g_base[j++]; removed++; }
					else { i++; j++; }
				}
			}
			g_st.nChanged = nChanged; g_st.added = added; g_st.removed = removed; g_st.baseGen = g_baseGen;
			Learn(g_cDiff, s0, (double)n + g_nBase);
			g_st.step = B_ALLOC;
			break;
		}
		case B_ALLOC:  // the image's layout and its memory
		{
			const double units = (double)g_st.n * 48 + g_st.vBytes;
			if (!Fits(t0, g_cAlloc, units)) return false;
			uint32_t used = 0;
			for (uint32_t h = 1; h < DT_MAX_HANDLE; ++h) used += g_used[h];
			auto al = [](uint64_t o) { return (o + 15) & ~15ull; };
			g_st.used = used;
			g_st.decalOff = al(sizeof(VPShellDecalImage));
			g_st.nameOff = al(g_st.decalOff + (uint64_t)g_st.n * sizeof(VPShellDecalRecord));
			g_st.vertOff = al(g_st.nameOff + (uint64_t)used * sizeof(VPShellDecalName));
			g_st.chOff = al(g_st.vertOff + g_st.vBytes);
			g_st.hashOff = al(g_st.chOff + (uint64_t)g_st.nChanged * 16);
			const uint64_t areaBytes = ((uint64_t)g_st.areaX * g_st.areaY + 7) / 8;
			g_st.areaOff = areaBytes ? al(g_st.hashOff + (uint64_t)g_st.n * 8) : 0;
			const uint64_t end = areaBytes ? al(g_st.areaOff + areaBytes) : al(g_st.hashOff + (uint64_t)g_st.n * 8);
			g_st.walkOff = g_st.walk ? end : 0;   // 0366
			g_st.total = g_st.walk ? al(g_st.walkOff + g_st.walkBytes) : end;
			if (g_st.total > g_maxBytes)
			{
				_snprintf_s(g_why, _TRUNCATE, "image of %llu bytes, the limit is %llu", (unsigned long long)g_st.total, (unsigned long long)g_maxBytes);
				DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0;
				Fail(VPDTAP_FAIL_TOO_BIG, g_why, false);
				WindowEnd("failed", g_generation);
				return false;
			}
			g_st.img = (unsigned char*)calloc(1, (size_t)g_st.total);
			g_st.nameOf = (uint32_t*)malloc(sizeof(uint32_t) * DT_MAX_HANDLE);
			if (!g_st.img || !g_st.nameOf) { DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0; Fail(VPDTAP_FAIL_TOO_BIG, "out of memory (image)", false); WindowEnd("failed", g_generation); return false; }
			for (uint32_t h = 0; h < DT_MAX_HANDLE; ++h) g_st.nameOf[h] = VPSHELL_DECAL_NO_TEXTURE;
			g_st.cur = 0; g_st.nNames = 0; g_st.nv = 0;
			Learn(g_cAlloc, s0, units);
			g_st.step = B_RECORDS;
			break;
		}
		case B_RECORDS:  // per batch: records (names in order of first use), vertices, content hashes
		{
			if (g_st.cur >= g_st.nb) { g_st.step = B_FINISH; break; }
			const uint32_t b = g_st.cur;
			const double units = (double)g_b[b].n * 48 + g_b[b].vBytes;
			if (!Fits(t0, g_cRec, units)) return false;
			if (g_test == T_HUGE && g_midBuild && !g_st.midDone && b == HW_TOGGLE % HW_NB)
			{
				// test: the EXE changes a batch of the snapshot while the build is half done; the rest of the build waits
				// for the next Present, whose poll sees the change
				g_midBuild--; g_st.midDone = true;
				HugeToggle();
				DLOG("decal_tap: test: batch %u changed during the build (decal %u %s)", b, HW_TOGGLE, g_hw->toggledOff ? "freed" : "back");
				return false;
			}
			VPShellDecalRecord* rec = (VPShellDecalRecord*)(g_st.img + g_st.decalOff);
			VPShellDecalName* names = (VPShellDecalName*)(g_st.img + g_st.nameOff);
			uint64_t* hashes = (uint64_t*)(g_st.img + g_st.hashOff);
			uint32_t order = 0;   // records in batch order, then the EXE's list order
			for (uint32_t c = 0; c < b; ++c) order += g_b[c].n;
			for (uint32_t i = 0; i < g_b[b].n; ++i, ++order)
			{
				const Decal& d = g_b[b].d[i];
				VPShellDecalRecord& r = rec[order];
				memcpy(r.rect, d.rect, 16); r.priority = d.priority; r.flags = d.pass;
				for (int t = 0; t < 3; ++t)
				{
					const uint32_t h = d.tex[t];
					if (!h) { r.texture[t] = VPSHELL_DECAL_NO_TEXTURE; continue; }
					if (g_st.nameOf[h] == VPSHELL_DECAL_NO_TEXTURE) { names[g_st.nNames] = g_names[g_nameIdx[h] - 1]; g_st.nameOf[h] = g_st.nNames++; }
					r.texture[t] = g_st.nameOf[h];
				}
				r.firstVertex = g_st.nv; r.triangles = d.tris; r.listOrder = order;
				memcpy(g_st.img + g_st.vertOff + (size_t)g_st.nv * 8, g_b[b].v + d.vOff, 24u * d.tris);
				g_st.nv += 3 * d.tris;
				hashes[order] = d.hash;
			}
			g_st.cur++;
			Learn(g_cRec, s0, units);
			break;
		}
		case B_FINISH:  // the changed block, the header; the snapshot check
		{
			if (!Fits(t0, g_cFinish, g_st.nChanged)) return false;
			if (!SnapshotIntact())
			{
				Loud("the decal records the build began with changed under it (batch count %u, was %u) - the build starts again", g_nb, g_st.nb);
				DropStage();
				g_buildRestarts++;
				return false;
			}
			for (uint32_t i = 0; i < g_st.nChanged; ++i) memcpy(g_st.img + g_st.chOff + 16ull * i, g_st.ch[i].rect, 16);
			if (g_st.areaOff)   // one bit per tile, (y * x tiles + x), the image is zeroed
			{
				unsigned char* a = g_st.img + g_st.areaOff;
				for (uint32_t k = 0; k < g_st.areaX * g_st.areaY; ++k) if (g_st.area[k]) a[k >> 3] |= (unsigned char)(1u << (k & 7));
			}
			if (g_st.walk) memcpy(g_st.img + g_st.walkOff, g_st.walk, g_st.walkBytes);   // 0366
			free(g_st.ch); g_st.ch = nullptr; free(g_st.nameOf); g_st.nameOf = nullptr;
			free(g_st.tmp); g_st.tmp = nullptr;
			VPShellDecalImage h = {};
			h.magic = VPSHELL_DECAL_MAGIC; h.version = VPSHELL_DECAL_IMAGE_VERSION; h.headerBytes = sizeof(h); h.decalBytes = sizeof(VPShellDecalRecord);
			h.generation = g_generation + 1; h.loadSeq = g_loadSeq; h.decalCount = g_st.n; h.nameCount = g_st.nNames; h.vertexCount = g_st.nv;
			h.changedCount = g_st.nChanged; h.decalOffset = (uint32_t)g_st.decalOff; h.nameOffset = (uint32_t)g_st.nameOff;
			h.vertexOffset = (uint32_t)g_st.vertOff; h.changedOffset = (uint32_t)g_st.chOff;
			for (int i = 0; i < 4; ++i) h.worldRect[i] = F32(g_hdr, 0x6C + 4 * i);
			h.imageBytes = g_st.total; h.changedSince = g_st.baseGen; h.hashOffset = (uint32_t)g_st.hashOff;
			h.areaOffset = (uint32_t)g_st.areaOff; h.areaTilesX = g_st.areaOff ? g_st.areaX : 0; h.areaTilesY = g_st.areaOff ? g_st.areaY : 0;
			h.walkOffset = (uint32_t)g_st.walkOff;
			memcpy(g_st.img, &h, sizeof(h));
			Learn(g_cFinish, s0, g_st.nChanged);
			g_st.stage = ST_HASH; g_st.pos = 0; g_st.fnv = 0xCBF29CE484222325ull;
			break;
		}
		}
		g_st.steps++;
		g_st.buildMs += (float)Ms(Qpc() - s0);
	}
	return true;
}

// One Present's share of a publication after the build: the image's FNV-1a 64 in 64 KiB steps, then the section in
// views of at most 256 KiB, each step only when its estimate fits decal_budget_ms; the last step publishes READY.
// Returns false when the budget is used up.
static bool StepPublish(int64_t t0)
{
	const uint64_t CH = 64u * 1024u, VIEW = 256u * 1024u;
	while (g_st.stage == ST_HASH)
	{
		const uint64_t nb = g_st.total - g_st.pos < CH ? g_st.total - g_st.pos : CH;
		if (!Fits(t0, g_cHash, (double)nb)) return false;
		const int64_t s0 = Qpc();
		g_st.fnv = Fnv(g_st.fnv, g_st.img + g_st.pos, (size_t)nb);
		g_st.pos += nb;
		Learn(g_cHash, s0, (double)nb);
		if (g_st.pos >= g_st.total) { g_st.stage = ST_SECTION; g_st.pos = 0; }
	}
	const uint32_t gen = g_generation + 1;
	wchar_t name[64]; _snwprintf_s(name, _TRUNCATE, VPSHELL_DECAL_IMAGE_FMT, GetCurrentProcessId(), gen);
	if (!g_st.sec)
	{
		const uint64_t secBytes = (g_st.total + 0xFFFF) & ~0xFFFFull;
		g_st.sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, (DWORD)(secBytes >> 32), (DWORD)secBytes, name);
		DWORD err = GetLastError();
		if (!g_st.sec || err == ERROR_ALREADY_EXISTS)
		{
			_snprintf_s(g_why, _TRUNCATE, "CreateFileMapping('%ls', %llu bytes) failed, error %lu", name, (unsigned long long)secBytes, err);
			DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0;
			Fail(VPDTAP_FAIL_SECTION, g_why, false);
			WindowEnd("failed", g_generation);
			return false;
		}
	}
	while (g_st.pos < g_st.total)
	{
		const uint64_t nb = g_st.total - g_st.pos < VIEW ? g_st.total - g_st.pos : VIEW;
		if (!Fits(t0, g_cSec, (double)nb)) return false;
		const int64_t s0 = Qpc();
		void* v = MapViewOfFile(g_st.sec, FILE_MAP_WRITE, (DWORD)(g_st.pos >> 32), (DWORD)g_st.pos, (SIZE_T)nb);
		if (!v)
		{
			_snprintf_s(g_why, _TRUNCATE, "MapViewOfFile at %llu failed, error %lu", (unsigned long long)g_st.pos, GetLastError());
			DropStage(); g_buildRestarts = 0; g_walkHoldRun = 0;
			Fail(VPDTAP_FAIL_SECTION, g_why, false);
			WindowEnd("failed", g_generation);
			return false;
		}
		memcpy(v, g_st.img + g_st.pos, (size_t)nb);
		UnmapViewOfFile(v);
		g_st.pos += nb;
		Learn(g_cSec, s0, (double)nb);
	}
	// the new generation replaces the old one
	free(g_st.img); g_st.img = nullptr;
	if (g_secPrev) CloseHandle(g_secPrev);
	g_secPrev = g_secCur; g_secCur = g_st.sec; g_st.sec = nullptr; g_secCurGen = gen;
	CloseSections(false);
	free(g_pub); g_pub = g_st.set; g_st.set = nullptr; g_nPub = g_st.n;
	free(g_areaPub); g_areaPub = g_st.area; g_st.area = nullptr; g_areaPubX = g_st.areaX; g_areaPubY = g_st.areaY; g_areaPubSet = g_st.areaSet;
	free(g_walkPub); g_walkPub = g_st.walk; g_st.walk = nullptr; g_walkPubBytes = g_walkPub ? g_st.walkBytes : 0;
	g_walkPubSmall = g_walkPub ? g_st.walkSmall : 0; g_walkPubLarge = g_walkPub ? g_st.walkLarge : 0;
	g_generation = gen; g_publishCount++;
	g_imageBytes = g_st.total; g_fnv = g_st.fnv; g_changedSince = g_st.baseGen; g_lastAdded = g_st.added; g_lastRemoved = g_st.removed;
	wcsncpy_s(g_imageName, name, _TRUNCATE);
	g_readyQpc = Qpc();
	g_st.cpuMs += (float)Ms(g_readyQpc - g_advQpc);
	g_copyMs = (float)(g_readCpuMs + g_st.cpuMs);
	g_readCpuMs = 0;
	SetState(VPDTAP_READY, VPDTAP_FAIL_NONE);
	DLOG("decal_tap: READY generation %u (load %u, %s): %u decals, %u names, %u vertices, %u changed rectangles (+%u -%u since generation %u), "
		"area %u x %u tiles, %u with +0x300; walk %s %u small + %u large; "
		"image %llu bytes in '%ls', fnv1a64 %016llX; composite CPU %.3f ms (build %.3f ms in %u steps), %u Presents, wall %.1f ms",
		gen, g_loadSeq, g_st.why, g_st.n, g_st.nNames, g_st.nv, g_st.nChanged, g_st.added, g_st.removed, g_st.baseGen,
		g_areaPubX, g_areaPubY, g_areaPubSet, g_walkPub ? "block" : "none", g_walkPubSmall, g_walkPubLarge,
		(unsigned long long)g_st.total, name, (unsigned long long)g_st.fnv, g_copyMs, g_st.buildMs, g_st.steps, g_st.presents,
		Ms(g_readyQpc - g_st.startQpc));
	{
		// 0649: one line per set that left records out (OutsideX), with the first one's rectangle
		uint32_t nOut = 0, firstB = DT_NIL;
		for (uint32_t b = 0; b < g_st.nb; ++b) { nOut += g_b[b].outN; if (g_b[b].outN && firstB == DT_NIL) firstB = b; }
		if (nOut)
		{
			const Batch& F = g_b[firstB];
			DLOG("decal_tap: generation %u leaves out %u decal record(s) outside the world's x range %g .. %g (the EXE's draw never "
				"takes them); the first: batch %u slot %u, rectangle (%g, %g, %g, %g)", gen, nOut, F32(g_hdr, 0x6C), F32(g_hdr, 0x74),
				firstB, F.outSlots[0], F.outFirst[0], F.outFirst[1], F.outFirst[2], F.outFirst[3]);
		}
	}
	WindowEnd("READY", gen);
	g_buildRestarts = 0; g_walkHoldRun = 0;
	DropStage();
	return true;
}

// Runs the stages in progress within this Present's budget.
static void Advance(int64_t t0)
{
	if (g_st.stage == ST_NONE) return;
	g_st.presents++;
	g_advQpc = Qpc();
	if (g_st.stage == ST_BUILD) StepBuild(t0);
	if (g_st.stage == ST_HASH || g_st.stage == ST_SECTION) StepPublish(t0);
	if (g_st.stage != ST_NONE) g_st.cpuMs += (float)Ms(Qpc() - g_advQpc);
}

static void CheckAck()
{
	if (!g_ctl || !g_generation) return;
	const uint32_t ack = g_ctl->ackGeneration;
	if (ack == g_lastAck || ack != g_generation) return;
	MemoryBarrier();
	const int32_t res = g_ctl->ackResult;
	g_lastAck = ack;
	if (res == 0)
	{
		// the diff base becomes this generation's set
		Pub* nb = (Pub*)malloc(sizeof(Pub) * (g_nPub ? g_nPub : 1));
		if (nb) { memcpy(nb, g_pub, sizeof(Pub) * g_nPub); free(g_base); g_base = nb; g_nBase = g_nPub; g_baseGen = ack; }
		CloseSections(true);
		g_wantVersion++;
		DLOG("decal_tap: generation %u acknowledged by pid %u; its section closed", ack, g_ctl->ackServerPid);
	}
	else
	{
		g_refused++; g_wantVersion++;
		Loud("the server refused generation %u (ackResult %d); the section stays until the next generation", ack, res);
	}
}

// ---- the per-Present entry ----------------------------------------------------------------------------------------------
static void Disable(const char* why) { g_sehCount++; Fail(VPDTAP_FAIL_SEH, why, true); }

void DecalTap_OnPresent(const DecalTapEnv& env)
{
	if (!g_qpcMs) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcMs = 1000.0 / (double)f.QuadPart; }
	g_log = env.log;
	if (!g_ctl && !OpenControl()) return;
	if (!env.enabled)
	{
		if (g_st.stage != ST_NONE) DropStage();
		if (g_state != VPDTAP_OFF) { SetState(VPDTAP_OFF, VPDTAP_FAIL_NONE); DLOG("decal_tap: OFF (decal_tap=0)"); }
		if (g_pubVersion != g_wantVersion) Publish();
		return;
	}
	if (g_state == VPDTAP_OFF) SetState(g_generation ? VPDTAP_READY : VPDTAP_NONE, VPDTAP_FAIL_NONE);
	if (g_dead) { if (g_pubVersion != g_wantVersion) Publish(); return; }
	const int64_t t0 = Qpc();
	g_stepDone = false;
	if (!g_init)
	{
		g_init = true;
		ReadKeys();
		g_nameIdx = (uint16_t*)calloc(DT_MAX_HANDLE, sizeof(uint16_t));
		g_used = (bool*)calloc(DT_MAX_HANDLE, sizeof(bool));
		if (!g_nameIdx || !g_used) { Fail(VPDTAP_FAIL_LAYOUT, "out of memory at start", true); Publish(); return; }
		if (g_test == T_HUGE)
		{
			DLOG("decal_tap: TEST MODE decal_test=huge: a synthetic decal system of %u decals in %u batches in this DLL, the EXE is not read; %u changes during builds", HW_N, HW_NB, g_midBuild);
			if (!BuildHugeWorld()) { Fail(VPDTAP_FAIL_LAYOUT, "test world could not be built", true); Publish(); return; }
			A.tds = (uintptr_t)g_hw->tds; A.farm = (uintptr_t)g_hw->farm;
			static uintptr_t mgrPtrH; mgrPtrH = (uintptr_t)g_hw->mgr; A.resmgr = (uintptr_t)&mgrPtrH;
			g_patternsOk = true;
		}
		else if (g_test == T_PATTERNS)
		{
			g_patternsOk = TestPatterns();
			if (!g_patternsOk) { Fail(VPDTAP_FAIL_PATTERN, g_why, true); Publish(); return; }
			DLOG("decal_tap: test: EXE patterns match (%d) on %s in the test image (TDS at +0x%X) - nothing is read", DT_NSIGS,
				VpExeBuildLabel(g_build), (unsigned)(ShellExeVa(g_build, VPX_bake_ts) - VPEXE_IMAGE_BASE));
			g_dead = true;
			Publish();
			return;
		}
		else if (g_test)
		{
			DLOG("decal_tap: TEST MODE decal_test=%s: a synthetic decal system in this DLL, the EXE is not read", kTestNames[g_test]);
			if (!BuildTestWorld()) { Fail(VPDTAP_FAIL_LAYOUT, "test world could not be built", true); Publish(); return; }
			A.tds = (uintptr_t)g_tw->tds; A.farm = (uintptr_t)g_tw->farm;
			static uintptr_t mgrPtr; mgrPtr = (uintptr_t)g_tw->mgr; A.resmgr = (uintptr_t)&mgrPtr;
			g_patternsOk = true;
		}
		else
		{
			if (!env.exeValidated) { _snprintf_s(g_why, _TRUNCATE, "the composite did not validate the EXE image - the tap never reads it"); g_patternsOk = false; }
			else g_patternsOk = env.exeBase && CheckPatterns(env.exeBase);
			if (!g_patternsOk) { Fail(VPDTAP_FAIL_PATTERN, g_why, true); Publish(); return; }
			A.tds = ShellExeAddr(g_build, VPX_bake_ts, env.exeBase); A.resmgr = ShellExeAddr(g_build, VPX_decal_resmgr, env.exeBase);
			A.farm = ShellExeAddr(g_build, VPX_decal_farm_owner, env.exeBase) + DT_FARM_OFF; A.trSlot = A.tds + DT_TS_TR;
			DLOG("decal_tap: EXE patterns match (%d) on %s - tap enabled (TDS 0x%08X, resource manager 0x%08X, farm types 0x%08X)",
				DT_NSIGS, VpExeBuildLabel(g_build), (uint32_t)A.tds, (uint32_t)A.resmgr, (uint32_t)A.farm);
		}
		g_netQpc = t0;
	}
	// test: the trigger file changes the synthetic decals
	if (g_test)
	{
		wchar_t trig[MAX_PATH]; _snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.decalchange", g_dir);
		if (GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig))
		{ g_testChanges++; if (g_test == T_HUGE) HugeToggle(); else TestChange(); DLOG("decal_tap: test: decals changed (+%u)", g_testChanges); }
		_snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.areachange", g_dir);
		if (GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig)) { TestSetArea(20, 20, 1); DLOG("decal_tap: test: tile (20, 20) +0x300 = 1 (no decal changed)"); }
		// 0406: the huge world changes now and before the walk of each of the next 4 builds (the restart limit is 3)
		_snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.decalstorm", g_dir);
		if (g_test == T_HUGE && GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig))
		{ g_midBuild = 4; g_midAtArea = true; HugeToggle(); DLOG("decal_tap: test: a storm of changes (decal %u %s, 4 more before the walks)", HW_TOGGLE, g_hw->toggledOff ? "freed" : "back"); }
		_snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.decalbreak", g_dir);
		if (g_test != T_HUGE && GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig)) { g_testBroken = true; TestLayout(); DLOG("decal_tap: test: a record of batch 1 now runs past its chunk"); }
	}
	const bool mapLoaded = env.cam && env.cam->gridValid;   // the front end has no decals; a test host sets gridValid
	if (mapLoaded) g_loadSeq = env.cam->gridSeq;
	CheckAck();
	if (mapLoaded)
	{
		// POLL
		uint32_t nb = 0;
		int r = ReadHeader(&nb);
		if (r < 0) { Disable(g_why); Publish(); return; }
		if (r == 0)
		{
			if (++g_hdrTries >= DT_LAYOUT_TRIES && !(g_state == VPDTAP_FAILED && g_fail == VPDTAP_FAIL_LAYOUT)) Fail(VPDTAP_FAIL_LAYOUT, g_why, false);
		}
		else
		{
			g_hdrTries = 0;
			if (!BatchesGrow(nb > 0 ? nb : 1)) { Fail(VPDTAP_FAIL_LAYOUT, "out of memory (batches)", false); }
			else
			{
				uint64_t sig = Fnv(0xCBF29CE484222325ull, g_hdr + 0xA8, 0x18);
				sig = Fnv(sig, g_vec, (size_t)nb * DT_BATCH);
				if (!g_haveSig || sig != g_sigHash || nb != g_nb)
				{
					if (!g_firstMoveQpc) g_firstMoveQpc = t0;
					g_quiet = 0; g_sigHash = sig; g_haveSig = true;
					for (uint32_t b = 0; b < nb; ++b)
						if (b >= g_nb || !g_b[b].valid || memcmp(g_b[b].sig, g_vec + DT_BATCH * b, 0x2C)) g_b[b].moved = true;
					for (uint32_t b = nb; b < g_nb; ++b) { RawFree(g_b[b]); g_b[b].n = 0; g_b[b].valid = false; g_b[b].moved = false; g_b[b].readSeq++; }
					if (nb != g_nb) g_firstPending = true;
					g_nb = nb;
				}
				else g_quiet++;
				// the safety net
				bool net = Ms(t0 - g_netQpc) >= g_netMs;
				if (net)
				{
					g_netQpc = t0;
					g_netNext = 0;   // one batch per Present from here (no spike of every batch in one Present)
					if (g_namesFailed && !g_reading && g_st.stage == ST_NONE) { WindowBegin(); Compose("names retry"); }
				}
				if (g_netNext < g_nb)
				{
					const uint32_t b = g_netNext++;
					if (g_b[b].valid && !g_b[b].moved)
					{
						int c = NetCheck(b);
						if (c < 0) { Disable("fault in the safety net's read"); Publish(); return; }
						if (!c) { g_b[b].moved = true; if (!g_firstMoveQpc) g_firstMoveQpc = t0; DLOG("decal_tap: safety net: batch %u changed with equal counters", b); }
					}
				}
				bool any = g_firstPending || !g_generation || g_walkRetry;   // a batch count change, nothing published yet, a walk hold (0406)
				bool movedAny = false;
				for (uint32_t b = 0; b < g_nb; ++b) movedAny |= g_b[b].moved;
				any |= movedAny;
				bool settled = g_quiet >= g_settleFrames || (g_firstMoveQpc && Ms(t0 - g_firstMoveQpc) >= g_maxWaitMs);
				if (g_state == VPDTAP_FAILED && g_fail == VPDTAP_FAIL_LAYOUT) settled = settled && net;   // failing records: once per net tick
				// a build whose snapshot moved under it starts again from the read (at most DT_BUILD_RESTARTS times in a row;
				// then the snapshot, still one consistent read, is published and the moved batch read after it). A batch
				// count that fell invalidates the snapshot itself: always again.
				if (g_st.stage == ST_BUILD && (movedAny || g_nb != g_st.nb))
				{
					if (g_nb < g_st.nb || g_buildRestarts < DT_BUILD_RESTARTS)
					{
						g_buildRestarts++;
						DLOG("decal_tap: build restarted at step %d (%s; restart %u, at most %u in a row)", g_st.step,
							g_nb < g_st.nb ? "the batch count fell" : "a batch moved during the build", g_buildRestarts, DT_BUILD_RESTARTS);
						DropStage();
					}
					else if (!g_st.limitLogged)
					{
						g_st.limitLogged = true;
						DLOG("decal_tap: a batch moved during the build after %u restarts: the snapshot is published, the batch is read after it", g_buildRestarts);
					}
				}
				if (g_st.stage != ST_NONE) Advance(t0);   // a publication in progress: no read until it is done
				else if (any && settled)
				{
					// READ, under the budget: a batch only when its estimate (record pool and used vertex bytes) still fits
					WindowBegin();
					g_reading = true;
					bool layout = false, torn = false;
					for (uint32_t b = 0; b < g_nb; ++b)
					{
						if (!g_b[b].moved) continue;
						int rb;
						if (!g_b[b].rawPending)
						{
							const double units = (double)U32(g_vec + DT_BATCH * b, 0xC) * DT_REC + (double)U32(g_vec + DT_BATCH * b, 0x24) * 8;
							if (!Fits(t0, g_cRead, units)) break;
							const int64_t s0 = Qpc();
							rb = ReadBatchCopy(b);
							Learn(g_cRead, s0, units);
							if (rb == 1) rb = ReadBatchWork(b, t0);
						}
						else rb = ReadBatchWork(b, t0);
						if (rb < 0) { Disable(g_why); WindowEnd("failed", g_generation); Publish(); return; }
						if (rb == 0)
						{
							layout = true;
							if (++g_b[b].layoutTries >= DT_LAYOUT_TRIES && g_fail != VPDTAP_FAIL_LAYOUT) { Fail(VPDTAP_FAIL_LAYOUT, g_why, false); WindowEnd("failed", g_generation); }
						}
						if (rb == 2) torn = true;
						if (rb == 3) break;   // the budget is used; this batch's checks and hashes go on in the next Present
					}
					bool pending = false;
					for (uint32_t b = 0; b < g_nb; ++b) pending |= g_b[b].moved;
					g_readCpuMs += Ms(Qpc() - t0);
					if (!pending && !layout && !torn)
					{
						g_reading = false;
						Compose(g_generation ? "records changed" : "first copy");
						g_firstMoveQpc = 0; g_firstPending = false;
						Advance(t0);
					}
				}
				else if (!any) g_firstMoveQpc = 0;
				// 4111: the idle sweep of the tiles' +0x300 between builds (a terrain edit invalidates through the area invalidation
				// without a decal change); a build reads its own (B_AREA), a decal change abandons this one
				if (g_swIdle && (any || g_st.stage != ST_NONE || g_reading)) { if (g_st.stage == ST_NONE) AreaFree(); g_swIdle = false; }
				if (net && !g_swIdle && !any && g_generation && g_st.stage == ST_NONE && !g_reading)
				{
					const int r = AreaBegin();
					if (r < 0) { Disable(g_why); Publish(); return; }
					if (r == 0) Fail(VPDTAP_FAIL_LAYOUT, g_why, false);
					else g_swIdle = true;
				}
				if (g_swIdle)
				{
					const int r = AreaStep(t0);
					if (r < 0) { g_swIdle = false; AreaFree(); Disable(g_why); Publish(); return; }
					if (r == 0) { g_swIdle = false; AreaFree(); Fail(VPDTAP_FAIL_LAYOUT, g_why, false); }
					else if (r == 2) { g_swIdle = false; AreaFree(); }   // a new grid: the next tick sweeps it
					else if (r == 1)
					{
						g_swIdle = false;
						const bool failed = g_state == VPDTAP_FAILED && g_fail == VPDTAP_FAIL_LAYOUT;
						if (!AreaEqualsPublished() || failed)
						{
							uint32_t set = 0;
							for (uint32_t i = 0; i < g_sw.n; ++i) set += g_sw.bits[i];
							DLOG("decal_tap: tile flags %s (%u x %u tiles, %u with +0x300; published %u x %u, %u) - a new build",
								failed ? "read again after a layout failure" : "changed without a decal change", g_sw.tx, g_sw.ty, set,
								g_areaPubX, g_areaPubY, g_areaPubSet);
							WindowBegin();
							Compose(failed ? "tile flags read again" : "tile flags changed");
							Advance(t0);
						}
						else AreaFree();
					}
				}
			}
		}
	}
	const double ms = Ms(Qpc() - t0);
	if (g_winActive)
	{
		g_winN++; g_winSum += ms; if (ms > g_winMax) g_winMax = ms;
		if (g_winEnd)
		{
			char steps[400]; int o = 0;
			for (Cost* c : kCosts) o += _snprintf_s(steps + o, sizeof(steps) - o, _TRUNCATE, "%s%s %.3f", o ? ", " : "", c->name, c->winMax);
			DLOG("decal_tap: publication cost (%s, generation %u): %u Presents from the first read, per Present max %.3f ms, mean %.3f ms "
				"(decal_budget_ms %u), %u build restarts; largest step (ms): %s", g_winWhat, g_winGen, g_winN, g_winMax, g_winSum / g_winN,
				g_budgetMs, g_winRestarts, steps);
			g_winActive = false; g_winEnd = false;
		}
	}
	g_pollSum += ms; g_pollN++;
	if (g_pollN >= 600)
	{
		g_pollMs = (float)(g_pollSum / g_pollN);
		g_wantVersion++;
		static uint32_t windows; if ((++windows % 50) == 1 && g_log)
			g_log("decal_tap: cost over %u Presents: mean %.4f ms per Present (render thread); state %u, generation %u, %u decals",
				g_pollN, g_pollMs, g_state, g_generation, g_nPub);
		g_pollSum = 0; g_pollN = 0;
	}
	if (g_pubVersion != g_wantVersion) Publish();
}

void DecalTap_Shutdown(const char* why)
{
	(void)why;   // nothing of the tap depends on the device; the sections stay readable
}
