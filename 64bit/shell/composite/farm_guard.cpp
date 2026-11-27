// farm_guard.cpp - the farm builder guard inside shell_composite.dll (ticket 0527; contract and behaviour: farm_guard.h).
// Written 2026-11-18 for this repository (GPL-3.0-or-later). What the EXE's objects are and why each byte pattern below is
// checked: research/02-exe-internals/farm-builder-guard.md (behaviour, D83; nothing of the EXE is in this file but the
// patterns' few bytes, as in decal_tap.cpp and cut_tap.cpp).
//
// Threads: FarmGuard_OnPresent and FgHook both run on the EXE's main thread (the Present and the queue-B pump are parts of
// one frame function, lockstep-probe.md); the counters are plain. The listener list is changed by the EXE only under its
// registration lock; the Present takes that lock (one try: a held lock means this Present does nothing) before it reads
// or writes a node. FgHook reads the list without the lock (bounded and SEH-guarded); a read that fails counts as "no
// stale node" and the builder is called as the EXE would have. The builder itself is never called inside a __try: a
// fault in the EXE's own code stays the EXE's (the proxy's crash filter, D118/D119).
#include "farm_guard.h"
#include <math.h>
#ifndef INFINITY   // ticket 0535: the proxy compiles this file against VC9's headers, which lack C99's INFINITY
#define INFINITY ((float)HUGE_VAL)
#endif
#include "shell_exe.h"   // the EXE build table (epic 0590, ticket 4965; needs exebuild/include and the emitted header)
#include "vp_farm_points.h"   // ticket 0657: the server's farm points (shell/common)
#include <share.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- EXE layout (DX11; addresses from the build table, epic 0590: shell_exe.h; farm-builder-guard.md 1-3) --------------
// fg_lockinit: bit 0 says the registration lock word below was set to 0 once; fg_lock: the registration lock of the "B"
// manager's listener lists (type 59 among them); pac_qb_index: the "B" event manager, [mgr] = channel index;
// fg_type_handler: every type's handler, a jmp to the listener walk; fg_builder: the farm-field builder; fg_builder_obj: its
// object, the layout graph at +FG_GRAPH_OFF; fg_viewflag: 1 from the city-field init to the GameView shutdown; fg_vtxvec
// (ticket 4854): the farm-track strip's vertex vector (begin; +4 size, +8 capacity); cam_h2w (ticket 4855): the hex ->
// world matrix (farm-fields-layout.md 1.3: the plot's rectangle); cam_aabb: the world box's minimum x, y.
#define FG_SLOT_OFF 0x1E0u           // mgr + 8 + 59 * 8: type 59's handler; +4 its listener list
#define FG_STRIDE 0x600u             // one channel's handler table
#define FG_GRAPH_OFF 0x158u          // the layout graph in the builder's object
#define FG_EP_HDR 0x94u              // endpoint free-list array: head, live, data, size, capacity
#define FG_ND_HDR 0xBCu              // node free-list array: the same five words
#define FG_G_NX 0x84u                // the grid: cells in x (columns), in y (rows), the cell array (farm-field-double-free.md 2.2)
#define FG_G_NY 0x88u
#define FG_G_CELLS 0x8Cu
#define FG_CELL_BYTES 24u            // +0x04 count, +0x08 first node, +0x0C last node
#define FG_NODE_BYTES 56u            // +0x10 points, +0x14 / +0x16 endpoint ids, +0x28 next, +0x2C previous (bit 31 freed),
                                     // +0x34 cell id (16 bits), +0x36 kind
#define FG_EP_BYTES 20u
#define FG_CELL_NONE 0xFFFFu         // the cell whose 16-bit id reads as "none" (farm-field-double-free.md 2)
#define FG_R2_MARGIN 0.66f           // rule 2's margin beyond the plot's rectangle (farm-fields-layout.md 15)
#define FG_ROW_SY 0.866f             // the rectangle's y scale (farm-fields-layout.md 1.3)
#define FG_NIL 0x0FFFFFFFu
#define FG_SANE_SIZE 4000000u
#define FG_SANE_LIST 4096u
#define FG_MAX_CHANNEL 16u
#define FG_QUIET_MS 2000.0           // a burst of builder calls ends after this long without one: its "load" line
#define FG_INGAME_MS 10000.0         // a load's in-game line (the check at a Present) this long after its map came up

// Byte patterns checked before the first read of the real EXE (R(name) = 4 bytes holding the symbol's address in the
// running build; gen_table.py checks every row on both builds, exebuild/patterns.txt). Function names are the research's
// (farm-builder-guard.md); the addresses are the build table's.
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFgSigs[] = {
	X(VPX_fg_startup_group, "GameView Startup: the listener group on the builder's object", "B9 R(fg_builder_obj) E8 8A 23 E4 FF")
	X(VPX_fg_listener_reg, "the listener group: listener {object, builder} given to the type-59 register", "C7 44 24 10 R(fg_builder) 89 74 24 0C E8 84 22 3E 00")
	X(VPX_fg_register, "the type-59 register: lock, list [lists + [mgr] * 0x600], add, unlock", "F6 05 R(fg_lockinit) 01 75 11 83 0D R(fg_lockinit) 01 C7 05 R(fg_lock) 00 00 00 00 56 8B 35 R(fg_iat_interlockedcompareexchange) 6A 00 6A 01 68 R(fg_lock) FF D6 85 C0 75 F1 8B 0D R(pac_qb_index) 8B 44 24 08 69 C9 00 06 00 00 8B 89 R(fg_lists) 50 E8 74 9E 01 00 C7 05 R(fg_lock) 00 00 00 00")
	X(VPX_fg_list_add, "list add: count +0x14, head +0x18, tail +0x1C", "FF 46 14 81 7E 18 FF FF FF 0F 8B F8 8B 46 1C 89 7E 1C 75 03 89 7E 18")
	X(VPX_fg_type_handler, "type handler: jmp to the listener walk", "E9 AB 83 22 00")
	X(VPX_fg_listener_walk, "listener walk: count +0x14, head +0x18, 20-byte nodes at [+0x08], object +0x0C, function +0x10 called with one argument, next +0x00", "83 7B 14 00 57 8B 7B 18 C7 44 24 10 00 00 00 00 76 45 55 8B 6C 24 10 56 8D 49 00 8B 43 08 8D 34 BF 03 F6 03 F6 8B 4C 06 0C 8B 54 06 10 8D 44 06 0C 55 FF D2 81 FF FF FF FF 0F 75 05 8B 7B 18 EB 06 8B 43 08 8B 3C 06")
	X(VPX_fg_builder, "farm builder: this in ecx, its argument at [esp+4]", "83 EC 20 8B 44 24 24 53 55 56 0F BF 70 1A 57 0F BF 78 18")
	X(VPX_fg_builder_layout, "farm builder: the layout call on this+0x154", "8D 8D 54 01 00 00 50 89 4C 24 38 E8 3D 86 EB FF")
	X(VPX_fg_builder_ret, "farm builder: its one return, ret 4", "5F 5E 5D 5B 83 C4 20 C2 04 00")
	X(VPX_fg_cityinit_flag, "city-field init: the flag = 1", "C7 05 R(fg_viewflag) 01 00 00 00")
	X(VPX_fg_shutdown_flag, "GameView shutdown: unregister the group, then the flag = 0", "E8 37 99 26 00 33 FF 39 3D R(fg_viewflag) 74 30")
	X(VPX_fg_node_delete, "node delete: frees endpoints on this+0x94 when a count (low 31 bits of +0x10) is 0", "8B 6D 10 8B 44 24 10 81 E5 FF FF FF 7F F7 40 10 FF FF FF 7F 75 10 0F B7 4E 16 51 8D 8F 94 00 00 00 E8 98 E0 E5 FF")
	X(VPX_fg_freelist_free, "free-list free: data +0x08, 20-byte entries, bit 31 of +0x10 = freed, head +0x00", "8B 51 08 56 57 8B 7C 24 0C 8D 04 BF 81 4C 82 10 00 00 00 80 8B 74 82 10 8D 14 82 8B 01 3B F8 73 16")
};
#undef X
#define FG_NSIGS ((int)(sizeof(kFgSigs) / sizeof(kFgSigs[0])))

// Ticket 0655: the farm-end post's patterns (cities-and-landmarks.md 2.6.1; farm-builder-guard.md 8). The builder's tests
// (cube coordinates on its stack, the flag byte, the plot test on its object, the art id of its name on its object, the
// compare) and its last step (the queue object from the channel index, the poster, the hex x and y into the payload, the
// return) are checked with every call target, so the post does exactly what the builder's own code does in this build.
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFgPostSigs[] = {
	X(VPX_fg_builder_tests, "farm builder: the cube, the flag byte, the plot test, the farm's art id", "8D 1C 37 F7 DB 80 78 1C 00 8B E9 89 7C 24 14 89 74 24 18 89 5C 24 1C 0F 84 ?? ?? ?? ?? 8D 44 24 14 50 B9 R(fg_plot_grid) E8 C(fg_plot_on_map) 84 C0 0F 84 ?? ?? ?? ?? 8B 4C 24 34 8B 51 08 68 R(fg_farm_art_name) B9 R(fg_art_db) 89 54 24 14 E8 C(fg_art_id) 8B 4C 24 10 3B C8 0F 85 ?? ?? ?? ?? 83 F9 FF 0F 84")
	X(VPX_fg_builder_post, "farm builder: the farm-end post on the queue object, hex x and y into the payload, ret 4", "8B 0D R(qg_channel_index) 69 C9 80 01 80 00 81 C1 R(qg_channels) E8 C(fg_post_farm_end) 66 89 38 66 89 70 02 5F 5E 5D 5B 83 C4 20 C2 04 00")
	X(VPX_fg_post_farm_end, "the farm-end poster: 0x80 bytes reserved by the queue's interlocked add, type 5, the payload returned", "56 8B B1 00 01 80 00 83 E6 01 69 F6 80 00 40 00 03 F1 68 80 00 00 00 56 FF 15 R(qg_iat_interlockedexchangeadd) 8D 84 30 80 00 00 00 33 C9 5E 3B C1 74 13 C7 00 80 00 00 00 C7 40 04 05 00 00 00 89 48 08 89 48 0C 83 C0 10 3B C1 74 0C 33 C9 33 D2 66 89 08 66 89 50 02 C3")
	X(VPX_fg_plot_on_map, "the plot test: reads the three cube coordinates of its one argument", "83 EC 18 8B 44 24 1C 8B 50 04 56 8B F1 8B 08 8B 40 08")
	X(VPX_fg_farm_art_name, "the farm's art name", "41 52 54 5F 44 45 46 5F 49 4D 50 52 4F 56 45 4D 45 4E 54 5F 46 41 52 4D 00")
};
#undef X
#define FG_NPOSTSIGS ((int)(sizeof(kFgPostSigs) / sizeof(kFgPostSigs[0])))
static const int kFgPostNeed[] = { VPX_fg_post_farm_end, VPX_qg_channel_index, VPX_qg_channels, VPX_fg_plot_on_map,
	VPX_fg_plot_grid, VPX_fg_art_id, VPX_fg_art_db, VPX_fg_farm_art_name, VPX_fg_builder_tests, VPX_fg_builder_post,
	VPX_qg_iat_interlockedexchangeadd };
#define FG_NPOSTNEED ((int)(sizeof(kFgPostNeed) / sizeof(kFgPostNeed[0])))

// Ticket 0655 (P1): the diagnostic area probe's patterns: the A manager's type-33 register (its own lock, the channel index,
// the list), the group's call of it with {object, area listener}, the listener's head (its argument, this + 0x154).
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFgAreaSigs[] = {
	X(VPX_fg_a_register, "the type-33 register of the A manager: lock, list [lists + [index] * 0x600], add, unlock", "F6 05 R(fg_a_lockinit) 01 75 11 83 0D R(fg_a_lockinit) 01 C7 05 R(fg_a_lock) 00 00 00 00 56 8B 35 R(fg_iat_interlockedcompareexchange) 6A 00 6A 01 68 R(fg_a_lock) FF D6 85 C0 75 F1 8B 0D R(qg_channel_index) 8B 44 24 08 69 C9 00 06 00 00 8B 89 R(fg_a_lists) 50 E8 B4 9D 01 00 C7 05 R(fg_a_lock) 00 00 00 00")
	X(VPX_fg_area_reg, "the listener group: {object, area listener} given to the type-33 register", "50 C7 44 24 1C R(fg_area_listener) 89 74 24 18 E8 C(fg_a_register) 83 C4 10 5E 83 C4 0C C3")
	X(VPX_fg_area_listener, "the area listener: its argument's four floats, this + 0x154", "8B 54 24 04 D9 02 56 83 EC 10 8B C4 D9 18 8D B1 54 01 00 00")
};
#undef X
#define FG_NAREASIGS ((int)(sizeof(kFgAreaSigs) / sizeof(kFgAreaSigs[0])))
static const int kFgAreaNeed[] = { VPX_fg_area_listener, VPX_fg_area_reg, VPX_fg_a_register, VPX_fg_a_lockinit, VPX_fg_a_lock,
	VPX_fg_a_lists, VPX_qg_channel_index, VPX_fg_type_handler, VPX_fg_builder_obj, VPX_fg_iat_interlockedcompareexchange };
#define FG_NAREANEED ((int)(sizeof(kFgAreaNeed) / sizeof(kFgAreaNeed[0])))

// Ticket 0657: the edge placement's patterns (cities-and-landmarks.md 2.6.2, 2.6.3): the farm manager's listener group
// registering the era listener on B48 and the farm-end listener on A5 with the manager itself as the object; the two
// registers (each manager's own lock, channel index and list slot); the two listeners' heads (the deferral bit +0x207C0
// that re-queues their own address, the pending list +0x458/+0x45C and the records +0x207A4/+0x207A8 of 0x90 bytes) and their
// calls of the placer; the placer's head and its curve loop (record +5 count, +0x20 ids, manager +0x6F4 the table of
// 0x2C-byte entries, +0x10 the first segment, +8 the point store whose +8 is the 0xFC-byte segment array, the centroid call,
// F against the threshold) and the three curve functions' heads (the segment links +4 and the count +8).
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFgEdgeSigs[] = {
	X(VPX_fg_farm_group, "the farm manager's listener group: {manager, era listener} to the B48 register, {manager, farm-end listener} to the A5 register", "8D 44 24 08 50 C7 44 24 14 R(fg_farm_era_listener) 89 74 24 10 E8 C(fg_b48_register) 8D 4C 24 0C 51 C7 44 24 18 ?? ?? ?? ?? 89 74 24 14 E8 ?? ?? ?? ?? 8D 54 24 10 52 C7 44 24 1C R(fg_farm_end_listener) 89 74 24 18 E8 C(fg_a5_register)")
	X(VPX_fg_a5_register, "the type-5 register of the A manager: lock, list [lists + [index] * 0x600], add, unlock", "F6 05 R(fg_a_lockinit) 01 75 11 83 0D R(fg_a_lockinit) 01 C7 05 R(fg_a_lock) 00 00 00 00 56 8B 35 R(fg_iat_interlockedcompareexchange) 6A 00 6A 01 68 R(fg_a_lock) FF D6 85 C0 75 F1 8B 0D R(qg_channel_index) 8B 44 24 08 69 C9 00 06 00 00 8B 89 R(fg_a5_lists) 50 E8 ?? ?? ?? ?? C7 05 R(fg_a_lock) 00 00 00 00")
	X(VPX_fg_b48_register, "the type-48 register of the B manager: lock, list [lists + [index] * 0x600], add, unlock", "F6 05 R(fg_lockinit) 01 75 11 83 0D R(fg_lockinit) 01 C7 05 R(fg_lock) 00 00 00 00 56 8B 35 R(fg_iat_interlockedcompareexchange) 6A 00 6A 01 68 R(fg_lock) FF D6 85 C0 75 F1 8B 0D R(pac_qb_index) 8B 44 24 08 69 C9 00 06 00 00 8B 89 R(fg_b48_lists) 50 E8 ?? ?? ?? ?? C7 05 R(fg_lock) 00 00 00 00")
	X(VPX_fg_farm_end_listener, "the farm-end listener: the deferral bit re-queues its own address; the pending list and the records", "81 EC 6C 04 00 00 53 55 8B E9 F6 85 C0 07 02 00 01 56 57 74 33 8B 8C 24 80 04 00 00 6A 00 8D 44 24 14 50 51 8D 8D B0 07 02 00 C7 44 24 20 R(fg_farm_end_listener) 89 6C 24 1C E8 ?? ?? ?? ?? 5F 5E 5D 5B 81 C4 6C 04 00 00 C2 04 00 33 DB 39 9D 5C 04 00 00 0F 86 ?? ?? ?? ?? EB 08 8D A4 24 00 00 00 00 90 8B 95 58 04 00 00 8B 04 9A 3B 85 A8 07 02 00")
	X(VPX_fg_farm_end_place, "the farm-end listener: its call of the placer", "E8 C(fg_farm_placer) 39 BC 24 78 04 00 00 74")
	X(VPX_fg_farm_era_listener, "the era listener: the deferral bit re-queues its own address", "81 EC 64 04 00 00 57 8B F9 F6 87 C0 07 02 00 01 74 30 8B 8C 24 6C 04 00 00 6A 00 8D 44 24 08 50 51 8D 8F B0 07 02 00 C7 44 24 14 R(fg_farm_era_listener) 89 7C 24 10 E8 ?? ?? ?? ?? 5F 81 C4 64 04 00 00 C2 04 00")
	X(VPX_fg_farm_era_place, "the era listener: its call of the placer", "E8 C(fg_farm_placer) 83 BC 24 70 04 00 00 00 74")
	X(VPX_fg_farm_placer, "the farm placer's head: the record's row, state and flags into the instance", "55 8B EC 83 E4 F0 81 EC A4 00 00 00 53 56 57 8B F9 89 7C 24 20 8B 5D 08 8B 03 8B 75 0C 89 06 C7 46 04 FF FF FF FF 8B 4B 0C 83 E1 0F 89 4E 0C 8A 43 04 8A 56 08 02 C0 02 C0 80 E2 C3 0A C2 88 46 08 8A 43 06 8A 4E 08 32 C8 80 E1 03 30 4E 08")
	X(VPX_fg_farm_placer_curves, "the farm placer's curve loop: record +5, manager +0x6F4, record +0x20, 0x2C per curve, +0x10, +8, 0xFC per segment, the centroid", "80 7B 05 00 F3 0F 10 84 24 A0 00 00 00 F3 0F 11 44 24 18 F3 0F 10 84 24 A4 00 00 00 F3 0F 11 44 24 1C F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 4C 0F 86 ?? ?? ?? ?? 0F B6 43 05 8B 8F F4 06 00 00 8D 53 20 89 4C 24 40 89 54 24 14 89 44 24 48 8B 4C 24 14 0F B7 11 8B 44 24 40 6B D2 2C 8B 7C 02 10 8D 74 02 08 81 FF FF FF FF 0F 75 0A 33 D2 8B CF 89 54 24 30 EB 39 8B C7 C1 E0 08 3D FF FF FF 0F 75 0A 33 D2 8B C8 89 54 24 30 EB 23 0F B6 D0 8B C8 8B 06 8B 40 08 89 54 24 30 C1 E9 08 8B D1 69 D2 FC 00 00 00 8B 54 10 08 81 E2 FF FF FF 7F 83 EC 10 8B C4 89 30 33 DB 89 58 04 BB FF FF FF 0F 89 58 08 33 DB 89 58 0C 83 EC 10 8B C4 8B DE 89 18 89 50 04 89 48 08 8B 4C 24 50 8D 54 24 54 52 89 48 0C E8 C(fg_curve_centroid)")
	X(VPX_fg_farm_placer_f, "the farm placer: F of the curve against the threshold", "E8 C(fg_curve_f) D9 E1 D9 05 R(fg_placer_min_f) 83 C4 20 DF F1 DD D8 77")
	X(VPX_fg_curve_centroid, "the curve centroid's head", "83 EC 20 8B 44 24 38 8B 4C 24 3C 0F 57 C0 8B 54 24 40 53 55 56 8B 74 24 30 89 44 24 1C 8B 44 24 50 89 4C 24 20 57 8D 4C 24 20 F3 0F 11 44 24 10")
	X(VPX_fg_curve_prev, "the curve iterator's step back: the last segment from the store at +0xC, 0xFC per segment, the count +8", "8B C1 8B 48 0C 85 C9 77 2B 8B 48 08 8B 10 81 F9 FF FF FF 0F 75 23 8B 4A 0C 89 48 08 69 C9 FC 00 00 00 8B 12 8B 52 08 8B 4C 0A 08 81 E1 FF FF FF")
	X(VPX_fg_curve_f, "the curve's F head", "83 EC 24 8B 44 24 38 8B 4C 24 3C 8B 54 24 40 0F 57 C0 53 55 56 89 44 24 20 8B 44 24 50 89 4C 24 24 57 8D 4C 24 24 F3 0F 11 44 24 10 89 54 24 2C")
};
#undef X
#define FG_NEDGESIGS ((int)(sizeof(kFgEdgeSigs) / sizeof(kFgEdgeSigs[0])))
static const int kFgEdgeNeed[] = { VPX_fg_farm_group, VPX_fg_a5_register, VPX_fg_b48_register, VPX_fg_farm_end_listener,
	VPX_fg_farm_end_place, VPX_fg_farm_era_listener, VPX_fg_farm_era_place, VPX_fg_farm_placer, VPX_fg_farm_placer_curves,
	VPX_fg_farm_placer_f, VPX_fg_curve_centroid, VPX_fg_curve_prev, VPX_fg_curve_f, VPX_fg_a5_lists, VPX_fg_b48_lists,
	VPX_fg_placer_min_f, VPX_fg_a_lockinit, VPX_fg_a_lock, VPX_qg_channel_index, VPX_fg_lockinit, VPX_fg_lock, VPX_pac_qb_index,
	VPX_fg_type_handler, VPX_fg_iat_interlockedcompareexchange };
#define FG_NEDGENEED ((int)(sizeof(kFgEdgeNeed) / sizeof(kFgEdgeNeed[0])))

// Ticket 0784 (D268): the tread filter's patterns (farm-builder-guard.md 15): the engine's start-up handing the main
// Particle object and its effect system to the Particle start-up; that start-up's head (it calls the listener group with the
// same object); the group's type-0x28 registration {object, listener} with its call of the register (the register has one
// caller on both builds; the group's other registrations differ between them); the A manager's type-0x28 register (its
// own lock and lock-init byte, the channel index, the list slot index + 0x14C); the listener's head (its one argument, the
// busy bit that re-queues its own address, ret 4, the payload's hash +0x50, scale +0x54, matrix and key +0x40..+0x4C).
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFgTreadSigs[] = {
	X(VPX_tf_particle_start, "the engine's start-up: the main Particle object and its effect system to the Particle start-up", "8D 54 24 34 52 B9 R(tf_particle_obj) C7 44 24 38 ?? ?? ?? ?? E8 C(tf_particle_startup)")
	X(VPX_tf_particle_startup, "the Particle start-up: the effect system into +0x9C, then the listener group on the same object", "8B 44 24 04 56 8B F1 8B 08 89 8E 9C 00 00 00 8B CE E8")
	X(VPX_tf_l28_reg, "the Particle object's listener group: {object, type-0x28 listener} given to the type-0x28 register", "C7 44 24 ?? R(tf_l28_listener) 89 74 24 ?? E8 C(tf_l28_register)")
	X(VPX_tf_l28_register, "the type-0x28 register of the A manager: lock, list [lists + [index] * 0x600], add, unlock", "F6 05 R(fg_a_lockinit) 01 75 11 83 0D R(fg_a_lockinit) 01 C7 05 R(fg_a_lock) 00 00 00 00 56 8B 35 R(fg_iat_interlockedcompareexchange) 6A 00 6A 01 68 R(fg_a_lock) FF D6 85 C0 75 F1 8B 0D R(qg_channel_index) 8B 44 24 08 69 C9 00 06 00 00 8B 89 R(tf_a28_lists) 50 E8 ?? ?? ?? ?? C7 05 R(fg_a_lock) 00 00 00 00 5E C3")
	X(VPX_tf_l28_listener, "the type-0x28 listener: one argument, the busy bit re-queues its own address, ret 4; the payload's hash +0x50, scale +0x54, matrix, key +0x40..+0x4C", "83 EC 58 55 8B E9 F6 85 98 00 00 00 01 56 57 74 2C 8B 4C 24 68 6A 00 8D 44 24 10 50 51 8D 8D 88 00 00 00 C7 44 24 1C R(tf_l28_listener) 89 6C 24 18 E8 ?? ?? ?? ?? 5F 5E 5D 83 C4 58 C2 04 00 8D 4C 24 1C E8 ?? ?? ?? ?? 8B 44 24 68 8B 50 50 F3 0F 10 40 54 8B F0 B9 10 00 00 00 8D 7C 24 1C F3 A5 8B 48 4C 51 8B 48 44 89 54 24 64 8B 50 48 52 8B 50 40 51 52")
};
#undef X
#define FG_NTREADSIGS ((int)(sizeof(kFgTreadSigs) / sizeof(kFgTreadSigs[0])))
static const int kFgTreadNeed[] = { VPX_tf_particle_start, VPX_tf_particle_startup, VPX_tf_particle_obj, VPX_tf_l28_reg,
	VPX_tf_l28_register, VPX_tf_l28_listener, VPX_tf_a28_lists, VPX_fg_a_lockinit, VPX_fg_a_lock, VPX_qg_channel_index,
	VPX_fg_type_handler, VPX_fg_iat_interlockedcompareexchange };
#define FG_NTREADNEED ((int)(sizeof(kFgTreadNeed) / sizeof(kFgTreadNeed[0])))
// every symbol FgWorldFromBase reads (the pattern sites are checked by the patterns)
static const int kFgNeed[] = { VPX_fg_lockinit, VPX_fg_lock, VPX_pac_qb_index, VPX_fg_type_handler, VPX_fg_builder,
	VPX_fg_builder_obj, VPX_fg_viewflag, VPX_fg_vtxvec, VPX_cam_h2w, VPX_cam_aabb };
#define FG_NNEED ((int)(sizeof(kFgNeed) / sizeof(kFgNeed[0])))

// ---- state (the EXE's main thread) -------------------------------------------------------------------------------------
static FgWorld g_w;
static bool g_ready, g_dead, g_test, g_tripped;
static FgListenerFn g_builder;                 // the builder's address as a function
static void (*g_log)(const char* fmt, ...);
static FgStats g_st;
static wchar_t g_recordDir[MAX_PATH];         // "" = not chosen yet
static bool g_recordDirChosen, g_recordInRun;
static LARGE_INTEGER g_freq;
// the burst of builder calls in progress (its "load" line)
static uint32_t g_bCalls, g_bChecks, g_bBad, g_bSkipped;
static double g_bCheckMs, g_bCheckUsMax;
static int64_t g_lastCallQpc;
static FgCheck g_bLast;
static uint32_t g_gridSeq, g_loads;
// the in-game line of every load (a map shown): its grid seq, when it became valid, the one it was logged for
static uint32_t g_seqSeen, g_seqDone, g_callsAtInGame;
static int64_t g_seqQpc;
static double g_quietMs = FG_QUIET_MS, g_inGameMs = FG_INGAME_MS;
static bool g_validLogged, g_disabledLogged;
// ticket 4853: farm_builder=0 (FarmGuardEnv::skipBuilder, taken at every Present; the hook runs on the same thread)
static bool g_skipBuilder;
static int g_skipLogged = -1;                 // the skipBuilder value last logged (-1: none yet)
static uint32_t g_bServer;                    // calls of the burst not passed to the builder for farm_builder=0
// ticket 0655: the farm-end post (its sites matched: g_postReady; the test switch farm_end_post=0: g_postOff)
static bool g_postReady, g_postOff;
static int g_postOffLogged = -1;
static uint32_t g_bPosts, g_bPostFail, g_bPostRefused, g_bPostOff;   // of the burst
static uint32_t g_bEndPlaced, g_bEndCentre, g_bEndHeld, g_bEndExpired, g_bEraCurves;   // ticket 0657: of the burst
static uint32_t g_postNullLogged;
static uint32_t g_lastScanBadLog;
// ticket 0535: the role in the process and the node words
static int g_role;                            // 0 alone (0527's form), 1 owner (the proxy), 2 follower (a composite beside an owner)
static FgShared* g_shared;                    // the owner's page (owner and follower)
static bool g_followChecked, g_followLogged;
static uintptr_t g_hookAddr, g_inertAddr;     // 0 = FgHook / FgInert (set once the functions are declared, below)
static char g_ownerName[16] = "composite";
static volatile LONG g_swapGen;               // swaps made (the watcher thread); rule 2 recomputes its radius when it moves
static int64_t g_lastFwdQpc;                  // the owner: the last Present a composite handed over (0 = none yet)
static int64_t g_lastBuiltQpc;                // the owner without a composite: the last builder call (the burst's first gets the check)
static CRITICAL_SECTION g_jobsLock;           // the owner: the watcher's tick and a composite's Present never run their jobs at once
static bool g_jobsLockReady;

static void L(const char* fmt, ...)
{
	if (!g_log) return;
	char b[2000];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, ap); va_end(ap);
	g_log("%s", b);
}

static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double QpcMs(int64_t d) { return g_freq.QuadPart ? (double)d * 1000.0 / (double)g_freq.QuadPart : 0.0; }

// ---- addresses and patterns ----------------------------------------------------------------------------------------------
bool FgWorldFromBase(int build, uint32_t base, FgWorld* w)
{
	for (int i = 0; i < FG_NNEED; ++i)
		if (!ShellExeVa(build, kFgNeed[i])) return false;
	w->lockInit = ShellExeAddr(build, VPX_fg_lockinit, base);
	w->lock = ShellExeAddr(build, VPX_fg_lock, base);
	w->mgr = ShellExeAddr(build, VPX_pac_qb_index, base);
	w->slotOff = FG_SLOT_OFF;
	w->stride = FG_STRIDE;
	w->trampoline = ShellExeAddr(build, VPX_fg_type_handler, base);
	w->builder = ShellExeAddr(build, VPX_fg_builder, base);
	w->builderObj = ShellExeAddr(build, VPX_fg_builder_obj, base);
	w->viewFlag = ShellExeAddr(build, VPX_fg_viewflag, base);
	w->graph = w->builderObj + FG_GRAPH_OFF;
	w->vtxVec = ShellExeAddr(build, VPX_fg_vtxvec, base);
	w->h2w = ShellExeAddr(build, VPX_cam_h2w, base);
	w->aabbMin = ShellExeAddr(build, VPX_cam_aabb, base);
	return true;
}

static bool SafeCopy(uintptr_t src, void* dst, size_t n)
{
	__try { memcpy(dst, (const void*)src, n); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int FgPatternCount() { return FG_NSIGS; }
int FgPatternSymbol(int k) { return k >= 0 && k < FG_NSIGS ? kFgSigs[k].sym : -1; }

bool FgCheckPatterns(uint32_t base, char* why, size_t whyCap, int* buildOut)
{
	const int build = ShellExeResolve(base, kFgNeed, FG_NNEED, why, whyCap);
	if (buildOut) *buildOut = build;
	return build >= 0 && ShellExeCheck(build, base, kFgSigs, FG_NSIGS, why, whyCap);
}

uint32_t FgImageBytes(int build) { return ShellExeSpan(build, kFgSigs, FG_NSIGS); }

bool FgWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken)
{
	return ShellExeLay(build, base, imageBytes, kFgSigs, FG_NSIGS, broken);
}

// ---- ticket 0655: the farm-end post's sites -------------------------------------------------------------------------------
int FgPostPatternCount() { return FG_NPOSTSIGS; }

bool FgPostCheckPatterns(uint32_t base, int build, char* why, size_t whyCap)
{
	for (int i = 0; i < FG_NPOSTNEED; ++i)
		if (!ShellExeVa(build, kFgPostNeed[i]))
		{
			_snprintf_s(why, whyCap, _TRUNCATE, "symbol %s is not carried to %s", VpExeSymName(kFgPostNeed[i]), VpExeBuildLabel(build));
			return false;
		}
	return ShellExeCheck(build, base, kFgPostSigs, FG_NPOSTSIGS, why, whyCap);
}

bool FgPostWorldFromBase(int build, uint32_t base, FgWorld* w)
{
	for (int i = 0; i < FG_NPOSTNEED; ++i)
		if (!ShellExeVa(build, kFgPostNeed[i])) return false;
	w->poster = ShellExeAddr(build, VPX_fg_post_farm_end, base);
	w->queueIndex = ShellExeAddr(build, VPX_qg_channel_index, base);
	w->queueBase = ShellExeAddr(build, VPX_qg_channels, base);
	w->plotOnMap = ShellExeAddr(build, VPX_fg_plot_on_map, base);
	w->plotGrid = ShellExeAddr(build, VPX_fg_plot_grid, base);
	w->artId = ShellExeAddr(build, VPX_fg_art_id, base);
	w->artDb = ShellExeAddr(build, VPX_fg_art_db, base);
	w->artName = ShellExeAddr(build, VPX_fg_farm_art_name, base);
	return true;
}

uint32_t FgPostImageBytes(int build) { return ShellExeSpan(build, kFgPostSigs, FG_NPOSTSIGS); }

bool FgPostWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken)
{
	return ShellExeLay(build, base, imageBytes, kFgPostSigs, FG_NPOSTSIGS, broken);
}

// ---- ticket 0655 (P1): the area probe's sites --------------------------------------------------------------------------------
int FgAreaPatternCount() { return FG_NAREASIGS; }

bool FgAreaCheckPatterns(uint32_t base, int build, char* why, size_t whyCap)
{
	for (int i = 0; i < FG_NAREANEED; ++i)
		if (!ShellExeVa(build, kFgAreaNeed[i]))
		{
			_snprintf_s(why, whyCap, _TRUNCATE, "symbol %s is not carried to %s", VpExeSymName(kFgAreaNeed[i]), VpExeBuildLabel(build));
			return false;
		}
	return ShellExeCheck(build, base, kFgAreaSigs, FG_NAREASIGS, why, whyCap);
}

bool FgAreaWorldFromBase(int build, uint32_t base, FgWorld* w)
{
	for (int i = 0; i < FG_NAREANEED; ++i)
		if (!ShellExeVa(build, kFgAreaNeed[i])) return false;
	memset(w, 0, sizeof(*w));
	w->lockInit = ShellExeAddr(build, VPX_fg_a_lockinit, base);
	w->lock = ShellExeAddr(build, VPX_fg_a_lock, base);
	w->mgr = ShellExeAddr(build, VPX_qg_channel_index, base);   // the A manager: its first word is the channel index
	w->slotOff = FG_AREA_SLOT_OFF;
	w->stride = FG_STRIDE;
	w->trampoline = ShellExeAddr(build, VPX_fg_type_handler, base);
	w->builder = ShellExeAddr(build, VPX_fg_area_listener, base);
	w->builderObj = ShellExeAddr(build, VPX_fg_builder_obj, base);
	return true;
}

uint32_t FgAreaImageBytes(int build) { return ShellExeSpan(build, kFgAreaSigs, FG_NAREASIGS); }

bool FgAreaWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken)
{
	return ShellExeLay(build, base, imageBytes, kFgAreaSigs, FG_NAREASIGS, broken);
}

// ---- ticket 0657: the edge placement's sites -------------------------------------------------------------------------------
int FgEdgePatternCount() { return FG_NEDGESIGS; }

bool FgEdgeCheckPatterns(uint32_t base, int build, char* why, size_t whyCap)
{
	for (int i = 0; i < FG_NEDGENEED; ++i)
		if (!ShellExeVa(build, kFgEdgeNeed[i]))
		{
			_snprintf_s(why, whyCap, _TRUNCATE, "symbol %s is not carried to %s", VpExeSymName(kFgEdgeNeed[i]), VpExeBuildLabel(build));
			return false;
		}
	return ShellExeCheck(build, base, kFgEdgeSigs, FG_NEDGESIGS, why, whyCap);
}

bool FgEdgeWorldFromBase(int build, uint32_t base, FgEdgeWorld* w)
{
	for (int i = 0; i < FG_NEDGENEED; ++i)
		if (!ShellExeVa(build, kFgEdgeNeed[i])) return false;
	memset(w, 0, sizeof(*w));
	w->end.lockInit = ShellExeAddr(build, VPX_fg_a_lockinit, base);
	w->end.lock = ShellExeAddr(build, VPX_fg_a_lock, base);
	w->end.mgr = ShellExeAddr(build, VPX_qg_channel_index, base);   // the A manager: its first word is the channel index
	w->end.slotOff = FG_EDGE_END_SLOT_OFF;
	w->end.stride = FG_STRIDE;
	w->end.trampoline = ShellExeAddr(build, VPX_fg_type_handler, base);
	w->end.builder = ShellExeAddr(build, VPX_fg_farm_end_listener, base);
	w->era.lockInit = ShellExeAddr(build, VPX_fg_lockinit, base);
	w->era.lock = ShellExeAddr(build, VPX_fg_lock, base);
	w->era.mgr = ShellExeAddr(build, VPX_pac_qb_index, base);
	w->era.slotOff = FG_EDGE_ERA_SLOT_OFF;
	w->era.stride = FG_STRIDE;
	w->era.trampoline = w->end.trampoline;
	w->era.builder = ShellExeAddr(build, VPX_fg_farm_era_listener, base);
	return true;   // builderObj 0: the node's object is the farm manager, whatever its address
}

uint32_t FgEdgeImageBytes(int build) { return ShellExeSpan(build, kFgEdgeSigs, FG_NEDGESIGS); }

bool FgEdgeWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken)
{
	return ShellExeLay(build, base, imageBytes, kFgEdgeSigs, FG_NEDGESIGS, broken);
}

// ---- ticket 0784: the tread filter's sites ---------------------------------------------------------------------------------
int FgTreadPatternCount() { return FG_NTREADSIGS; }

bool FgTreadCheckPatterns(uint32_t base, int build, char* why, size_t whyCap)
{
	for (int i = 0; i < FG_NTREADNEED; ++i)
		if (!ShellExeVa(build, kFgTreadNeed[i]))
		{
			_snprintf_s(why, whyCap, _TRUNCATE, "symbol %s is not carried to %s", VpExeSymName(kFgTreadNeed[i]), VpExeBuildLabel(build));
			return false;
		}
	if (ShellExeVa(build, VPX_tf_a28_lists) != ShellExeVa(build, VPX_qg_channel_index) + FG_TREAD_LISTS_OFF)
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "type 0x28's list slot is not the A manager's index + 0x%X on %s", FG_TREAD_LISTS_OFF,
			VpExeBuildLabel(build));
		return false;
	}
	return ShellExeCheck(build, base, kFgTreadSigs, FG_NTREADSIGS, why, whyCap);
}

bool FgTreadWorldFromBase(int build, uint32_t base, FgWorld* w)
{
	for (int i = 0; i < FG_NTREADNEED; ++i)
		if (!ShellExeVa(build, kFgTreadNeed[i])) return false;
	memset(w, 0, sizeof(*w));
	w->lockInit = ShellExeAddr(build, VPX_fg_a_lockinit, base);
	w->lock = ShellExeAddr(build, VPX_fg_a_lock, base);
	w->mgr = ShellExeAddr(build, VPX_qg_channel_index, base);   // the A manager: its first word is the channel index
	w->slotOff = FG_TREAD_SLOT_OFF;
	w->stride = FG_STRIDE;
	w->trampoline = ShellExeAddr(build, VPX_fg_type_handler, base);
	w->builder = ShellExeAddr(build, VPX_tf_l28_listener, base);
	w->builderObj = ShellExeAddr(build, VPX_tf_particle_obj, base);   // only the main Particle object's node
	return true;
}

uint32_t FgTreadImageBytes(int build) { return ShellExeSpan(build, kFgTreadSigs, FG_NTREADSIGS); }

bool FgTreadWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken)
{
	return ShellExeLay(build, base, imageBytes, kFgTreadSigs, FG_NTREADSIGS, broken);
}

uint32_t FgEffectNameHash(const char* name)
{
	uint32_t c = 0xFFFFFFFFu;
	for (const unsigned char* p = (const unsigned char*)name; p && *p; ++p)
	{
		c ^= *p;
		for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return c;   // no final inversion (the EXE's rule)
}

// ---- F2: the endpoint free list ----------------------------------------------------------------------------------------
void FgCheckGraph(uintptr_t graph, FgCheck* r)
{
	memset(r, 0, sizeof(*r));
	r->badIndex = 0xFFFFFFFFu;
	r->firstOver4 = 0xFFFFFFFFu;
	const uint32_t* h = (const uint32_t*)(graph + FG_EP_HDR);
	r->head = h[0]; r->live = h[1];
	const uintptr_t data = h[2];
	r->size = h[3]; r->cap = h[4];
	if (r->size == 0) { r->sound = 1; return; }   // nothing laid out (yet)
	if (!data || r->size > r->cap || r->cap > FG_SANE_SIZE)
	{
		_snprintf_s(r->why, _TRUNCATE, "header not plausible (data 0x%08X, size %u, capacity %u)", (unsigned)data, r->size, r->cap);
		return;
	}
	// every entry: the freed ones by bit 31, the live ones' counts
	for (uint32_t e = 0; e < r->size; ++e)
	{
		const uint32_t w = *(const uint32_t*)(data + (uintptr_t)e * 20u + 0x10u);
		if (w & 0x80000000u) { r->freedByBit++; continue; }
		if ((w & 0x7FFFFFFFu) > 4u)
		{
			if (!r->over4) { r->firstOver4 = e; r->firstOver4Word = w; }
			r->over4++;
		}
	}
	// the walk from the head: freed entries only, each once (more steps than freed entries = a cycle), to 0x0FFFFFFF
	uint32_t idx = r->head;
	while (idx != FG_NIL)
	{
		if (idx >= r->size)
		{
			r->badIndex = idx;
			_snprintf_s(r->why, _TRUNCATE, "the free list names entry 0x%08X past the array's %u entries (after %u steps)", idx, r->size, r->chain);
			return;
		}
		const uint32_t w = *(const uint32_t*)(data + (uintptr_t)idx * 20u + 0x10u);
		if (!(w & 0x80000000u))
		{
			r->badIndex = idx;
			_snprintf_s(r->why, _TRUNCATE, "the free list reaches live entry %u (count word 0x%08X) after %u steps", idx, w, r->chain);
			return;
		}
		if (++r->chain > r->freedByBit)
		{
			r->badIndex = idx;
			_snprintf_s(r->why, _TRUNCATE, "the free list has a cycle: %u steps for %u freed entries (at entry %u)", r->chain, r->freedByBit, idx);
			return;
		}
		idx = w & 0x7FFFFFFFu;
	}
	if (r->chain != r->freedByBit)
	{
		_snprintf_s(r->why, _TRUNCATE, "%u freed entries but the free list holds %u", r->freedByBit, r->chain);
		return;
	}
	if ((uint64_t)r->live + r->freedByBit != r->size)
	{
		_snprintf_s(r->why, _TRUNCATE, "live %u + freed %u != size %u", r->live, r->freedByBit, r->size);
		return;
	}
	if (r->over4)
	{
		_snprintf_s(r->why, _TRUNCATE, "%u live entries count more than 4 handles (first: entry %u, count word 0x%08X)", r->over4, r->firstOver4, r->firstOver4Word);
		return;
	}
	r->sound = 1;
}

bool FgCheckGraphSafe(uintptr_t graph, FgCheck* r)
{
	__try { FgCheckGraph(graph, r); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		r->sound = 0;
		_snprintf_s(r->why, _TRUNCATE, "the check's read of the graph faulted");
		return false;
	}
}

// ---- ticket 4854 (M3): the graph line, read only ----------------------------------------------------------------------------
static inline uint32_t Rd32(uintptr_t a) { return *(const uint32_t*)a; }
static inline uint16_t Rd16(uintptr_t a) { return *(const uint16_t*)a; }

// The first fault of one cell's list: counted once per list, the first list's reason kept.
static void ListBad(FgGraphStat* r, uint32_t cell, bool* bad, char* why, size_t cap, const char* fmt, ...)
{
	if (*bad) return;
	*bad = true;
	va_list ap; va_start(ap, fmt); _vsnprintf_s(why, cap, _TRUNCATE, fmt, ap); va_end(ap);
	r->listsBad++;
	if (r->firstBadCell == 0xFFFFFFFFu) { r->firstBadCell = cell; strncpy_s(r->firstBadWhy, why, _TRUNCATE); }
}

void FgGraphScan(uintptr_t graph, uintptr_t vtxVec, uint8_t* mark, uint32_t* lastList, uint32_t scratchCap, FgGraphStat* r)
{
	memset(r, 0, sizeof(*r));
	r->firstBadCell = 0xFFFFFFFFu;
	r->nx = Rd32(graph + FG_G_NX); r->ny = Rd32(graph + FG_G_NY);
	const uintptr_t cellData = Rd32(graph + FG_G_CELLS);
	const uint64_t cells64 = (uint64_t)r->nx * r->ny;
	r->cells = cells64 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)cells64;
	const uintptr_t ep = graph + FG_EP_HDR, nd = graph + FG_ND_HDR;
	r->epLive = Rd32(ep + 4); const uintptr_t epData = Rd32(ep + 8); r->epSize = Rd32(ep + 12); r->epCap = Rd32(ep + 16);
	r->nodeLive = Rd32(nd + 4); const uintptr_t ndData = Rd32(nd + 8); r->nodeSize = Rd32(nd + 12); r->nodeCap = Rd32(nd + 16);
	if (cells64 > FG_SANE_SIZE || (cells64 && !cellData))
	{ _snprintf_s(r->why, _TRUNCATE, "grid not plausible (%u x %u cells, array 0x%08X)", r->nx, r->ny, (unsigned)cellData); return; }
	if (r->nodeSize > r->nodeCap || r->nodeCap > FG_SANE_SIZE || (r->nodeSize && !ndData))
	{ _snprintf_s(r->why, _TRUNCATE, "node array not plausible (size %u, capacity %u, data 0x%08X)", r->nodeSize, r->nodeCap, (unsigned)ndData); return; }
	if (r->epSize > r->epCap || r->epCap > FG_SANE_SIZE || (r->epSize && !epData))
	{ _snprintf_s(r->why, _TRUNCATE, "endpoint array not plausible (size %u, capacity %u, data 0x%08X)", r->epSize, r->epCap, (unsigned)epData); return; }
	if (r->nodeSize > scratchCap)
	{ _snprintf_s(r->why, _TRUNCATE, "no scratch for %u nodes (%u)", r->nodeSize, scratchCap); return; }

	// the nodes: the freed bit; the scratch (lists met, the last list met on)
	for (uint32_t i = 0; i < r->nodeSize; ++i)
	{
		mark[i] = 0; lastList[i] = 0xFFFFFFFFu;
		if (Rd32(ndData + (uintptr_t)i * FG_NODE_BYTES + 0x2C) & 0x80000000u) r->nodeFreedBit++;
	}
	// every cell's list from its first node along +0x28: each node once per list, at most `count` nodes, the previous
	// links mirrored, ending at none with the last word; a step budget bounds a damaged graph
	const uint64_t budget = (uint64_t)r->nodeSize * 4u + 4096u;
	uint64_t steps = 0;
	// static, not on this frame: a faulting read unwinds this frame through the caller's SEH handler, and MSVC's x86 ASan
	// leaves an unwound frame's stack objects poisoned (found by ticket 4855's tests; the main thread is the only caller)
	static char why[64];
	for (uint32_t c = 0; c < r->cells; ++c)
	{
		const uintptr_t cell = cellData + (uintptr_t)c * FG_CELL_BYTES;
		const uint32_t cnt = Rd32(cell + 4), first = Rd32(cell + 8), last = Rd32(cell + 12);
		r->listsWalked++;
		uint32_t i = first, prev = FG_NIL, walked = 0;
		bool bad = false;
		why[0] = 0;
		while (i != FG_NIL)
		{
			if (i >= r->nodeSize) { ListBad(r, c, &bad, why, sizeof(why), "node 0x%X past the node array after %u", i, walked); break; }
			if (lastList[i] == c) { ListBad(r, c, &bad, why, sizeof(why), "a ring: node 0x%X met again after %u", i, walked); break; }
			if (walked >= cnt) { ListBad(r, c, &bad, why, sizeof(why), "more nodes than its count %u", cnt); break; }
			if (++steps > budget) { _snprintf_s(r->why, _TRUNCATE, "step budget %llu spent at cell %u", (unsigned long long)budget, c); return; }
			const uintptr_t n = ndData + (uintptr_t)i * FG_NODE_BYTES;
			const uint32_t w2c = Rd32(n + 0x2C);
			if ((w2c & 0x7FFFFFFFu) != prev)
				ListBad(r, c, &bad, why, sizeof(why), "node 0x%X links back to 0x%X, not 0x%X", i, w2c & 0x7FFFFFFFu, prev);
			if (mark[i] < 255) mark[i]++;
			if (mark[i] == 2) r->onTwoLists++;
			lastList[i] = c;
			r->listed++;
			if (Rd32(n + 0x10) == 0) r->listedNoPoints++;
			if (w2c & 0x80000000u) r->listedFreed++;
			if (Rd16(n + 0x34) != (uint16_t)(c & 0xFFFFu)) r->idMismatch++;
			if (c > 0xFFFFu) r->heldAbove++;
			prev = i; i = Rd32(n + 0x28); walked++;
		}
		if (!bad && i == FG_NIL)
		{
			if (walked != cnt) ListBad(r, c, &bad, why, sizeof(why), "count %u, walked %u", cnt, walked);
			else if (last != prev) ListBad(r, c, &bad, why, sizeof(why), "last word 0x%X, the walk ended at 0x%X", last, prev);
		}
		if (c == 0xFFFFu)
		{
			r->c65535 = bad ? -1 : 1; r->c65535Count = cnt; r->c65535Walked = walked;
			strncpy_s(r->c65535Why, why, _TRUNCATE);
		}
	}
	// cell 65,535's members that are on another list too (walked again, at most as far as the first walk went)
	if (r->cells > 0xFFFFu)
	{
		uint32_t i = Rd32(cellData + (uintptr_t)0xFFFFu * FG_CELL_BYTES + 8);
		for (uint32_t k = 0; k < r->c65535Walked && i < r->nodeSize; ++k)
		{
			if (mark[i] >= 2) r->c65535Shared++;
			i = Rd32(ndData + (uintptr_t)i * FG_NODE_BYTES + 0x28);
		}
	}
	// live nodes (no freed bit): those on no list; the cell id 0xFFFF by kind
	for (uint32_t i = 0; i < r->nodeSize; ++i)
	{
		const uintptr_t n = ndData + (uintptr_t)i * FG_NODE_BYTES;
		if (Rd32(n + 0x2C) & 0x80000000u) continue;
		const uint16_t id = Rd16(n + 0x34);
		if (id == 0xFFFFu) { const uint8_t k = *(const uint8_t*)(n + 0x36); r->idNoneByKind[k < 3 ? k : 3]++; }
		if (!mark[i]) { r->unlisted++; if (id != 0xFFFFu) r->unlistedNamed++; }
	}
	// the endpoints: the handles below each live entry's count (at most 4)
	for (uint32_t e = 0; e < r->epSize; ++e)
	{
		const uintptr_t p = epData + (uintptr_t)e * FG_EP_BYTES;
		const uint32_t w = Rd32(p + 0x10);
		if (w & 0x80000000u) { r->epFreed++; continue; }
		uint32_t cnt = w & 0x7FFFFFFFu;
		if (cnt > 4) { r->epCountOver4++; cnt = 4; }
		for (uint32_t s = 0; s < cnt; ++s)
		{
			const uint32_t h = Rd32(p + 4u * s), idx = h & 0x0FFFFFFFu;
			if (idx == FG_NIL) { r->hNeverFilled++; if (!((h >> 29) & 3u)) r->hNeverFilledKind0++; continue; }
			if (idx >= r->nodeSize) { r->hPast++; continue; }
			const uint16_t back = Rd16(ndData + (uintptr_t)idx * FG_NODE_BYTES + ((h & 0x10000000u) ? 0x16 : 0x14));
			if ((uint32_t)back != e) r->hNotBack++;
		}
	}
	if (vtxVec) { r->vtxSize = Rd32(vtxVec + 4); r->vtxCap = Rd32(vtxVec + 8); r->vtxOk = 1; }
	r->ok = 1;
}

static uint8_t* g_scMark;
static uint32_t* g_scList;
static uint32_t g_scCap;
static bool ReadNodeSize(uintptr_t graph, uint32_t* n)
{
	__try { *n = *(const uint32_t*)(graph + FG_ND_HDR + 12); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool ScanSeh(uintptr_t graph, uintptr_t vtxVec, FgGraphStat* r)
{
	__try { FgGraphScan(graph, vtxVec, g_scMark, g_scList, g_scCap, r); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool VtxSeh(uintptr_t vtxVec, FgGraphStat* r)
{
	__try { r->vtxSize = Rd32(vtxVec + 4); r->vtxCap = Rd32(vtxVec + 8); r->vtxOk = 1; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { r->vtxOk = 0; return false; }
}

bool FgGraphScanSafe(uintptr_t graph, uintptr_t vtxVec, FgGraphStat* r)
{
	const int64_t t0 = Qpc();
	uint32_t n = 0;
	if (!ReadNodeSize(graph, &n))
	{
		memset(r, 0, sizeof(*r));
		_snprintf_s(r->why, _TRUNCATE, "the graph's header could not be read (fault)");
		return false;
	}
	if (n <= FG_SANE_SIZE && n > g_scCap)
	{
		const uint32_t cap = n + n / 4 + 1024;
		uint8_t* m = (uint8_t*)realloc(g_scMark, cap);
		if (m) g_scMark = m;
		uint32_t* l = m ? (uint32_t*)realloc(g_scList, (size_t)cap * 4u) : nullptr;
		if (l) g_scList = l;
		if (m && l) g_scCap = cap;
	}
	const bool read = ScanSeh(graph, vtxVec, r);
	if (!read)
	{
		r->ok = 0;
		_snprintf_s(r->why, _TRUNCATE, "a read of the graph faulted (the counts are partial)");
		if (vtxVec && !r->vtxOk) VtxSeh(vtxVec, r);
	}
	r->ms = QpcMs(Qpc() - t0);
	return read;
}

void FgGraphLineText(const FgGraphStat& g, const char* where, char* out, size_t cap)
{
	const bool loud = g.listsBad || g.listedNoPoints || g.listedFreed || g.hNeverFilledKind0 || g.c65535 < 0;
	char c65535[160], bad[120], vtx[64];
	if (g.cells <= 0xFFFFu) _snprintf_s(c65535, _TRUNCATE, "none (the grid fits 16-bit ids)");
	else _snprintf_s(c65535, _TRUNCATE, "count %u, walked %u, %s%s, also on another list %u", g.c65535Count, g.c65535Walked,
		g.c65535 > 0 ? "sound" : g.c65535 < 0 ? "NOT SOUND: " : "not reached", g.c65535 < 0 ? g.c65535Why : "", g.c65535Shared);
	if (g.listsBad) _snprintf_s(bad, _TRUNCATE, " (first: cell %u: %s)", g.firstBadCell, g.firstBadWhy); else bad[0] = 0;
	if (g.vtxOk) _snprintf_s(vtx, _TRUNCATE, "size %u, capacity %u", g.vtxSize, g.vtxCap); else _snprintf_s(vtx, _TRUNCATE, "not read");
	_snprintf_s(out, cap, _TRUNCATE,
		"farm_guard: %sgraph (%s): grid %u x %u = %u cells (%s 65,535); nodes live %u, size %u, capacity %u, freed bit %u "
		"(live + freed %s size); endpoints live %u, size %u, capacity %u, freed %u; ids past 65,535: nodes %s, endpoints %s; "
		"cell lists walked %u, nodes listed %u, lists not sound %u%s; listed with no points %u, with the freed bit %u; "
		"+0x34 not the holding cell's low 16 bits %u; held by a cell above 65,535 %u; on two lists %u; live nodes on no list %u "
		"(naming a cell %u); cell id 0xFFFF on live nodes by kind 0/1/2/other %u/%u/%u/%u; cell 65,535: %s; handles: never-filled "
		"slot %u (kind 0: %u), past the node array %u, not named back %u; endpoints counting above 4 %u; vertex vector %s; "
		"scan %.2f ms%s%s",
		loud ? "LOUD: " : "", where, g.nx, g.ny, g.cells, g.cells > 0xFFFFu ? "ABOVE" : "within", g.nodeLive, g.nodeSize, g.nodeCap,
		g.nodeFreedBit, (uint64_t)g.nodeLive + g.nodeFreedBit == g.nodeSize ? "=" : "!=", g.epLive, g.epSize, g.epCap, g.epFreed,
		g.nodeSize > 0xFFFFu ? "YES" : "no", g.epSize > 0xFFFFu ? "YES" : "no",
		g.listsWalked, g.listed, g.listsBad, bad, g.listedNoPoints, g.listedFreed, g.idMismatch, g.heldAbove, g.onTwoLists,
		g.unlisted, g.unlistedNamed, g.idNoneByKind[0], g.idNoneByKind[1], g.idNoneByKind[2], g.idNoneByKind[3], c65535,
		g.hNeverFilled, g.hNeverFilledKind0, g.hPast, g.hNotBack, g.epCountOver4, vtx, g.ms,
		g.ok ? "" : "; NOT COMPLETE: ", g.ok ? "" : g.why);
}

// ---- ticket 4855: rule 2's geometry ---------------------------------------------------------------------------------------
static int Cube(int dq, int dr) { return (abs(dq) + abs(dr) + abs(dq + dr)) / 2; }

int FgHexDistWrap(int hx1, int hy1, int hx2, int hy2, int mapW)
{
	const int dr = hy1 - hy2, dq = hx1 - hx2;
	int best = Cube(dq, dr);
	if (mapW > 0)   // a shift of one map width in grid x at the same row is a shift of one map width in hex x
		for (int k = -3; k <= 3; ++k) { const int d = Cube(dq + k * mapW, dr); if (d < best) best = d; }
	return best;
}


// The plot's centre in layout units as the EXE's plot rectangle has it (farm-fields-layout.md 1.3): the cube coordinates
// (hx, hy, -hx-hy, 1) through the hex -> world matrix (row vector times matrix), x + the world box's minimum x, y x 0.866 +
// its minimum y.
struct FgPlotFrame { float m[16]; float minX, minY; };
static void PlotCentre(const FgPlotFrame& f, int hx, int hy, float* x, float* y)
{
	const float a = (float)hx, b = (float)hy, c = (float)(-hx - hy);
	*x = a * f.m[0] + b * f.m[4] + c * f.m[8] + f.m[12] + f.minX;
	*y = (a * f.m[1] + b * f.m[5] + c * f.m[9] + f.m[13]) * FG_ROW_SY + f.minY;
}

void FgRule2Geometry(uintptr_t graph, uintptr_t h2w, uintptr_t aabbMin, FgRule2Geo* g)
{
	// Every read goes through SafeCopy, so a fault is caught in that leaf frame and this one never unwinds (MSVC's x86 ASan
	// leaves an SEH-unwound frame's stack poisoned: two reports in the test's faulting case before this form).
	memset(g, 0, sizeof(*g));
	static const char* kFault = "a read of the grid or the plot geometry faulted";
	uint32_t hdr[(0xD0 - 0x6C) / 4];                // graph +0x6C .. +0xCF: extent, origin, cells, the node header
	if (!SafeCopy(graph + 0x6C, hdr, sizeof(hdr))) { _snprintf_s(g->why, _TRUNCATE, "%s", kFault); return; }
	g->nx = hdr[(FG_G_NX - 0x6C) / 4]; g->ny = hdr[(FG_G_NY - 0x6C) / 4];
	const uint64_t cells = (uint64_t)g->nx * g->ny;
	g->cells = cells > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)cells;
	if (cells <= FG_CELL_NONE) { _snprintf_s(g->why, _TRUNCATE, "the grid has %u cells: no cell 65,535", g->cells); return; }
	float ex, ey, ox, oy;
	memcpy(&ex, &hdr[0], 4); memcpy(&ey, &hdr[1], 4); memcpy(&ox, &hdr[2], 4); memcpy(&oy, &hdr[3], 4);
	g->col = FG_CELL_NONE / g->ny; g->row = FG_CELL_NONE % g->ny;   // column-major (farm-field-double-free.md 2.2)
	g->cw = ex / (float)g->nx; g->ch = ey / (float)g->ny;
	g->cx = ox + ((float)g->col + 0.5f) * g->cw; g->cy = oy + ((float)g->row + 0.5f) * g->ch;
	if (!(g->cw > 1.0f && g->cw < 1000.0f && g->ch > 1.0f && g->ch < 1000.0f))
	{ _snprintf_s(g->why, _TRUNCATE, "the grid's cells are not plausible (%.3f x %.3f)", g->cw, g->ch); return; }
	g->mapW = g->nx / 2;   // two cells per plot in x (farm-field-double-free.md 2.6)
	if ((g->nx & 1) || fabsf(ex - 64.0f * (float)g->mapW) > 64.0f)
	{ _snprintf_s(g->why, _TRUNCATE, "the grid (%u columns, extent %.1f) does not give the map width", g->nx, ex); return; }
	FgPlotFrame f;
	float box[2];
	if (!SafeCopy(h2w, f.m, sizeof(f.m)) || !SafeCopy(aabbMin, box, sizeof(box))) { _snprintf_s(g->why, _TRUNCATE, "%s", kFault); return; }
	f.minX = box[0]; f.minY = box[1];
	// the lattice: one hex x = 64 units in x; one hex y = 32 in x and 64 x 0.866 in y (camera-and-picking.md 5)
	float x00, y00, x10, y10, x01, y01;
	PlotCentre(f, 0, 0, &x00, &y00); PlotCentre(f, 1, 0, &x10, &y10); PlotCentre(f, 0, 1, &x01, &y01);
	const float ux = x10 - x00, uy = y10 - y00, vx = x01 - x00, vy = y01 - y00;
	// (rows may run either way in y: a mirrored lattice keeps the cube distance)
	if (fabsf(ux - 64.0f) > 0.05f || fabsf(uy) > 0.05f || fabsf(vx - 32.0f) > 0.05f || fabsf(fabsf(vy) - 64.0f * FG_ROW_SY) > 0.05f)
	{
		_snprintf_s(g->why, _TRUNCATE, "the plot geometry is not the hex lattice (hex x step %.3f, %.3f; hex y step %.3f, %.3f)", ux, uy, vx, vy);
		return;
	}
	// the hex holding the cell's centre: the lattice inverted, cube-rounded, then the nearest centre among its neighbours
	const float det = ux * vy - uy * vx;
	const float a = ((g->cx - x00) * vy - (g->cy - y00) * vx) / det, b = (ux * (g->cy - y00) - uy * (g->cx - x00)) / det;
	int rx = (int)floorf(a + 0.5f), ry = (int)floorf(b + 0.5f), rz = (int)floorf(-a - b + 0.5f);
	const float drx = fabsf((float)rx - a), dry = fabsf((float)ry - b), drz = fabsf((float)rz + a + b);
	if (drx > dry && drx > drz) rx = -ry - rz; else if (dry > drz) ry = -rx - rz;
	float best = 1e30f;
	for (int dq = -1; dq <= 1; ++dq)
		for (int dr = -1; dr <= 1; ++dr)
		{
			float x, y; PlotCentre(f, rx + dq, ry + dr, &x, &y);
			const float d = (x - g->cx) * (x - g->cx) + (y - g->cy) * (y - g->cy);
			if (d < best) { best = d; g->phx = rx + dq; g->phy = ry + dr; }
		}
	g->pdist = sqrtf(best);
	g->py = g->phy;
	const int gx = g->phx + (g->phy - (g->phy & 1)) / 2;
	g->px = ((gx % (int)g->mapW) + (int)g->mapW) % (int)g->mapW;
	// the self-check: every plot within hex distance 4 whose rectangle (+-64, +-64 x 0.866, grown by the margin) meets the
	// cell must be within distance 2
	const float hx = 64.0f + FG_R2_MARGIN + g->cw * 0.5f, hy = 64.0f * FG_ROW_SY + FG_R2_MARGIN + g->ch * 0.5f;
	for (int dq = -4; dq <= 4; ++dq)
		for (int dr = -4; dr <= 4; ++dr)
		{
			const int d = Cube(dq, dr);
			if (d > 4) continue;
			float x, y; PlotCentre(f, g->phx + dq, g->phy + dr, &x, &y);
			if (fabsf(x - g->cx) < hx && fabsf(y - g->cy) < hy) { g->reach++; if (d > 2) g->reachFar++; }
		}
	if (g->reachFar || !g->reach)
	{
		_snprintf_s(g->why, _TRUNCATE, "%u plots past hex distance 2 can reach cell 65,535 (%u in all)", g->reachFar, g->reach);
		return;
	}
	// MC #5493 (0542): the live kind-0 nodes (the bake's coastline pieces) whose box (+0x18 min x, min y, max x, max y) meets
	// the cell or its image one world width over (the x wrap): any -> radius 4
	const uintptr_t ndData = hdr[(FG_ND_HDR + 8 - 0x6C) / 4];
	const uint32_t nodes = hdr[(FG_ND_HDR + 12 - 0x6C) / 4];
	if (nodes > FG_SANE_SIZE || (nodes && !ndData))
	{ _snprintf_s(g->why, _TRUNCATE, "the node array is not plausible (size %u)", nodes); return; }
	const float x0 = g->cx - g->cw * 0.5f, x1 = g->cx + g->cw * 0.5f, y0 = g->cy - g->ch * 0.5f, y1 = g->cy + g->ch * 0.5f;
	for (uint32_t i = 0; i < nodes; ++i)
	{
		unsigned char nb[FG_NODE_BYTES];
		if (!SafeCopy(ndData + (uintptr_t)i * FG_NODE_BYTES, nb, sizeof(nb))) { _snprintf_s(g->why, _TRUNCATE, "%s", kFault); return; }
		uint32_t w2c; memcpy(&w2c, nb + 0x2C, 4);
		if ((w2c & 0x80000000u) || nb[0x36] != 0) continue;
		g->kind0++;
		float bb[4]; memcpy(bb, nb + 0x18, sizeof(bb));
		const float bx0 = bb[0], by0 = bb[1], bx1 = bb[2], by1 = bb[3];
		if (by1 < y0 || by0 > y1) continue;
		for (int k = -1; k <= 1; ++k)
		{
			const float s = (float)k * ex;
			if (bx1 >= x0 + s && bx0 <= x1 + s) { g->kind0Touch++; break; }
		}
	}
	g->radius = g->kind0Touch ? 4 : 2;
	g->ok = 1;
}

// ---- F1: the listener list ---------------------------------------------------------------------------------------------
static void __fastcall FgHook(void* obj, void* edx, void* ev);
static void __fastcall FgInert(void* obj, void* edx, void* ev) { (void)obj; (void)edx; (void)ev; }
FgListenerFn FarmGuard_Hook() { return FgHook; }
FgListenerFn FarmGuard_Inert() { return FgInert; }
// ticket 0535: the words a node of the guard holds (the owner's stubs, else the functions themselves)
static uintptr_t HookWord() { return g_hookAddr ? g_hookAddr : (uintptr_t)FgHook; }
static uintptr_t InertWord() { return g_inertAddr ? g_inertAddr : (uintptr_t)FgInert; }
uintptr_t FarmGuard_HookWord() { return HookWord(); }
uintptr_t FarmGuard_InertWord() { return InertWord(); }
int FarmGuard_Role() { return g_role; }

// ticket 0655: the hook and inert words given (the area probe's list has its own)
static void ScanListWith(const FgWorld& w, uintptr_t hook, uintptr_t inert, FgScan* s)
{
	memset(s, 0, sizeof(*s));
	s->channel = *(const uint32_t*)w.mgr;
	if (s->channel >= FG_MAX_CHANNEL) { _snprintf_s(s->why, _TRUNCATE, "channel index %u", s->channel); return; }
	const uintptr_t slot = w.mgr + w.slotOff + (uintptr_t)s->channel * w.stride;
	const uintptr_t handler = *(const uint32_t*)slot;
	s->list = *(const uint32_t*)(slot + 4);
	if (!s->list) { s->ok = 1; return; }   // nothing registered on type 59 yet
	if (handler != w.trampoline)
	{
		_snprintf_s(s->why, _TRUNCATE, "type 59's handler is 0x%08X, not the listener walk's 0x%08X", (unsigned)handler, (unsigned)w.trampoline);
		return;
	}
	const uint32_t* L4 = (const uint32_t*)s->list;
	const uintptr_t data = L4[2];
	s->size = L4[3];
	const uint32_t cap = L4[4];
	s->count = L4[5];
	const uint32_t head = L4[6];
	if (s->count > s->size || s->size > cap || cap > FG_SANE_LIST || (s->count && !data))
	{
		_snprintf_s(s->why, _TRUNCATE, "list not plausible (count %u, size %u, capacity %u, nodes 0x%08X)", s->count, s->size, cap, (unsigned)data);
		return;
	}
	uint32_t idx = head;
	for (uint32_t i = 0; i < s->count; ++i)
	{
		if (idx >= s->size) { _snprintf_s(s->why, _TRUNCATE, "node index 0x%08X past the list's %u (step %u)", idx, s->size, i); return; }
		const uintptr_t node = data + (uintptr_t)idx * 20u;
		const uintptr_t o = *(const uint32_t*)(node + 0x0C), f = *(const uint32_t*)(node + 0x10);
		if ((!w.builderObj || o == w.builderObj) && (f == w.builder || f == hook || f == inert))   // 0657: builderObj 0 = any object
		{
			if (s->n >= FG_MAX_NODES) { _snprintf_s(s->why, _TRUNCATE, "more than %u nodes of the builder's object", FG_MAX_NODES); return; }
			s->idx[s->n] = idx; s->fnWord[s->n] = node + 0x10; s->fn[s->n] = f; s->n++;
		}
		idx = *(const uint32_t*)node;
	}
	s->ok = 1;
}

void FgScanList(const FgWorld& w, FgScan* s) { ScanListWith(w, HookWord(), InertWord(), s); }

static bool ScanSafe(FgScan* s)
{
	__try { FgScanList(g_w, s); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { s->ok = 0; _snprintf_s(s->why, _TRUNCATE, "the list's read faulted"); return false; }
}

// 1 = a node of the list still holds the builder itself (a registration not swapped yet: this call came through a stale
// node); 0 = none; also 0 when the list could not be read (the builder is then called as the EXE would have).
static int StaleCheck()
{
	FgScan s;
	if (!ScanSafe(&s) || !s.ok) return 0;
	for (uint32_t i = 0; i < s.n; ++i) if (s.fn[i] == g_w.builder) return 1;
	return 0;
}

static uint32_t ReadViewFlag(bool* ok)
{
	__try { *ok = true; return *(volatile const uint32_t*)g_w.viewFlag; }
	__except (EXCEPTION_EXECUTE_HANDLER) { *ok = false; return 0; }
}

// ---- the record (D119: the server's report takes the tail of every *.log in the proxy's run folder) -----------------------
static void ChooseRecordDir(const wchar_t* outDir)
{
	if (g_recordDirChosen) return;
	g_recordDirChosen = true;
	g_recordDir[0] = 0; g_recordInRun = false;
	if (!outDir || !outDir[0]) return;
	// the shipped layout (issue 0148): <player>\Logs\VP64\run\<yyyymmdd>-<hhmmss>-<pid>-<n>; the newest of this process
	wchar_t pat[MAX_PATH], best[MAX_PATH] = L"";
	const size_t ol = wcslen(outDir);
	const wchar_t* sep = (ol && (outDir[ol - 1] == L'\\' || outDir[ol - 1] == L'/')) ? L"" : L"\\";
	_snwprintf_s(pat, _TRUNCATE, L"%s%sVP64\\run\\*-%lu-*", outDir, sep, GetCurrentProcessId());
	wchar_t mid[32]; _snwprintf_s(mid, _TRUNCATE, L"-%lu-", GetCurrentProcessId());
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
			// the name: 8 digits '-' 6 digits '-' <pid> '-' digits (the pid exactly, not a longer number containing it)
			const wchar_t* n = fd.cFileName;
			if (wcslen(n) < 19 || n[8] != L'-' || n[15] != L'-' || wcsncmp(n + 15, mid, wcslen(mid)) != 0) continue;
			if (!best[0] || wcscmp(n, best) > 0) wcsncpy_s(best, n, _TRUNCATE);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	if (best[0])
	{
		_snwprintf_s(g_recordDir, _TRUNCATE, L"%s%sVP64\\run\\%s", outDir, sep, best);
		g_recordInRun = true;
	}
	else
		_snwprintf_s(g_recordDir, _TRUNCATE, L"%s", outDir);
}

static void WriteRecord(const char* text)
{
	if (!g_recordDir[0]) { L("farm_guard: LOUD: no folder for farm-guard.log (the composite has no log folder): the record is only in this log"); return; }
	if (!g_recordInRun)
		L("farm_guard: LOUD: the proxy's run folder of this process was not found under the composite's log folder: farm-guard.log is written beside this log, and an error report does not carry it");
	wchar_t p[MAX_PATH];
	const size_t dl = wcslen(g_recordDir);
	_snwprintf_s(p, _TRUNCATE, L"%s%sfarm-guard.log", g_recordDir, (dl && g_recordDir[dl - 1] == L'\\') ? L"" : L"\\");
	FILE* f = nullptr;
	if (_wfopen_s(&f, p, L"ab") || !f) { L("farm_guard: LOUD: farm-guard.log could not be written (%S)", p); return; }
	fputs(text, f);
	fclose(f);
	char pn[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, p, -1, pn, (int)sizeof(pn), nullptr, nullptr);
	L("farm_guard: the record is in %s", pn);
}

// where: "after builder call N" (the hook) or "at the in-game check of grid seq S" (a Present)
static void Trip(const FgCheck& c, const char* where)
{
	g_tripped = true; g_st.tripped = 1;
	if (g_role == 1 && g_shared) InterlockedExchange(&g_shared->tripped, 1);   // ticket 0535: kept for a later proxy boot
	SYSTEMTIME t; GetLocalTime(&t);
	char b[2048];
	_snprintf_s(b, _TRUNCATE,
		"%04u-%02u-%02u %02u:%02u:%02u farm_guard: LOUD: the EXE's farm-field layout graph failed the free-list check %s "
		"(process %lu, load %u, builder calls %u): %s. Endpoints: head 0x%08X, live %u, size %u, capacity %u, freed %u, walked %u, "
		"counts above 4 %u. The EXE's farm builder is not called again in this process (ticket 0527, decision D186): farms "
		"built or changed from now on get no field decals until the game is restarted.\r\n",
		t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, where, GetCurrentProcessId(), g_loads, g_st.builderCalls,
		c.why, c.head, c.live, c.size, c.cap, c.freedByBit, c.chain, c.over4);
	L("%.*s", (int)strlen(b) - 22, b + 20);   // the same text without the time stamp (20 characters) and the line end
	WriteRecord(b);
}

// ---- the event log (MC #5073, for 0528): one line per call of the hook in farm-builder-calls.log beside farm-guard.log --------
// The event payload's fields the builder reads first (farm-builder-guard.md 2): the dword at +0x08, the 16-bit plot x and y at
// +0x18 / +0x1A, the byte at +0x1C. A data read under SEH, before the call; the CRT buffers the lines, flushed per burst.
static FILE* g_evLog;
static bool g_evLogTried;
static uint32_t g_evLines, g_bEvLines;

struct FgEvent { int ok; uint32_t d8; int16_t x, y; uint8_t b1c; };
static FgEvent ReadEvent(const void* ev)
{
	FgEvent e = {};
	__try
	{
		const unsigned char* p = (const unsigned char*)ev;
		e.d8 = *(const uint32_t*)(p + 0x08);
		e.x = *(const int16_t*)(p + 0x18);
		e.y = *(const int16_t*)(p + 0x1A);
		e.b1c = p[0x1C];
		e.ok = 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { e.ok = 0; }
	return e;
}

static void EventLine(const void* ev, const char* what)
{
	if (!g_evLogTried)
	{
		g_evLogTried = true;
		if (g_recordDir[0])
		{
			wchar_t p[MAX_PATH];
			const size_t dl = wcslen(g_recordDir);
			_snwprintf_s(p, _TRUNCATE, L"%s%sfarm-builder-calls.log", g_recordDir, (dl && g_recordDir[dl - 1] == L'\\') ? L"" : L"\\");
			g_evLog = _wfsopen(p, L"ab", _SH_DENYNO);   // shared: tools and a report can read it while the game runs
			char pn[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, p, -1, pn, (int)sizeof(pn), nullptr, nullptr);
			L("farm_guard: the builder's events are logged one line per call in %s%s", pn, g_evLog ? "" : " (could not be opened: not logged)");
			if (g_evLog) fprintf(g_evLog, "# ticket 0527 (MC #5073): one line per call of the farm builder's listener: call, grid seq, "
				"what, plot x, y, the payload's dword +0x08, byte +0x1C (process %lu)\r\n", GetCurrentProcessId());
		}
	}
	if (!g_evLog) return;
	const FgEvent e = ReadEvent(ev);
	if (e.ok)
		fprintf(g_evLog, "%u seq=%u %s x=%d y=%d d8=0x%08X b1c=%u\r\n", g_st.calls, g_gridSeq, what, e.x, e.y, e.d8, e.b1c);
	else
		fprintf(g_evLog, "%u seq=%u %s payload not readable\r\n", g_st.calls, g_gridSeq, what);
	g_evLines++; g_bEvLines++;
}

static void EventFlush() { if (g_evLog) fflush(g_evLog); }

// ---- ticket 4855: rule 2 before each builder call (farm_builder=1) ----------------------------------------------------------
static bool g_r2Stopped;                      // (a) fired: no further builder call in this process
static uint32_t g_r2Key[7];                   // the grid the geometry below is for (seq, cells, extent, origin)
static bool g_r2Have;
static LONG g_r2Gen;                          // ticket 0535: g_swapGen when the geometry was set up (a swap = a new registration)
static FgRule2Geo g_r2;
static uint32_t g_bRule2;                     // calls of the burst skipped by rule 2

static bool Rule2GeoSeh(FgRule2Geo* g)
{
	__try { FgRule2Geometry(g_w.graph, g_w.h2w, g_w.aabbMin, g); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { g->ok = 0; _snprintf_s(g->why, _TRUNCATE, "a read of the grid or the plot geometry faulted"); return false; }
}
// The grid header and cell 65,535's count word; false = a read faulted.
static bool Rule2Read(uint32_t key[7], uint32_t* cells, uint32_t* count)
{
	__try
	{
		key[0] = g_gridSeq;
		key[1] = Rd32(g_w.graph + FG_G_NX); key[2] = Rd32(g_w.graph + FG_G_NY);
		for (int i = 0; i < 4; ++i) key[3 + i] = Rd32(g_w.graph + 0x6C + 4u * i);
		const uint64_t c = (uint64_t)key[1] * key[2];
		*cells = c > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)c;
		*count = 0;
		if (c > FG_CELL_NONE) *count = Rd32(Rd32(g_w.graph + FG_G_CELLS) + (uintptr_t)FG_CELL_NONE * FG_CELL_BYTES + 4);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 0 = call the builder; else the event line's word for the skip
static const char* Rule2(const void* ev)
{
	if (g_r2Stopped) { g_st.skipRule2Stop++; return "skipped-rule2-stopped"; }
	uint32_t key[7], cells = 0, count = 0;
	if (!Rule2Read(key, &cells, &count)) return nullptr;   // the grid could not be read: the builder is called as the EXE would
	if (cells <= FG_CELL_NONE) return nullptr;             // a grid within 16-bit cell ids: never skipped
	if (count)
	{
		g_r2Stopped = true; g_st.rule2Stopped = 1; g_st.skipRule2Stop++;
		if (g_role == 1 && g_shared) InterlockedExchange(&g_shared->rule2Stopped, 1);   // ticket 0535: kept for a later proxy boot
		SYSTEMTIME t; GetLocalTime(&t);
		char b[1024];
		_snprintf_s(b, _TRUNCATE,
			"%04u-%02u-%02u %02u:%02u:%02u farm_guard: LOUD: rule 2 (a): cell 65,535 of the farm-field layout grid (%u x %u = %u "
			"cells, above 65,535) holds %u nodes before builder call %u (process %lu, load %u): the cell's 16-bit id reads as "
			"none and its list breaks at the next refile (farm-field-double-free.md 2). The EXE's farm builder is not called again "
			"in this process (ticket 4855): farms built or changed from now on get no field decals until the game is restarted.\r\n",
			t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, key[1], key[2], cells, count, g_st.builderCalls + 1,
			GetCurrentProcessId(), g_loads);
		L("%.*s", (int)strlen(b) - 22, b + 20);
		WriteRecord(b);
		return "skipped-rule2-stopped";
	}
	const LONG gen = g_swapGen;
	if (!g_r2Have || gen != g_r2Gen || memcmp(key, g_r2Key, sizeof(key)) != 0)
	{
		memcpy(g_r2Key, key, sizeof(key)); g_r2Have = true; g_r2Gen = gen;
		Rule2GeoSeh(&g_r2);
		g_st.rule2Grids++;
		if (g_r2.ok)
			L("farm_guard: rule 2: the grid has %u x %u = %u cells (above 65,535): cell 65,535 = column %u, row %u, centre (%.1f, "
			  "%.1f), %.2f x %.2f; it lies in plot (%d, %d) (hex %d, %d; %.1f from its centre), map width %u; %u plots within hex "
			  "distance 4 can reach the cell, all within distance 2; kind-0 nodes %u, of them with a box meeting the cell %u: "
			  "radius %d: the builder is not called for the plots within hex distance %d of it (x wrapped) while cell 65,535 holds "
			  "nothing", g_r2.nx, g_r2.ny, g_r2.cells, g_r2.col, g_r2.row, g_r2.cx, g_r2.cy, g_r2.cw, g_r2.ch, g_r2.px, g_r2.py,
			  g_r2.phx, g_r2.phy, g_r2.pdist, g_r2.mapW, g_r2.reach, g_r2.kind0, g_r2.kind0Touch, g_r2.radius, g_r2.radius);
		else
			L("farm_guard: LOUD: rule 2: the grid has %u cells (above 65,535) and the plot holding cell 65,535 is not known (%s): "
			  "the builder is not called on this grid (0538's proposal 1)", g_r2.cells, g_r2.why);
	}
	if (!g_r2.ok) { g_st.skipRule2Geo++; return "skipped-rule2-nogeo"; }
	const FgEvent e = ReadEvent(ev);
	if (!e.ok || FgHexDistWrap(e.x, e.y, g_r2.phx, g_r2.phy, (int)g_r2.mapW) <= g_r2.radius)   // an unreadable payload: skipped too
	{ g_st.skipRule2Near++; return "skipped-rule2-near"; }
	return nullptr;
}

// ---- ticket 0535: the owner without a composite ---------------------------------------------------------------------------
// No Present reaches the owner, so nothing runs on the main thread between frames: the in-game check (and the graph line)
// run in the hook, on the main thread, before the first builder call of every burst. A bad result trips the guard as the
// in-game check would; that call and every later one are skipped.
// Once a composite has handed over one Present in this image, its Presents drive the jobs for good, as in 0527: the EXE
// presents nothing during a long layout (1,200 builder calls in about 20 s on a huge load, 0535's load s1a), and the
// burst line and its graph line then come at the next Present.
static bool OwnerAlone() { return g_role == 1 && !g_lastFwdQpc; }
static void GraphLine(const char* where);
static bool PreCheck()
{
	FgCheck c;
	const bool read = FgCheckGraphSafe(g_w.graph, &c);
	char w[96]; _snprintf_s(w, _TRUNCATE, "before builder call %u, the first of a burst", g_st.builderCalls + 1);
	L("farm_guard: before a burst (owner %s, no composite: the in-game check at the burst's first builder call %u): %s%s%s; "
	  "endpoints live %u, size %u, freed %u, walked %u", g_ownerName, g_st.builderCalls + 1, !read ? "could not read" : c.sound ? "sound" : "BAD",
	  c.sound ? "" : ": ", c.sound ? "" : c.why, c.live, c.size, c.freedByBit, c.chain);
	GraphLine(w);
	if (read && !c.sound && !g_tripped)
	{
		g_st.checksBad++;
		char tw[96]; _snprintf_s(tw, _TRUNCATE, "before builder call %u (the first of a burst; no composite)", g_st.builderCalls + 1);
		Trip(c, tw);
	}
	return !g_tripped;
}

// ---- ticket 0655: the farm-end post (the builder's last step, on the skipped-server path) ---------------------------------
// The builder's three tests, then its last step, as its own code does them (cities-and-landmarks.md 2.6.1 (1) and (3);
// farm-builder-guard.md 8): the flag byte +0x1C non-zero; the EXE's plot test on its object with the cube coordinates
// (x, y, -x-y) of the payload's hex x +0x18 and y +0x1A; the EXE's art id of the farm's name equal to the payload's +8 and not
// -1; then the EXE's poster on the queue object qg_channels + [qg_channel_index] * 0x800180, and the hex x and y as two 16-bit
// words into the payload it returns. The EXE's functions are called outside any __try (their faults stay the EXE's, as the
// builder's); only this file's reads of the payload and the index word are guarded. Returns the event line's word.
typedef void*(__fastcall* FgPosterFn)(void* queue, void* edx);
typedef uint32_t(__fastcall* FgPlotOnMapFn)(void* grid, void* edx, const int32_t* cube);   // the answer is al only
typedef int32_t(__fastcall* FgArtIdFn)(void* db, void* edx, const char* name);

static bool ReadQueueIndex(uint32_t* idx)
{
	__try { *idx = *(volatile const uint32_t*)g_w.queueIndex; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static const char* PostFarmEnd(const void* ev)
{
	if (g_postOff) { g_st.postSwitchedOff++; g_bPostOff++; return "skipped-server-post-switched-off"; }
	if (!g_postReady) { g_st.postRefused++; g_bPostRefused++; return "skipped-server-post-off-for-build"; }
	const FgEvent e = ReadEvent(ev);
	if (!e.ok) { g_st.postRefused++; g_bPostRefused++; return "skipped-server-post-refused-payload"; }
	if (!e.b1c) { g_st.postFailFlag++; g_bPostFail++; return "skipped-server-post-test-flag"; }
	const int32_t cube[3] = { e.x, e.y, -((int32_t)e.x + (int32_t)e.y) };
	if (!(((FgPlotOnMapFn)g_w.plotOnMap)((void*)g_w.plotGrid, nullptr, cube) & 0xFFu)) { g_st.postFailMap++; g_bPostFail++; return "skipped-server-post-test-map"; }
	const int32_t art = ((FgArtIdFn)g_w.artId)((void*)g_w.artDb, nullptr, (const char*)g_w.artName);
	if ((int32_t)e.d8 != art || art == -1) { g_st.postFailArt++; g_bPostFail++; return "skipped-server-post-test-art"; }
	uint32_t idx = 0;
	if (!ReadQueueIndex(&idx) || idx >= FG_QUEUE_CHANNELS) { g_st.postRefused++; g_bPostRefused++; return "skipped-server-post-refused-queue"; }
	int16_t* p = (int16_t*)((FgPosterFn)g_w.poster)((void*)(g_w.queueBase + (uintptr_t)idx * FG_QUEUE_STRIDE), nullptr);
	if (!p)
	{
		g_st.postRefused++; g_bPostRefused++;
		if (g_postNullLogged++ == 0) L("farm_guard: LOUD: the EXE's farm-end poster returned no record (queue channel %u): this farm event gets no building", idx);
		return "skipped-server-post-refused-null";
	}
	p[0] = e.x; p[1] = e.y;   // the builder's own two stores (the poster zeroed both)
	g_st.posts++; g_bPosts++;
	return "skipped-server-posted";
}

// ---- the hook: the builder's listener function while the guard is installed ----------------------------------------------
static void __fastcall FgHook(void* obj, void* edx, void* ev)
{
	(void)edx;
	g_st.calls++;
	g_lastCallQpc = Qpc();
	g_bCalls++;
	if (g_tripped) { g_st.skipTripped++; g_bSkipped++; EventLine(ev, "skipped-tripped"); return; }
	bool flagOk = false;
	const uint32_t flag = ReadViewFlag(&flagOk);
	if (flagOk && flag == 0) { g_st.skipNoView++; g_bSkipped++; EventLine(ev, "skipped-noview"); return; }   // the GameView group is shut down: a stale node
	if (StaleCheck()) { g_st.skipStale++; g_bSkipped++; EventLine(ev, "skipped-stale"); return; }           // the EXE's own registration calls the builder
	if (g_skipBuilder)   // ticket 4853: the server lays out the farm fields; ticket 0655: the builder's last step only
	{
		g_st.skipServer++; g_bSkipped++; g_bServer++;
		EventLine(ev, PostFarmEnd(ev));
		return;
	}
	if (const char* r2 = Rule2(ev)) { g_bSkipped++; g_bRule2++; EventLine(ev, r2); EventFlush(); return; }   // ticket 4855
	if (OwnerAlone())   // ticket 0535: no composite hands over its Presents: the in-game check before the burst's first call
	{
		const int64_t now = Qpc();
		const bool first = !g_lastBuiltQpc || QpcMs(now - g_lastBuiltQpc) >= g_quietMs;
		g_lastBuiltQpc = now;
		if (first && !PreCheck())
		{ g_st.skipTripped++; g_bSkipped++; EventLine(ev, "skipped-tripped"); EventFlush(); return; }
	}
	g_st.builderCalls++;
	EventLine(ev, "built");
	g_builder(obj, nullptr, ev);                                             // the EXE's builder, outside any __try
	const int64_t t0 = Qpc();
	FgCheck c;
	const bool read = FgCheckGraphSafe(g_w.graph, &c);
	const double us = QpcMs(Qpc() - t0) * 1000.0;
	g_st.checks++; g_bChecks++;
	g_st.checkMsTotal += us / 1000.0; g_bCheckMs += us / 1000.0;
	if (us > g_st.checkUsMax) g_st.checkUsMax = us;
	if (us > g_bCheckUsMax) g_bCheckUsMax = us;
	g_bLast = c;
	if (!read) { g_st.checkFaults++; g_bBad++; L("farm_guard: the check could not read the graph (fault) after builder call %u: no verdict", g_st.builderCalls); return; }
	if (!c.sound)
	{
		g_st.checksBad++; g_bBad++;
		char w[48]; _snprintf_s(w, _TRUNCATE, "after builder call %u", g_st.builderCalls);
		Trip(c, w);
		EventFlush();
	}
}

// ---- the Present: swaps, retirements, restores; the burst lines -----------------------------------------------------------
static void LogBurst(const char* why)
{
	if (!g_bCalls) return;
	g_loads++;
	EventFlush();
	L("farm_guard: load %u (grid seq %u, %s): builder calls %u (skipped %u), checks %u: %s; check time %.2f ms total, max %.0f us; "
	  "endpoints after the last check: live %u, size %u, freed %u, walked %u; event lines %u; not called for farm_builder=0 %u; "
	  "rule 2 skipped %u%s; farm-end posts %u (failed the builder's tests %u, refused %u, switched off %u)%s; edge (0657): placed at "
	  "the server's point %u, at the plot centre %u, held %u, holds run out %u, era curves %u",
	  g_loads, g_gridSeq, why, g_bCalls, g_bSkipped, g_bChecks,
	  g_tripped ? "BAD (the guard tripped: no further builder call in this process)" : g_bBad ? "a check could not read" : g_bChecks ? "all sound" : "none run",
	  g_bCheckMs, g_bCheckUsMax, g_bLast.live, g_bLast.size, g_bLast.freedByBit, g_bLast.chain, g_bEvLines, g_bServer, g_bRule2,
	  g_r2Stopped ? " (rule 2 (a) fired: no further builder call in this process)" : "",
	  g_bPosts, g_bPostFail, g_bPostRefused, g_bPostOff, g_bServer && !g_postReady ? " (the post is off for this build)" : "",
	  g_bEndPlaced, g_bEndCentre, g_bEndHeld, g_bEndExpired, g_bEraCurves);
	g_bPosts = g_bPostFail = g_bPostRefused = g_bPostOff = 0;   // ticket 0655
	g_bEndPlaced = g_bEndCentre = g_bEndHeld = g_bEndExpired = g_bEraCurves = 0;   // ticket 0657
	g_bCalls = g_bChecks = g_bBad = g_bSkipped = g_bEvLines = g_bServer = g_bRule2 = 0; g_bCheckMs = g_bCheckUsMax = 0; memset(&g_bLast, 0, sizeof(g_bLast));
}

// ticket 4854 (M3): the graph line after every in-game line and every burst line (read only; never trips the guard)
static void GraphLine(const char* where)
{
	FgGraphStat gs;
	if (!FgGraphScanSafe(g_w.graph, g_w.vtxVec, &gs)) g_st.graphFaults++;
	g_st.graphLines++;
	char b[1900];
	FgGraphLineText(gs, where, b, sizeof(b));
	// ticket 4855 (MC #5493): rule 2's geometry, radius and kind-0 count on an oversize grid, with the option at 0 or 1
	if (gs.cells > FG_CELL_NONE)
	{
		FgRule2Geo r;
		Rule2GeoSeh(&r);
		const char* state = g_r2Stopped ? "stopped by (a)" : g_skipBuilder ? "not acting (farm_builder=0)"
			: !g_r2Have ? "not set up yet (no builder call on this grid)" : !g_r2.ok ? "no geometry: every call skipped"
			: g_r2.radius == 2 ? "in force, radius 2" : "in force, radius 4";
		const size_t n = strlen(b);
		if (r.ok)
			_snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "; rule 2: plot (%d, %d), kind-0 nodes %u, touching cell 65,535 %u, radius %d; %s",
				r.px, r.py, r.kind0, r.kind0Touch, r.radius, state);
		else
			_snprintf_s(b + n, sizeof(b) - n, _TRUNCATE, "; rule 2: no geometry (%s); %s", r.why, state);
	}
	L("%s", b);
}

// 1 = taken; 0 = held by another; -1 = never initialised (nothing registered yet) or not readable
static int TakeLock()
{
	__try
	{
		if (!(*(volatile const uint8_t*)g_w.lockInit & 1)) return -1;
		return InterlockedCompareExchange((volatile LONG*)g_w.lock, 1, 0) == 0 ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void DropLock() { __try { InterlockedExchange((volatile LONG*)g_w.lock, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

static bool Cas(uintptr_t word, uintptr_t from, uintptr_t to)
{
	__try { return (uintptr_t)InterlockedCompareExchange((volatile LONG*)word, (LONG)to, (LONG)from) == from; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void GraphCounts(uint32_t* nodes, uint32_t* eps)
{
	*nodes = *eps = 0xFFFFFFFFu;
	uint32_t h[0x40];
	if (SafeCopy(g_w.graph + FG_EP_HDR, h, 0x3C)) { *eps = h[1]; *nodes = h[(FG_ND_HDR - FG_EP_HDR) / 4 + 1]; }
}

// ticket 0535: a read of the list without the lock (bounded, SEH): true when Maintain would write nothing, so the owner's
// watcher (every 2 ms) takes the EXE's lock only when a node needs a write. A list it cannot read takes the locked path.
static bool NothingToDo(int want)
{
	FgScan s;
	if (!ScanSafe(&s) || !s.ok) return false;
	const uintptr_t hook = HookWord();
	uint32_t nO = 0, nU = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_w.builder) nO++; else if (s.fn[i] == hook) nU++; }
	if (nO && nU) return false;
	return want ? nO == 0 : nU == 0;
}

static void Maintain(int want)
{
	if (NothingToDo(want)) return;
	const int lk = TakeLock();
	if (lk <= 0) { if (lk == 0) g_st.lockBusy++; return; }
	FgScan s;
	const bool read = ScanSafe(&s);
	if (!read || !s.ok)
	{
		DropLock();
		g_st.scanBad++;
		if (g_st.scanBad == 1 || g_st.scanBad - g_lastScanBadLog >= 1000)
		{
			g_lastScanBadLog = g_st.scanBad;
			L("farm_guard: LOUD: the listener list of type 59 is not as expected (%s; %u times): no node read or written", s.why, g_st.scanBad);
		}
		return;
	}
	const uintptr_t hook = HookWord(), inert = InertWord();
	uint32_t nO = 0, nU = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_w.builder) nO++; else if (s.fn[i] == hook) nU++; }
	char msg[3][320]; int nm = 0;
	if (nO && nU)   // the EXE registered the builder again while a swapped node of an earlier registration is still listed
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == hook && Cas(s.fnWord[i], hook, inert))
			{
				g_st.retired++;
				if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: node %u of an earlier registration retired (its unregister could not find it: the guard's function word); it returns at once from now on", s.idx[i]);
			}
	if (want)
	{
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == g_w.builder && Cas(s.fnWord[i], g_w.builder, hook))
			{
				g_st.swaps++;
				InterlockedIncrement(&g_swapGen);   // ticket 4855: a new registration = a new load: rule 2's radius is chosen again at its first call
				uint32_t nodes, eps; GraphCounts(&nodes, &eps);
				if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: installed by the %s: the builder's listener node %u (list of %u, channel %u) now calls the guard, "
					"which calls the builder (swap %u; graph at the swap: %u nodes, %u endpoints live)", g_ownerName, s.idx[i], s.count, s.channel, g_st.swaps, nodes, eps);
			}
		if (nO > 1) g_st.anomalies++;
	}
	else if (!nO && nU)
	{
		bool first = true;
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == hook && Cas(s.fnWord[i], hook, first ? g_w.builder : inert))
			{
				if (first) { g_st.restored++; if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: off (farm_guard=0): node %u holds the builder's own address again", s.idx[i]); }
				else g_st.retired++;
				first = false;
			}
	}
	DropLock();
	for (int i = 0; i < nm; ++i) L("%s", msg[i]);
	if (want && nO > 1) L("farm_guard: %u nodes of the builder in one list (the EXE registered it more than once); each calls the guard", nO);
}

// ---- ticket 0655 (P1): the diagnostic area probe -------------------------------------------------------------------------
// The builder object's area listener (type 33 of the A manager, fg_area_listener) with farm_area_probe=1: its node calls
// AreaProbe, which times each call. Calls are recorded in a ring (the main thread writes, an interlocked index; the jobs read
// behind it); per burst (2 s without a call) one line and the values in farm-area-calls.log. Owner or alone only.
static FgWorld g_aw;                          // its lock, list, listener (as `builder`) and object
static bool g_areaReady, g_areaWant;
static FgListenerFn g_areaFn;
#define FG_AREA_RING 65536u
static float g_areaUs[FG_AREA_RING];
static volatile LONG g_areaN;                 // calls recorded (ever)
static LONG g_areaDone;                       // calls already logged
static int64_t g_areaLastQpc;
static FILE* g_areaLog;
static bool g_areaLogTried;
static char g_areaWhy[256];

static void __fastcall AreaProbe(void* obj, void* edx, void* ev)
{
	const int64_t t0 = Qpc();
	g_areaFn(obj, edx, ev);                   // the EXE's listener, outside any __try
	const int64_t t1 = Qpc();
	const LONG k = InterlockedIncrement(&g_areaN) - 1;
	g_areaUs[(uint32_t)k % FG_AREA_RING] = (float)(QpcMs(t1 - t0) * 1000.0);
	g_areaLastQpc = t1;
}
static void __fastcall AreaInert(void* obj, void* edx, void* ev) { (void)obj; (void)edx; (void)ev; }
FgListenerFn FarmGuard_AreaProbe() { return AreaProbe; }
FgListenerFn FarmGuard_AreaInert() { return AreaInert; }

static int TakeLockOf(const FgWorld& w)
{
	__try
	{
		if (!(*(volatile const uint8_t*)w.lockInit & 1)) return -1;
		return InterlockedCompareExchange((volatile LONG*)w.lock, 1, 0) == 0 ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void DropLockOf(const FgWorld& w) { __try { InterlockedExchange((volatile LONG*)w.lock, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static bool AreaScanSafe(FgScan* s)
{
	__try { ScanListWith(g_aw, (uintptr_t)AreaProbe, (uintptr_t)AreaInert, s); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { s->ok = 0; _snprintf_s(s->why, _TRUNCATE, "the list's read faulted"); return false; }
}

// want: swap every node holding the listener to the probe (an older probe node beside a new registration retires to inert);
// not want: every probe node back to the listener (the first; more go inert).
static void AreaMaintain(bool want, bool silent = false)
{
	if (!g_areaReady) return;
	FgScan s;
	if (!AreaScanSafe(&s) || !s.ok) return;
	const uintptr_t probe = (uintptr_t)AreaProbe, inert = (uintptr_t)AreaInert;
	uint32_t nO = 0, nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_aw.builder) nO++; else if (s.fn[i] == probe) nP++; }
	if (want ? nO == 0 : nP == 0) return;
	const int lk = TakeLockOf(g_aw);
	if (lk <= 0) return;
	if (!AreaScanSafe(&s) || !s.ok) { DropLockOf(g_aw); return; }
	nO = nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_aw.builder) nO++; else if (s.fn[i] == probe) nP++; }
	char msg[200] = "";
	if (want)
	{
		if (nO && nP)
			for (uint32_t i = 0; i < s.n; ++i)
				if (s.fn[i] == probe && Cas(s.fnWord[i], probe, inert)) g_st.areaRetired++;
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == g_aw.builder && Cas(s.fnWord[i], g_aw.builder, probe))
			{
				g_st.areaSwaps++;
				_snprintf_s(msg, _TRUNCATE, "farm_guard: area probe (farm_area_probe=1, a DIAGNOSTIC of ticket 0655): node %u of type 33 "
					"(list of %u, channel %u) now times the builder object's area listener (swap %u)", s.idx[i], s.count, s.channel, g_st.areaSwaps);
			}
	}
	else
	{
		bool first = true;
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == probe && Cas(s.fnWord[i], probe, (first && !nO) ? g_aw.builder : inert))
			{
				if (first && !nO) { g_st.areaRestored++; _snprintf_s(msg, _TRUNCATE, "farm_guard: area probe off: node %u holds the area listener's own address again", s.idx[i]); }
				else g_st.areaRetired++;
				first = false;
			}
	}
	DropLockOf(g_aw);
	if (msg[0] && !silent) L("%s", msg);
}

static int CmpF(const void* a, const void* b) { const float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
// One line per burst of area calls (2 s without one), and every value in farm-area-calls.log.
static void AreaJobs()
{
	const LONG n = g_areaN;
	if (n == g_areaDone || !g_areaLastQpc || QpcMs(Qpc() - g_areaLastQpc) < g_quietMs) return;
	LONG from = g_areaDone;
	if (n - from > (LONG)FG_AREA_RING) from = n - (LONG)FG_AREA_RING;
	const uint32_t cnt = (uint32_t)(n - from);
	static float v[FG_AREA_RING];
	double sum = 0, mx = 0, first = 0, last = 0;
	const uint32_t k20 = cnt < 20 ? cnt : 20;
	if (!g_areaLogTried)
	{
		g_areaLogTried = true;
		if (g_recordDir[0])
		{
			wchar_t p[MAX_PATH];
			const size_t dl = wcslen(g_recordDir);
			_snwprintf_s(p, _TRUNCATE, L"%s%sfarm-area-calls.log", g_recordDir, (dl && g_recordDir[dl - 1] == L'\\') ? L"" : L"\\");
			g_areaLog = _wfsopen(p, L"ab", _SH_DENYNO);
			if (g_areaLog) fprintf(g_areaLog, "# ticket 0655 (P1): one line per call of the builder object's area listener: call, microseconds (process %lu)\r\n", GetCurrentProcessId());
		}
	}
	for (uint32_t i = 0; i < cnt; ++i)
	{
		const float us = g_areaUs[(uint32_t)(from + (LONG)i) % FG_AREA_RING];
		v[i] = us; sum += us; if (us > mx) mx = us;
		if (i < k20) first += us;
		if (i >= cnt - k20) last += us;
		if (g_areaLog) fprintf(g_areaLog, "%ld %.1f\r\n", (long)(from + (LONG)i + 1), us);
	}
	if (g_areaLog) fflush(g_areaLog);
	qsort(v, cnt, sizeof(float), CmpF);
	if (mx > g_st.areaUsMax) g_st.areaUsMax = mx;
	g_st.areaCalls = (uint32_t)n; g_st.areaLines++;
	L("farm_guard: area probe: %u calls of the builder object's area listener (calls %ld..%ld of this process): total %.2f ms, "
	  "mean %.1f us, median %.1f us, max %.1f us; the first %u mean %.1f us, the last %u mean %.1f us (ticket 0655 P1: flat = "
	  "no growth with the number of calls)", cnt, (long)from + 1, (long)n, sum / 1000.0, cnt ? sum / cnt : 0.0, cnt ? v[cnt / 2] : 0.0f,
	  mx, k20, k20 ? first / k20 : 0.0, k20, k20 ? last / k20 : 0.0);
	g_areaDone = n;
}

// ---- ticket 0657: the edge placement (farm_guard.h; cities-and-landmarks.md 2.6.2, 2.6.3) ------------------------------------
// Main thread: the two pass-throughs (the EXE calls them from its queue dispatches) and the hold table (cleared at a Present
// that sees a new camera grid). The owner's watcher thread: the node words (under each manager's lock) and the section's open.
static FgEdgeWorld g_ew;
static bool g_edgeReady, g_edgeOff, g_edgeTest;
static volatile bool g_edgeWant;              // ready, farm_edge on, farm_builder=0, the post's sites matched
static int g_edgeOffLogged = -1, g_edgeWantLogged = -1;
static char g_edgeWhy[256];
static FgListenerFn g_edgeEndFn, g_edgeEraFn; // the EXE's two listeners
static const VPFarmHeader* volatile g_farmSec;   // <base>.Farm (or a test block)
static HANDLE g_farmSecMap;
static DWORD g_farmSecTried;
static bool g_farmSecLogged;
static volatile LONG g_camValid, g_camSeq;    // the camera block's grid at the last Present
static LONG g_holdSeq = -1;                   // the grid seq the hold table belongs to
static double g_holdLoadMs = 20000.0, g_holdMissMs = 5000.0;
#define FG_HOLD_SLOTS 8192u
struct FgHold { uint32_t key; int64_t since; int why; };   // since 0 = empty slot, -1 = done (the key stays for its plot)
static FgHold g_hold[FG_HOLD_SLOTS];
static uint32_t g_holdCount;
// the transient curves (the patch's own table; entry k: the square of record k of one call)
static unsigned char* g_tcCurves;
static unsigned char* g_tcSegs;
static uint32_t g_tcStore[4];                 // the point store: +8 = the segment array
struct FgSaved { uintptr_t rec; uint8_t n; uint16_t id0; };
static FgSaved* g_tcSaved;
static uint32_t g_tcCap, g_tcWritten;
static bool g_tcSwapped;
static uint32_t g_tcOldTable;

static void __fastcall EdgeEnd(void* fm, void* edx, void* ev);
static void __fastcall EdgeEra(void* fm, void* edx, void* ev);
FgListenerFn FarmGuard_EdgeEnd() { return EdgeEnd; }
FgListenerFn FarmGuard_EdgeEra() { return EdgeEra; }
// the node words: the owner's stubs (they reach the EXE's listener while no owner is attached), else the functions
static uintptr_t EdgeWord(int i)
{
	if (g_role == 1 && g_shared && g_shared->edgeStub[i]) return g_shared->edgeStub[i];
	return i == 0 ? (uintptr_t)EdgeEnd : (uintptr_t)EdgeEra;
}

static uint32_t HoldKey(int hx, int hy) { return (uint32_t)(uint16_t)hx | ((uint32_t)(uint16_t)hy << 16); }
static FgHold* HoldFind(uint32_t key, bool create)
{
	uint32_t i = (key * 2654435761u) >> 19;   // 13 bits
	for (uint32_t k = 0; k < FG_HOLD_SLOTS; ++k, i = (i + 1) & (FG_HOLD_SLOTS - 1))
	{
		FgHold& h = g_hold[i];
		if (h.since == 0)
		{
			if (!create || g_holdCount >= FG_HOLD_SLOTS / 2) return nullptr;
			h.key = key; h.since = -1; h.why = 0; g_holdCount++;
			return &h;
		}
		if (h.key == key) return &h;
	}
	return nullptr;
}
static void HoldClear() { memset(g_hold, 0, sizeof(g_hold)); g_holdCount = 0; }
static void HoldDone(int hx, int hy) { FgHold* h = HoldFind(HoldKey(hx, hy), false); if (h) h->since = -1; }

// The section's view of one plot. -1 = the writer was busy (try again).
enum { FG_LK_NOSEC = 0, FG_LK_WAIT = 1, FG_LK_MISSING = 2, FG_LK_CENTRE = 3, FG_LK_POINT = 4 };
static int LookupOnce(const VPFarmHeader* s, int hx, int hy, float* wx, float* wy)
{
	__try
	{
		const uint32_t s1 = s->seq;
		if (s1 & 1u) return -1;
		MemoryBarrier();
		if (!s1 || s->magic != VPFARM_MAGIC || s->version != VPFARM_VERSION) return FG_LK_NOSEC;
		const uint32_t load = s->loadSeq, state = s->state, n = s->count;
		int r;
		if (!g_camValid || load != (uint32_t)g_camSeq || state != VPFARM_DONE) r = FG_LK_WAIT;
		else if (n > VPFARM_CAPACITY) r = FG_LK_NOSEC;
		else
		{
			const VPFarmEntry* e = VPFarmFind(VPFARM_ENTRIES(s), n, hx, hy);
			if (!e) r = FG_LK_MISSING;
			else if (e->kind == VPFARM_KIND_POINT) { *wx = e->wx; *wy = e->wy; r = FG_LK_POINT; }
			else r = FG_LK_CENTRE;
		}
		MemoryBarrier();
		return s->seq == s1 ? r : -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return FG_LK_NOSEC; }
}
static int EdgeLookup(int hx, int hy, float* wx, float* wy)
{
	const VPFarmHeader* s = g_farmSec;
	if (!s) return FG_LK_NOSEC;
	for (int t = 0; t < 16; ++t)
	{
		const int r = LookupOnce(s, hx, hy, wx, wy);
		if (r >= 0) return r;
		YieldProcessor();
	}
	return FG_LK_WAIT;   // the server is writing: held, tried again at the next pump
}

// The farm end again on the queue (the EXE's poster, as PostFarmEnd's last step): pumped the next time.
static bool Repost(int16_t hx, int16_t hy)
{
	if (!g_postReady || !g_w.poster) return false;
	uint32_t idx = 0;
	if (!ReadQueueIndex(&idx) || idx >= FG_QUEUE_CHANNELS) return false;
	int16_t* p = (int16_t*)((FgPosterFn)g_w.poster)((void*)(g_w.queueBase + (uintptr_t)idx * FG_QUEUE_STRIDE), nullptr);
	if (!p) return false;
	p[0] = hx; p[1] = hy;
	return true;
}

// true = held (posted again; the EXE's listener is not called now); false = call it now (the hold ran out or failed)
static bool Hold(int16_t hx, int16_t hy, int why)
{
	const int64_t now = Qpc();
	FgHold* h = HoldFind(HoldKey(hx, hy), true);
	if (!h) return false;
	if (h->since <= 0 || h->why != why) { h->since = now; h->why = why; }
	const double lim = why == FG_LK_MISSING ? g_holdMissMs : g_holdLoadMs;
	if (QpcMs(now - h->since) >= lim)
	{
		h->since = -1;
		g_st.endExpired++; g_bEndExpired++;
		return false;
	}
	if (!Repost(hx, hy)) { h->since = -1; g_st.endRepostFail++; return false; }
	return true;
}

static bool ReadEvHex(const void* ev, int16_t* x, int16_t* y)
{
	__try { *x = ((const int16_t*)ev)[0]; *y = ((const int16_t*)ev)[1]; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int ReadDefer(uintptr_t fm)
{
	__try { return (*(volatile const uint8_t*)(fm + FG_FM_DEFER) & 1) ? 1 : 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// A node of `w`'s list holds the EXE's own listener: this call came through a node of an earlier registration (F1's rule).
static bool EdgeStale(const FgWorld& w, uintptr_t hook)
{
	FgScan s;
	__try { ScanListWith(w, hook, InertWord(), &s); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	if (!s.ok) return false;
	for (uint32_t i = 0; i < s.n; ++i) if (s.fn[i] == w.builder) return true;
	return false;
}

static bool EnsureCap(uint32_t n)
{
	if (n <= g_tcCap) return true;
	const uint32_t cap = n + n / 4 + 64;
	unsigned char* c = (unsigned char*)realloc(g_tcCurves, (size_t)cap * FG_CURVE_BYTES);
	if (!c) return false;
	g_tcCurves = c;
	unsigned char* s = (unsigned char*)realloc(g_tcSegs, (size_t)cap * FG_SEG_BYTES);
	if (!s) return false;
	g_tcSegs = s;
	FgSaved* v = (FgSaved*)realloc(g_tcSaved, (size_t)cap * sizeof(FgSaved));
	if (!v) return false;
	g_tcSaved = v;
	g_tcCap = cap;
	return true;
}

// Curve k: one segment k of four corners, the square of side 2 x FG_EDGE_HALF centred on (x, y), counter-clockwise.
static void MakeCurve(uint32_t k, float x, float y)
{
	unsigned char* c = g_tcCurves + (size_t)k * FG_CURVE_BYTES;
	memset(c, 0, FG_CURVE_BYTES);
	*(uint32_t*)(c + 0x08) = (uint32_t)(uintptr_t)g_tcStore;
	*(uint32_t*)(c + 0x10) = k;               // the first segment (low 24 bits)
	*(uint32_t*)(c + 0x14) = k;               // the last segment
	*(uint32_t*)(c + 0x1C) = 0xFFFFFFFFu;     // no field id
	*(uint32_t*)(c + 0x24) = 0xFFFFFFFFu;     // no decal handles
	*(uint32_t*)(c + 0x28) = 0xFFFFFFFFu;
	unsigned char* s = g_tcSegs + (size_t)k * FG_SEG_BYTES;
	memset(s, 0, FG_SEG_BYTES);
	*(uint32_t*)(s + 0x0) = FG_NIL;           // previous
	*(uint32_t*)(s + 0x4) = FG_NIL;           // next
	*(uint32_t*)(s + 0x8) = 4u;               // points
	float* p = (float*)(s + 0xC);
	const float h = FG_EDGE_HALF;
	p[0] = x - h; p[1] = y - h; p[2] = x + h; p[3] = y - h; p[4] = x + h; p[5] = y + h; p[6] = x - h; p[7] = y + h;
}

static bool ReadRecs(uintptr_t fm, uintptr_t* recs, uint32_t* n)
{
	__try { *recs = *(const uint32_t*)(fm + FG_FM_RECS); *n = *(const uint32_t*)(fm + FG_FM_RECS + 4); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The writes of an install, in order: each record's count and first id, then the manager's table word. A fault leaves
// g_tcWritten / g_tcSwapped where it stopped, for Restore.
static bool WriteInstall(uintptr_t fm, uint32_t k)
{
	__try
	{
		for (g_tcWritten = 0; g_tcWritten < k; ++g_tcWritten)
		{
			const uintptr_t r = g_tcSaved[g_tcWritten].rec;
			*(volatile uint8_t*)(r + FG_REC_NCURVES) = 1;
			*(volatile uint16_t*)(r + FG_REC_IDS) = (uint16_t)g_tcWritten;
		}
		g_tcOldTable = *(volatile const uint32_t*)(fm + FG_FM_CURVES);
		*(volatile uint32_t*)(fm + FG_FM_CURVES) = (uint32_t)(uintptr_t)g_tcCurves;
		g_tcSwapped = true;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Puts back what WriteInstall wrote (the table word first). False when a write faulted.
static bool Restore(uintptr_t fm)
{
	bool ok = true;
	__try
	{
		if (g_tcSwapped) *(volatile uint32_t*)(fm + FG_FM_CURVES) = g_tcOldTable;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
	g_tcSwapped = false;
	for (uint32_t j = 0; j < g_tcWritten; ++j)
	{
		__try
		{
			const uintptr_t r = g_tcSaved[j].rec;
			*(volatile uint16_t*)(r + FG_REC_IDS) = g_tcSaved[j].id0;
			*(volatile uint8_t*)(r + FG_REC_NCURVES) = g_tcSaved[j].n;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
	}
	g_tcWritten = 0;
	return ok;
}

// The records to give a curve. mode 0 (the farm end): the live records of (hx, hy), point (wx, wy). mode 1 (the era): every
// live record with an instance whose plot has a server point of this load. Returns how many got one (0: none, the EXE's
// listener runs as before), -1 after a fault (nothing left installed).
static int Install(uintptr_t fm, int mode, int16_t hx, int16_t hy, float wx, float wy)
{
	uintptr_t recs = 0;
	uint32_t n = 0;
	if (!ReadRecs(fm, &recs, &n)) return -1;
	if (!n) return 0;
	if (n > 1000000u || !recs) return -1;
	if (!EnsureCap(mode == 0 ? 64u : n)) return 0;
	g_tcStore[2] = (uint32_t)(uintptr_t)g_tcSegs;
	uint32_t k = 0;
	for (uint32_t i = 0; i < n && k < g_tcCap; ++i)
	{
		unsigned char r[0x24];
		const uintptr_t a = recs + (uintptr_t)i * FG_REC_BYTES;
		if (!SafeCopy(a, r, sizeof(r))) return -1;
		uint32_t flags; memcpy(&flags, r + FG_REC_FLAGS, 4);
		if (flags & 0x400u) continue;   // freed
		int16_t rx, ry; memcpy(&rx, r + FG_REC_HEX, 2); memcpy(&ry, r + FG_REC_HEX + 2, 2);
		float px = wx, py = wy;
		if (mode == 0) { if (rx != hx || ry != hy) continue; }
		else
		{
			if (!(flags & 0x100u)) continue;   // the era listener re-places only records with an instance
			if (EdgeLookup(rx, ry, &px, &py) != FG_LK_POINT) continue;
		}
		MakeCurve(k, px, py);
		g_tcSaved[k].rec = a;
		g_tcSaved[k].n = r[FG_REC_NCURVES];
		memcpy(&g_tcSaved[k].id0, r + FG_REC_IDS, 2);
		k++;
	}
	if (!k) return 0;
	g_tcWritten = 0; g_tcSwapped = false;
	if (!WriteInstall(fm, k)) { Restore(fm); return -1; }
	return (int)k;
}

// After a farm end with a point: where the records of the call stand now (the placer wrote +0x14 / +0x18).
static void CheckPlaced(uint32_t k, float wx, float wy)
{
	for (uint32_t j = 0; j < k; ++j)
	{
		float p[2];
		if (!SafeCopy(g_tcSaved[j].rec + FG_REC_POS, p, sizeof(p))) continue;
		const double d = sqrt((double)(p[0] - wx) * (p[0] - wx) + (double)(p[1] - wy) * (p[1] - wy));
		g_st.endChecked++;
		if (d > 1.0) g_st.endOff1++;
		if (d > g_st.endErrMax) g_st.endErrMax = d;
	}
}

static void __fastcall EdgeEnd(void* fm, void* edx, void* ev)
{
	(void)edx;
	g_st.endCalls++;
	if (!g_edgeWant) { g_edgeEndFn(fm, nullptr, ev); return; }
	bool vfOk = false;
	const uint32_t vf = ReadViewFlag(&vfOk);
	if (vfOk && vf == 0) { g_st.endDropped++; return; }                     // the GameView is shut down: as F1's skip
	if (EdgeStale(g_ew.end, EdgeWord(0))) { g_st.endStale++; return; }      // the EXE's own node calls the listener
	int16_t hx = 0, hy = 0;
	const int defer = ReadEvHex(ev, &hx, &hy) ? ReadDefer((uintptr_t)fm) : -1;
	if (defer < 0) { g_st.edgeFaults++; g_edgeEndFn(fm, nullptr, ev); return; }
	float wx = 0, wy = 0;
	const int lk = defer ? FG_LK_WAIT : EdgeLookup(hx, hy, &wx, &wy);
	if (defer || lk == FG_LK_WAIT || lk == FG_LK_MISSING)
	{
		if (Hold(hx, hy, defer ? 5 : lk))
		{
			if (defer) g_st.endHeldDefer++; else g_st.endHeld++;
			g_bEndHeld++;
			return;
		}
		g_st.endCentre++; g_bEndCentre++;
		g_edgeEndFn(fm, nullptr, ev);   // the hold ran out: the plot centre, as before 0657
		return;
	}
	HoldDone(hx, hy);
	if (lk != FG_LK_POINT)
	{
		if (lk == FG_LK_NOSEC) g_st.endNoSection++; else g_st.endCentre++;
		g_bEndCentre++;
		g_edgeEndFn(fm, nullptr, ev);
		return;
	}
	const int k = Install((uintptr_t)fm, 0, hx, hy, wx, wy);
	if (k <= 0)
	{
		if (k < 0) g_st.edgeFaults++;
		g_st.endCentre++; g_bEndCentre++;
		g_edgeEndFn(fm, nullptr, ev);
		return;
	}
	g_edgeEndFn(fm, nullptr, ev);   // the EXE's listener, outside any __try: its placer reads the transient curve
	if (!Restore((uintptr_t)fm)) g_st.edgeFaults++;
	g_st.endPlaced++; g_bEndPlaced++;
	CheckPlaced((uint32_t)k, wx, wy);
}

static void __fastcall EdgeEra(void* fm, void* edx, void* ev)
{
	(void)edx;
	g_st.eraCalls++;
	if (!g_edgeWant) { g_edgeEraFn(fm, nullptr, ev); return; }
	bool vfOk = false;
	const uint32_t vf = ReadViewFlag(&vfOk);
	if (vfOk && vf == 0) { g_st.eraDropped++; return; }
	if (EdgeStale(g_ew.era, EdgeWord(1))) { g_st.eraStale++; return; }
	const int defer = ReadDefer((uintptr_t)fm);
	if (defer != 0)
	{   // the listener queues itself (its later direct call places at the plot centre); a fault: as before 0657
		if (defer > 0) g_st.eraDeferred++; else g_st.edgeFaults++;
		g_edgeEraFn(fm, nullptr, ev);
		return;
	}
	const int k = Install((uintptr_t)fm, 1, 0, 0, 0.0f, 0.0f);
	if (k < 0) g_st.edgeFaults++;
	g_edgeEraFn(fm, nullptr, ev);   // the EXE's listener, outside any __try
	if (k > 0)
	{
		if (!Restore((uintptr_t)fm)) g_st.edgeFaults++;
		g_st.eraCurves += (uint32_t)k; g_bEraCurves += (uint32_t)k;
	}
}

// The node words of one list: want = every node holding the EXE's listener gets `hook` (a node of an earlier registration
// beside a new one is retired to the inert word); not want = the hook back to the listener (more go inert).
static void EdgeMaintain(const FgWorld& w, uintptr_t hook, bool want, const char* what)
{
	FgScan s;
	bool read = false;
	__try { ScanListWith(w, hook, InertWord(), &s); read = true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
	if (!read || !s.ok) return;
	uint32_t nO = 0, nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == w.builder) nO++; else if (s.fn[i] == hook) nP++; }
	if (want ? nO == 0 : nP == 0) return;
	const int lk = TakeLockOf(w);
	if (lk <= 0) return;
	read = false;
	__try { ScanListWith(w, hook, InertWord(), &s); read = true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
	if (!read || !s.ok) { DropLockOf(w); return; }
	nO = nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == w.builder) nO++; else if (s.fn[i] == hook) nP++; }
	char msg[3][240]; int nm = 0;
	if (want)
	{
		if (nO && nP)
			for (uint32_t i = 0; i < s.n; ++i)
				if (s.fn[i] == hook && Cas(s.fnWord[i], hook, InertWord()))
				{
					g_st.edgeRetired++;
					if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: edge (ticket 0657): %s node %u of an earlier registration retired", what, s.idx[i]);
				}
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == w.builder && Cas(s.fnWord[i], w.builder, hook))
			{
				g_st.edgeSwaps++;
				if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: edge (ticket 0657): the farm manager's %s listener node %u (list of %u, channel %u, "
					"object 0x%08X) now calls the pass-through (swap %u)", what, s.idx[i], s.count, s.channel, (unsigned)*(const uint32_t*)(s.fnWord[i] - 4), g_st.edgeSwaps);
			}
	}
	else
	{
		bool first = true;
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == hook && Cas(s.fnWord[i], hook, (first && !nO) ? w.builder : InertWord()))
			{
				if (first && !nO) { g_st.edgeRestored++; if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: edge off: the %s node %u holds the EXE's listener again", what, s.idx[i]); }
				else g_st.edgeRetired++;
				first = false;
			}
	}
	DropLockOf(w);
	for (int i = 0; i < nm; ++i) L("%s", msg[i]);
}

// The watcher (or a composite alone): <VPSHELL_SECTION>.Farm, opened read-only (never created) once a second until it is there.
static void EdgeOpenSection()
{
	if (g_farmSec || g_edgeTest) return;
	const DWORD now = GetTickCount();
	if (g_farmSecTried && now - g_farmSecTried < 1000) return;
	g_farmSecTried = now ? now : 1;
	wchar_t base[256];
	const DWORD n = GetEnvironmentVariableW(L"VPSHELL_SECTION", base, 256);
	if (n == 0 || n >= 256)
	{
		if (!g_farmSecLogged) { g_farmSecLogged = true; L("farm_guard: LOUD: edge (ticket 0657): VPSHELL_SECTION is not set: no server points, the farm buildings stand at their plot centres"); }
		return;
	}
	wchar_t name[300];
	_snwprintf_s(name, _TRUNCATE, L"%s%s", base, VPFARM_SECTION_SUFFIX);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	if (!h)
	{
		if (!g_farmSecLogged) { g_farmSecLogged = true; L("farm_guard: edge (ticket 0657): %S is not there yet (error %lu): tried again every second; until then the farm buildings stand at their plot centres", name, GetLastError()); }
		return;
	}
	const void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, VPFARM_SECTION_BYTES);
	if (!v) { CloseHandle(h); return; }
	g_farmSecMap = h;
	InterlockedExchangePointer((PVOID volatile*)&g_farmSec, (PVOID)v);
	g_st.edgeSection = 1;
	L("farm_guard: edge (ticket 0657): the server's farm points %S are mapped: farm ends are held until this load's points are there "
	  "(at most %.0f s, %.0f s for a plot missing from a finished list), then each building is placed at its point", name, g_holdLoadMs / 1000.0,
	  g_holdMissMs / 1000.0);
}

// ---- ticket 0784 (D268): the tread filter (farm_guard.h; farm-builder-guard.md 15) ---------------------------------------
// Main thread: the pass-through (the EXE's A-queue pump calls it through the node). The owner's watcher thread (or a composite
// alone, at its Present): the node word under the A manager's lock, the "treads" line.
static FgWorld g_tw;                          // its lock, list (slot 0x148), listener (as `builder`), the main Particle object
static bool g_treadReady;
static volatile bool g_treadWant;             // ready, tread_filter=1, not a follower
static int g_treadWantLogged = -1;
static FgListenerFn g_treadFn;                // the EXE's type-0x28 listener
static uint32_t g_treadHash[2];               // the two tread names' hashes (FgEffectNameHash)
static char g_treadWhy[256];
static uint32_t g_treadLineDropped;           // dropped counts at the last "treads" line
static DWORD g_treadLineTick;

static void __fastcall TreadPass(void* obj, void* edx, void* ev);
FgListenerFn FarmGuard_TreadPass() { return TreadPass; }
static uintptr_t TreadWord()
{
	if (g_role == 1 && g_shared && g_shared->treadStub) return g_shared->treadStub;
	return (uintptr_t)TreadPass;
}

// 0 = pass, 1 / 2 = drop (the first / second tread name, no VP64 tag), 3 = a tread record of the shell's effect player
// (passed), -1 = the payload could not be read (passed). Its own function: the EXE's listener is never called inside a __try.
static int TreadClass(const void* ev)
{
	__try
	{
		const uint8_t* p = (const uint8_t*)ev;
		const uint32_t h = *(const uint32_t*)(p + FG_EV_HASH);
		const int k = h == g_treadHash[0] ? 1 : h == g_treadHash[1] ? 2 : 0;
		if (!k) return 0;
		return *(const uint32_t*)(p + FG_EV_KEY_TAG) == FG_VP64_KEY_TAG ? 3 : k;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void __fastcall TreadPass(void* obj, void* edx, void* ev)
{
	g_st.treadCalls++;
	if (!g_treadWant) { g_st.treadOff++; g_treadFn(obj, edx, ev); return; }
	const int c = TreadClass(ev);
	if (c == 1) { g_st.treadDropped0++; return; }
	if (c == 2) { g_st.treadDropped1++; return; }
	if (c == 3) g_st.treadTagged++;
	else if (c < 0) g_st.treadFaults++;
	g_treadFn(obj, edx, ev);   // every record not dropped: the EXE's listener, same object and argument, outside any __try
}

// want: every node of the main Particle object holding the listener gets the pass-through's word (one of an earlier
// registration beside a new one is retired to the inert word); not want: the word back to the listener (more go inert).
static void TreadMaintain(bool want, bool silent = false)
{
	if (!g_treadReady) return;
	const uintptr_t hook = TreadWord();
	FgScan s;
	bool read = false;
	__try { ScanListWith(g_tw, hook, InertWord(), &s); read = true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
	if (!read || !s.ok) return;
	uint32_t nO = 0, nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_tw.builder) nO++; else if (s.fn[i] == hook) nP++; }
	if (want ? nO == 0 : nP == 0) return;
	const int lk = TakeLockOf(g_tw);
	if (lk <= 0) return;
	read = false;
	__try { ScanListWith(g_tw, hook, InertWord(), &s); read = true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
	if (!read || !s.ok) { DropLockOf(g_tw); return; }
	nO = nP = 0;
	for (uint32_t i = 0; i < s.n; ++i) { if (s.fn[i] == g_tw.builder) nO++; else if (s.fn[i] == hook) nP++; }
	char msg[3][260]; int nm = 0;
	if (want)
	{
		if (nO && nP)
			for (uint32_t i = 0; i < s.n; ++i)
				if (s.fn[i] == hook && Cas(s.fnWord[i], hook, InertWord()))
				{
					g_st.treadRetired++;
					if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: treads (ticket 0784): node %u of an earlier registration retired", s.idx[i]);
				}
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == g_tw.builder && Cas(s.fnWord[i], g_tw.builder, hook))
			{
				g_st.treadSwaps++;
				if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: treads (ticket 0784): the main Particle object's type-0x28 listener node %u (list of %u, "
					"channel %u, object 0x%08X) now calls the tread filter: the two landship tread effects are dropped (swap %u)", s.idx[i], s.count, s.channel,
					(unsigned)g_tw.builderObj, g_st.treadSwaps);
			}
	}
	else
	{
		bool first = true;
		for (uint32_t i = 0; i < s.n; ++i)
			if (s.fn[i] == hook && Cas(s.fnWord[i], hook, (first && !nO) ? g_tw.builder : InertWord()))
			{
				if (first && !nO) { g_st.treadRestored++; if (nm < 3) _snprintf_s(msg[nm++], _TRUNCATE, "farm_guard: treads (ticket 0784) off: node %u holds the EXE's listener again", s.idx[i]); }
				else g_st.treadRetired++;
				first = false;
			}
	}
	DropLockOf(g_tw);
	if (!silent)
		for (int i = 0; i < nm; ++i) L("%s", msg[i]);
}

// One line when the dropped count moved, at most every 30 s (a landship walking in view): the live check reads it.
static void TreadJobs()
{
	const uint32_t d = g_st.treadDropped0 + g_st.treadDropped1;
	if (d == g_treadLineDropped) return;
	const DWORD now = GetTickCount();
	if (g_treadLineTick && now - g_treadLineTick < 30000u) return;
	g_treadLineTick = now ? now : 1;
	g_treadLineDropped = d;
	g_st.treadLines++;
	L("farm_guard: treads (ticket 0784): %u type-0x28 records through the filter in this process: dropped %u (%s %u, %s %u), "
	  "passed %u (a tread record of the shell's effect player %u, unreadable %u, while switching off %u)", g_st.treadCalls, d,
	  FG_TREAD_NAME0, g_st.treadDropped0, FG_TREAD_NAME1, g_st.treadDropped1, g_st.treadCalls - d, g_st.treadTagged,
	  g_st.treadFaults, g_st.treadOff);
}

// The EXE check, once: the composite's validation (or the proxy's time stamp check) and the 13 patterns. False while the
// guard is not ready (not validated yet, refused, a test without FarmGuard_TestUse).
static bool ReadyCheck(const FarmGuardEnv& env)
{
	if (g_dead) return false;
	if (g_ready) return true;
	if (g_test) return false;
	if (env.exeValidated == 0 || !env.exeBase) return false;
	if (env.exeValidated < 0)
	{
		if (!g_validLogged) { g_validLogged = true; L("farm_guard: off: the %s did not validate the EXE (another build): the builder is not guarded", g_ownerName); }
		return false;
	}
	char why[256];
	int build = kVpExeBuildUnknown;
	if (!FgCheckPatterns(env.exeBase, why, sizeof(why), &build) || !FgWorldFromBase(build, env.exeBase, &g_w))
	{
		g_dead = true; g_st.dead = 1;
		L("farm_guard: LOUD: REFUSED: %s - the farm builder is not guarded in this process (nothing read or written)", why);
		return false;
	}
	g_builder = (FgListenerFn)g_w.builder;
	g_ready = true; g_st.ready = 1;
	// ticket 0655: the farm-end post's own sites; a mismatch turns only the post off (the guard and the skip run as before)
	{
		char pwhy[256] = "";
		g_postReady = FgPostCheckPatterns(env.exeBase, build, pwhy, sizeof(pwhy)) && FgPostWorldFromBase(build, env.exeBase, &g_w);
		g_st.postReady = g_postReady ? 1 : 0;
		if (g_postReady)
			L("farm_guard: the farm-end post (ticket 0655): %d byte patterns match (%s): with farm_builder=0 every event that passes "
			  "the builder's three tests gets the builder's last step, the EXE's farm-end post, and nothing else of the builder",
			  FG_NPOSTSIGS, VpExeBuildLabel(build));
		else
			L("farm_guard: LOUD: the farm-end post is OFF on %s: %s - with farm_builder=0 no farm building is made in this process "
			  "(the guard and the skip run as before)", VpExeBuildLabel(build), pwhy[0] ? pwhy : "its sites could not be read");
	}
	// ticket 0655 (P1): the diagnostic area probe's sites (used only with farm_area_probe=1)
	{
		char awhy[256] = "";
		g_areaReady = FgAreaCheckPatterns(env.exeBase, build, awhy, sizeof(awhy)) && FgAreaWorldFromBase(build, env.exeBase, &g_aw);
		g_st.areaReady = g_areaReady ? 1 : 0;
		if (g_areaReady) g_areaFn = (FgListenerFn)g_aw.builder;
		else _snprintf_s(g_areaWhy, _TRUNCATE, "%s", awhy);
	}
	// ticket 0657: the edge placement's sites; a mismatch leaves the buildings at their plot centres (as 0655), nothing else
	{
		char ewhy[256] = "";
		g_edgeReady = FgEdgeCheckPatterns(env.exeBase, build, ewhy, sizeof(ewhy)) && FgEdgeWorldFromBase(build, env.exeBase, &g_ew);
		g_st.edgeReady = g_edgeReady ? 1 : 0;
		if (g_edgeReady)
		{
			g_edgeEndFn = (FgListenerFn)g_ew.end.builder;
			g_edgeEraFn = (FgListenerFn)g_ew.era.builder;
			L("farm_guard: the edge placement (ticket 0657): %d byte patterns match (%s): with farm_builder=0 the farm manager's farm-end "
			  "and era listeners get a pass-through that hands the EXE's placer the server's point of each farm",
			  FG_NEDGESIGS, VpExeBuildLabel(build));
		}
		else
		{
			_snprintf_s(g_edgeWhy, _TRUNCATE, "%s", ewhy[0] ? ewhy : "its sites could not be read");
			L("farm_guard: LOUD: the edge placement (ticket 0657) is OFF on %s: %s - with farm_builder=0 the farm buildings stand at "
			  "their plot centres (the guard, the skip and the farm-end post run as before)", VpExeBuildLabel(build), g_edgeWhy);
		}
	}
	// ticket 0784: the tread filter's sites; a mismatch leaves the EXE's tread effect drawn, nothing else
	{
		char twhy[256] = "";
		g_treadReady = FgTreadCheckPatterns(env.exeBase, build, twhy, sizeof(twhy)) && FgTreadWorldFromBase(build, env.exeBase, &g_tw);
		g_st.treadReady = g_treadReady ? 1 : 0;
		g_treadHash[0] = FgEffectNameHash(FG_TREAD_NAME0);
		g_treadHash[1] = FgEffectNameHash(FG_TREAD_NAME1);
		if (g_treadReady)
		{
			g_treadFn = (FgListenerFn)g_tw.builder;
			L("farm_guard: the tread filter (ticket 0784): %d byte patterns match (%s): with tread_filter=1 the main Particle object's "
			  "type-0x28 listener gets a pass-through that drops %s (0x%08X) and %s (0x%08X) unless the shell's effect player posted them",
			  FG_NTREADSIGS, VpExeBuildLabel(build), FG_TREAD_NAME0, g_treadHash[0], FG_TREAD_NAME1, g_treadHash[1]);
		}
		else
		{
			_snprintf_s(g_treadWhy, _TRUNCATE, "%s", twhy[0] ? twhy : "its sites could not be read");
			L("farm_guard: LOUD: the tread filter (ticket 0784) is OFF on %s: %s - the landship's tread effect is drawn by the game "
			  "(with ServerUnits = 1 it can part from the server's hull in a turn); everything else runs as before",
			  VpExeBuildLabel(build), g_treadWhy);
		}
	}
	ChooseRecordDir(env.outDir);
	char rd[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, g_recordDir, -1, rd, (int)sizeof(rd), nullptr, nullptr);
	L("farm_guard: %d byte patterns match (%s); the guard watches type 59's listener list (builder 0x%08X, object 0x%08X, graph 0x%08X); "
	  "farm-guard.log would go to %s (%s); owner: the %s%s", FG_NSIGS, VpExeBuildLabel(build), (unsigned)g_w.builder, (unsigned)g_w.builderObj, (unsigned)g_w.graph,
	  rd[0] ? rd : "(nowhere)", g_recordInRun ? "the proxy's run folder of this process" : "not the run folder: an error report would not carry it",
	  g_ownerName, g_role == 1 ? " (ticket 0535: one owner per process; a composite beside it installs nothing)" : " (no proxy guard in this process)");
	return true;
}

// The Present's jobs: the burst line (after 2 s without a call) with its graph line, the in-game line once per load. With
// mainThread false (the owner's watcher thread) only the burst line: the graph is never read off the main thread.
static void PresentJobs(uint32_t gridValid, uint32_t gridSeq, bool mainThread)
{
	if (gridValid && gridSeq != g_gridSeq) g_gridSeq = gridSeq;
	if (mainThread)
	{   // ticket 0657: the camera's grid as the edge's lookups compare it; a new grid seq starts the hold table again
		InterlockedExchange(&g_camValid, (LONG)gridValid);
		InterlockedExchange(&g_camSeq, (LONG)gridSeq);
		if ((LONG)gridSeq != g_holdSeq) { g_holdSeq = (LONG)gridSeq; HoldClear(); }
	}
	if (g_st.areaSwaps) AreaJobs();   // ticket 0655 (P1)
	if (g_treadReady) TreadJobs();   // ticket 0784 (a line only when the dropped count moved)
	const int64_t now = Qpc();
	if (g_bCalls && QpcMs(now - g_lastCallQpc) >= g_quietMs)
	{
		LogBurst(mainThread ? "2 s without a call" : "2 s without a call; no composite: no graph line here");
		if (mainThread)
		{
			char w[64]; _snprintf_s(w, _TRUNCATE, "after load %u, grid seq %u", g_loads, g_gridSeq);
			GraphLine(w);   // ticket 4854
		}
	}
	if (!mainThread) return;
	// every load: the check once at a Present (the main thread, between frames) g_inGameMs after its map came up, also
	// when no builder call came through the guard (a map without farms, or a node not swapped in time)
	if (gridValid && gridSeq != g_seqSeen) { g_seqSeen = gridSeq; g_seqQpc = now; }
	if (gridValid && g_seqSeen && g_seqDone != g_seqSeen && QpcMs(now - g_seqQpc) >= g_inGameMs
		&& (!g_lastCallQpc || QpcMs(now - g_lastCallQpc) >= g_quietMs))
	{
		g_seqDone = g_seqSeen;
		if (g_bCalls) LogBurst("in game");
		EventFlush();
		FgCheck c;
		const bool read = FgCheckGraphSafe(g_w.graph, &c);
		L("farm_guard: in game (grid seq %u): builder calls through the guard %u since the last in-game line (%u in this "
		  "process, skipped %u); the check at this Present: %s%s%s; endpoints live %u, size %u, freed %u, walked %u%s; "
		  "not called for farm_builder=0: %u in this process; rule 2 skipped %u in this process (near %u, after (a) %u, no geometry %u); "
		  "farm-end posts %u in this process (failed the flag / plot / art test %u / %u / %u, refused %u, switched off %u; post %s); edge (0657) %s: farm ends %u, placed %u (read back %u, more than 1 unit off %u, worst %.3f), centre %u, held %u + %u deferred, run out %u, re-post failed %u, no section %u, dropped %u, stale %u; era events %u, curves %u, deferred %u; faults %u; "
		  "treads (0784) %s: type-0x28 records %u, dropped %u (%u + %u), passed %u (effect player's treads %u, unreadable %u)",
		  g_seqSeen, g_st.builderCalls - g_callsAtInGame, g_st.builderCalls,
		  g_st.skipTripped + g_st.skipNoView + g_st.skipStale + g_st.skipServer + g_st.skipRule2Near + g_st.skipRule2Stop + g_st.skipRule2Geo,
		  !read ? "could not read" : c.sound ? "sound" : "BAD", c.sound ? "" : ": ", c.sound ? "" : c.why, c.live, c.size,
		  c.freedByBit, c.chain, g_tripped ? " (the guard tripped earlier: no builder call)" : "", g_st.skipServer,
		  g_st.skipRule2Near + g_st.skipRule2Stop + g_st.skipRule2Geo, g_st.skipRule2Near, g_st.skipRule2Stop, g_st.skipRule2Geo,
		  g_st.posts, g_st.postFailFlag, g_st.postFailMap, g_st.postFailArt, g_st.postRefused, g_st.postSwitchedOff,
		  !g_postReady ? "off for this build" : g_postOff ? "switched off" : "on",
		  !g_edgeReady ? "off for this build" : g_edgeWant ? (g_farmSec ? "on" : "on, no section yet") : "off", g_st.endCalls,
		  g_st.endPlaced, g_st.endChecked, g_st.endOff1, g_st.endErrMax, g_st.endCentre, g_st.endHeld, g_st.endHeldDefer, g_st.endExpired,
		  g_st.endRepostFail, g_st.endNoSection, g_st.endDropped, g_st.endStale, g_st.eraCalls, g_st.eraCurves, g_st.eraDeferred, g_st.edgeFaults,
		  !g_treadReady ? "off for this build" : g_treadWant ? "on" : "off", g_st.treadCalls, g_st.treadDropped0 + g_st.treadDropped1,
		  g_st.treadDropped0, g_st.treadDropped1, g_st.treadCalls - (g_st.treadDropped0 + g_st.treadDropped1), g_st.treadTagged, g_st.treadFaults);
		g_callsAtInGame = g_st.builderCalls;
		if (read && !c.sound && !g_tripped)
		{
			g_st.checksBad++;
			char w[64]; _snprintf_s(w, _TRUNCATE, "at the in-game check of grid seq %u", g_seqSeen);
			Trip(c, w);
		}
		char gw[64]; _snprintf_s(gw, _TRUNCATE, "in game, grid seq %u", g_seqSeen);
		GraphLine(gw);   // ticket 4854: once per load
	}
}

// The kill switch and farm_builder, then the swap, retirement or restore under the EXE's lock.
static void ConfigMaintain(const FarmGuardEnv& env)
{
	g_skipBuilder = env.skipBuilder != 0;   // ticket 4853
	if ((int)g_skipBuilder != g_skipLogged)
	{
		if (g_skipBuilder || g_skipLogged == 1)
			L(g_skipBuilder ? "farm_guard: farm_builder=0: the EXE's farm builder is not called from now on (the server lays out the farm "
			                  "fields); every call of its listener is counted as skipped (skipped-server)"
			                : "farm_guard: farm_builder=1: the EXE's farm builder is called again");
		g_skipLogged = g_skipBuilder ? 1 : 0;
	}
	// ticket 0655 (P1): the diagnostic area probe
	const bool areaWant = env.areaProbe != 0;
	if (areaWant != g_areaWant)
	{
		g_areaWant = areaWant;
		if (areaWant && !g_areaReady) L("farm_guard: LOUD: farm_area_probe=1 but the area probe's sites do not match (%s): nothing is timed", g_areaWhy[0] ? g_areaWhy : "not checked");
		else L("farm_guard: farm_area_probe=%d (a DIAGNOSTIC of ticket 0655): the builder object's area listener is %s", areaWant ? 1 : 0, areaWant ? "timed per call" : "called by its own node word");
	}
	if (g_areaReady && (g_areaWant || g_st.areaSwaps > g_st.areaRestored)) AreaMaintain(g_areaWant);
	g_postOff = env.farmEndPostOff != 0;   // ticket 0655: the test switch
	if ((int)g_postOff != g_postOffLogged)
	{
		if (g_postOff || g_postOffLogged == 1)
			L(g_postOff ? "farm_guard: farm_end_post=0: no farm-end post on the skipped-server path (the test switch of ticket 0655: no farm "
			              "building with farm_builder=0)"
			            : "farm_guard: farm_end_post=1: the farm-end post is made again on the skipped-server path");
		g_postOffLogged = g_postOff ? 1 : 0;
	}
	// ticket 0657: the edge placement (the pass-throughs while farm_builder=0, farm_edge=1, the post's sites matched)
	g_edgeOff = env.farmEdgeOff != 0;
	if ((int)g_edgeOff != g_edgeOffLogged)
	{
		if (g_edgeOff || g_edgeOffLogged == 1)
			L(g_edgeOff ? "farm_guard: farm_edge=0: no edge pass-through (the test switch of ticket 0657: every farm building at its plot centre)"
			            : "farm_guard: farm_edge=1: the edge pass-throughs are made again");
		g_edgeOffLogged = g_edgeOff ? 1 : 0;
	}
	{
		const bool ew = g_edgeReady && !g_edgeOff && g_skipBuilder && g_postReady && g_role != 2;
		g_edgeWant = ew;
		if ((int)ew != g_edgeWantLogged && (ew || g_edgeWantLogged == 1))
			L("farm_guard: edge (ticket 0657): %s", ew ? "on: the farm buildings are placed at the server's points"
			                                         : "off: the farm buildings stand at their plot centres");
		g_edgeWantLogged = ew ? 1 : 0;
		if (g_edgeReady && (ew || g_st.edgeSwaps > g_st.edgeRestored))
		{
			EdgeMaintain(g_ew.end, EdgeWord(0), ew, "farm-end");
			EdgeMaintain(g_ew.era, EdgeWord(1), ew, "era");
		}
		if (ew) EdgeOpenSection();
	}
	// ticket 0784: the tread filter (tread_filter=1, its sites matched, the owner or alone)
	{
		const bool tw = g_treadReady && env.treadFilter != 0 && g_role != 2;
		if (env.treadFilter && !g_treadReady && g_treadWantLogged != 2)
		{
			g_treadWantLogged = 2;
			L("farm_guard: LOUD: tread_filter=1 but the tread filter (ticket 0784) is OFF for this build (%s): the landship's tread effect is drawn by the game",
			  g_treadWhy[0] ? g_treadWhy : "not checked");
		}
		else if ((int)tw != g_treadWantLogged && (tw || g_treadWantLogged == 1))
		{
			g_treadWantLogged = tw ? 1 : 0;
			L("farm_guard: treads (ticket 0784): %s", tw ? "on: the EXE's two landship tread effects are dropped ([Renderer] HideTreadEffect = 1 with ServerUnits = 1)"
			                                           : "off: the EXE's tread effect is drawn by the game");
		}
		g_treadWant = tw;
		if (g_treadReady) TreadMaintain(tw);   // off: also a node an earlier boot of the owner swapped (the scan finds none: no write)
	}
	const int want = env.enabled || g_tripped || g_skipBuilder;
	if (!env.enabled && !g_disabledLogged) { g_disabledLogged = true; L("farm_guard: farm_guard=0%s", g_tripped ? " ignored: the guard tripped, the builder stays off" : g_skipBuilder ? " ignored while farm_builder=0: the guard's node stays, the builder is not called" : ": the builder's own address is put back"); }
	if (env.enabled) g_disabledLogged = false;
	Maintain(want);
}

// ---- ticket 0535: the shared page (one owner per process) ------------------------------------------------------------------
static_assert(sizeof(void*) == 4, "the guard and its stubs are x86 only (the EXE is 32-bit)");
static_assert(sizeof(FgShared) <= 0x1000, "FgShared fits the page's first 4 KB");
#define FG_PAGE_BYTES 0x2000u
#define FG_CODE_OFF 0x1000u
#define FG_INERT_OFF 0x20u
// The hook stub: mov eax, [hookTarget]; test eax, eax; jz +2; jmp eax; ret 4. The walk calls it with ecx = the node's object
// and one stack argument (farm-builder-guard.md 1): jmp eax hands both on unchanged to the owner's FgHook (fastcall: ecx,
// a dummy edx, one argument popped by the callee); with hookTarget 0 it pops the argument and returns, as FgInert does.
static void StubBytes(uint32_t cell, unsigned char out[14])
{
	static const unsigned char tail[9] = { 0x85, 0xC0, 0x74, 0x02, 0xFF, 0xE0, 0xC2, 0x04, 0x00 };
	out[0] = 0xA1; memcpy(out + 1, &cell, 4); memcpy(out + 5, tail, sizeof(tail));
}
static const unsigned char kInertStub[3] = { 0xC2, 0x04, 0x00 };   // ret 4
// Ticket 0657: an edge stub: mov eax, [target]; test eax, eax; jnz +5; mov eax, [orig]; jmp eax. The walk calls it as the
// listener (ecx = the farm manager, one stack argument): both reach the owner's pass-through, or the EXE's own listener while
// no owner is attached (target 0), unchanged.
#define FG_EDGE_STUB_OFF0 0x40u
#define FG_EDGE_STUB_OFF1 0x60u
#define FG_TREAD_STUB_OFF 0x80u      // ticket 0784: the tread filter's stub (the edge stubs' form)
static void EdgeStubBytes(uint32_t target, uint32_t orig, unsigned char out[16])
{
	out[0] = 0xA1; memcpy(out + 1, &target, 4); out[5] = 0x85; out[6] = 0xC0; out[7] = 0x75; out[8] = 0x05;
	out[9] = 0xA1; memcpy(out + 10, &orig, 4); out[14] = 0xFF; out[15] = 0xE0;
}

static bool CheckShared(uintptr_t a, char* why, size_t cap)
{
	MEMORY_BASIC_INFORMATION m;
	if (VirtualQuery((const void*)a, &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT || m.AllocationBase != (void*)a
		|| m.Protect != PAGE_READWRITE || m.RegionSize < FG_CODE_OFF)
	{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X is not a committed read-write page of its own allocation", (unsigned)a); return false; }
	if (VirtualQuery((const void*)(a + FG_CODE_OFF), &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT
		|| m.AllocationBase != (void*)a || m.Protect != PAGE_EXECUTE_READ)
	{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X + 0x1000 is not the page's execute-read stub page", (unsigned)a); return false; }
	FgShared h;
	if (!SafeCopy(a, &h, sizeof(h))) { _snprintf_s(why, cap, _TRUNCATE, "0x%08X could not be read", (unsigned)a); return false; }
	if (h.magic != FG_SHARED_MAGIC || h.version != FG_SHARED_VERSION || h.pid != GetCurrentProcessId() || h.self != (uint32_t)a
		|| h.hookStub != (uint32_t)(a + FG_CODE_OFF) || h.inertStub != (uint32_t)(a + FG_CODE_OFF + FG_INERT_OFF)
		|| h.edgeStub[0] != (uint32_t)(a + FG_CODE_OFF + FG_EDGE_STUB_OFF0) || h.edgeStub[1] != (uint32_t)(a + FG_CODE_OFF + FG_EDGE_STUB_OFF1)
		|| h.treadStub != (uint32_t)(a + FG_CODE_OFF + FG_TREAD_STUB_OFF))
	{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X does not hold this process's shared page (magic 0x%08X, version %u, pid %u)", (unsigned)a, h.magic, h.version, h.pid); return false; }
	unsigned char want[14], got[14], inert[3];
	StubBytes((uint32_t)(a + offsetof(FgShared, hookTarget)), want);
	if (!SafeCopy(a + FG_CODE_OFF, got, sizeof(got)) || memcmp(got, want, sizeof(want)) != 0
		|| !SafeCopy(a + FG_CODE_OFF + FG_INERT_OFF, inert, sizeof(inert)) || memcmp(inert, kInertStub, sizeof(inert)) != 0)
	{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X: the stubs are not the guard's", (unsigned)a); return false; }
	for (int i = 0; i < 2; ++i)   // ticket 0657
	{
		unsigned char ew[16], eg[16];
		EdgeStubBytes((uint32_t)(a + offsetof(FgShared, edgeTarget) + 4u * i), (uint32_t)(a + offsetof(FgShared, edgeOrig) + 4u * i), ew);
		if (!SafeCopy(a + FG_CODE_OFF + (i ? FG_EDGE_STUB_OFF1 : FG_EDGE_STUB_OFF0), eg, sizeof(eg)) || memcmp(eg, ew, sizeof(ew)) != 0)
		{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X: the edge stubs are not the guard's", (unsigned)a); return false; }
	}
	{   // ticket 0784
		unsigned char tw[16], tg[16];
		EdgeStubBytes((uint32_t)(a + offsetof(FgShared, treadTarget)), (uint32_t)(a + offsetof(FgShared, treadOrig)), tw);
		if (!SafeCopy(a + FG_CODE_OFF + FG_TREAD_STUB_OFF, tg, sizeof(tg)) || memcmp(tg, tw, sizeof(tw)) != 0)
		{ _snprintf_s(why, cap, _TRUNCATE, "0x%08X: the tread stub is not the guard's", (unsigned)a); return false; }
	}
	return true;
}

uintptr_t FarmGuard_FindShared(char* why, size_t cap)
{
	why[0] = 0;
	wchar_t v[32];
	const DWORD n = GetEnvironmentVariableW(FG_SHARED_ENV, v, 32);
	if (n == 0) return 0;   // none (why stays empty)
	if (n >= 32) { _snprintf_s(why, cap, _TRUNCATE, "VP64_FARM_GUARD is too long"); return 0; }
	wchar_t* end = nullptr;
	const unsigned long a = wcstoul(v, &end, 16);
	if (!a || !end || *end) { _snprintf_s(why, cap, _TRUNCATE, "VP64_FARM_GUARD=%S is not a hex address", v); return 0; }
	return CheckShared((uintptr_t)a, why, cap) ? (uintptr_t)a : 0;
}

// Allocated once per process and never freed; the stubs' page is execute-read after it is written (MC #5795).
static uintptr_t CreateShared(char* why, size_t cap)
{
	unsigned char* p = (unsigned char*)VirtualAlloc(nullptr, FG_PAGE_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!p) { _snprintf_s(why, cap, _TRUNCATE, "VirtualAlloc failed (error %lu)", GetLastError()); return 0; }
	FgShared* s = (FgShared*)p;
	s->magic = FG_SHARED_MAGIC; s->version = FG_SHARED_VERSION; s->pid = GetCurrentProcessId(); s->self = (uint32_t)(uintptr_t)p;
	s->hookStub = (uint32_t)(uintptr_t)(p + FG_CODE_OFF); s->inertStub = (uint32_t)(uintptr_t)(p + FG_CODE_OFF + FG_INERT_OFF);
	unsigned char* code = p + FG_CODE_OFF;
	memset(code, 0xCC, FG_PAGE_BYTES - FG_CODE_OFF);
	StubBytes((uint32_t)(uintptr_t)&s->hookTarget, code);
	memcpy(code + FG_INERT_OFF, kInertStub, sizeof(kInertStub));
	s->edgeStub[0] = (uint32_t)(uintptr_t)(code + FG_EDGE_STUB_OFF0); s->edgeStub[1] = (uint32_t)(uintptr_t)(code + FG_EDGE_STUB_OFF1);   // 0657
	EdgeStubBytes((uint32_t)(uintptr_t)&s->edgeTarget[0], (uint32_t)(uintptr_t)&s->edgeOrig[0], code + FG_EDGE_STUB_OFF0);
	EdgeStubBytes((uint32_t)(uintptr_t)&s->edgeTarget[1], (uint32_t)(uintptr_t)&s->edgeOrig[1], code + FG_EDGE_STUB_OFF1);
	s->treadStub = (uint32_t)(uintptr_t)(code + FG_TREAD_STUB_OFF);   // 0784
	EdgeStubBytes((uint32_t)(uintptr_t)&s->treadTarget, (uint32_t)(uintptr_t)&s->treadOrig, code + FG_TREAD_STUB_OFF);
	DWORD old = 0;
	if (!VirtualProtect(code, FG_PAGE_BYTES - FG_CODE_OFF, PAGE_EXECUTE_READ, &old))
	{
		_snprintf_s(why, cap, _TRUNCATE, "VirtualProtect of the stub page failed (error %lu)", GetLastError());
		VirtualFree(p, 0, MEM_RELEASE);   // never published: nothing can point into it yet
		return 0;
	}
	FlushInstructionCache(GetCurrentProcess(), code, FG_PAGE_BYTES - FG_CODE_OFF);
	wchar_t v[16]; _snwprintf_s(v, _TRUNCATE, L"%08X", (unsigned)(uintptr_t)p);
	if (!SetEnvironmentVariableW(FG_SHARED_ENV, v))
		_snprintf_s(why, cap, _TRUNCATE, "created at 0x%08X but VP64_FARM_GUARD could not be set (error %lu): a later boot makes a new page",
			(unsigned)(uintptr_t)p, GetLastError());
	return (uintptr_t)p;
}

static bool g_published;
bool FarmGuard_OwnerAttach(const char* owner, char* why, size_t cap)
{
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	if (!g_jobsLockReady) { InitializeCriticalSection(&g_jobsLock); g_jobsLockReady = true; }
	char fwhy[160] = "";
	uintptr_t p = FarmGuard_FindShared(fwhy, sizeof(fwhy));
	const bool found = p != 0;
	char cwhy[160] = "";
	if (!p) p = CreateShared(cwhy, sizeof(cwhy));
	if (!p) { _snprintf_s(why, cap, _TRUNCATE, "no shared page: %s%s%s", fwhy, fwhy[0] ? "; " : "", cwhy); return false; }
	g_shared = (FgShared*)p; g_role = 1;
	strncpy_s(g_ownerName, owner, _TRUNCATE);
	g_hookAddr = g_shared->hookStub; g_inertAddr = g_shared->inertStub;
	if (g_shared->tripped) { g_tripped = true; g_st.tripped = 1; }
	if (g_shared->rule2Stopped) { g_r2Stopped = true; g_st.rule2Stopped = 1; }
	if (!found) strncpy_s(g_shared->owner, owner, _TRUNCATE);
	const LONG n = InterlockedIncrement(&g_shared->attaches);
	g_published = false;
	_snprintf_s(why, cap, _TRUNCATE, "shared page 0x%08X %s (attach %ld in this process; hook stub 0x%08X, inert stub 0x%08X)%s%s%s%s%s",
		(unsigned)p, found ? "found by VP64_FARM_GUARD (an earlier image of this process made it)" : "created", n, g_shared->hookStub,
		g_shared->inertStub, fwhy[0] ? "; LOUD: the page named before was refused: " : "", fwhy, cwhy[0] ? "; LOUD: " : "", cwhy,
		g_tripped || g_r2Stopped ? "; the guard stopped the builder in an earlier boot of this process: it stays off" : "");
	return true;
}

void FarmGuard_OwnerDetach()
{
	if (g_role != 1 || !g_shared) return;
	InterlockedExchange(&g_shared->hookTarget, 0);   // the stub returns at once from now on: no call reaches this image
	InterlockedExchange(&g_shared->presentFn, 0);
	InterlockedExchange(&g_shared->edgeTarget[0], 0);   // ticket 0657: the edge stubs jump to the EXE's own listeners
	InterlockedExchange(&g_shared->edgeTarget[1], 0);
	InterlockedExchange(&g_shared->treadTarget, 0);   // ticket 0784: the tread stub jumps to the EXE's own listener
	if (g_treadReady) TreadMaintain(false, true);   // and the node gets it back when the lock is free (a node of an earlier boot too)
	if (g_areaReady && g_st.areaSwaps > g_st.areaRestored) AreaMaintain(false, true);   // ticket 0655: no node keeps a word into this image
	InterlockedIncrement(&g_shared->detaches);
	g_published = false;
	if (g_evLog) { fclose(g_evLog); g_evLog = nullptr; }
}

static void __cdecl FgOwnerPresent(uint32_t gridValid, uint32_t gridSeq);
// Once the EXE is checked: the stub reaches this image's hook, a composite's Presents its jobs.
static void Publish()
{
	if (g_role != 1 || !g_shared || g_published) return;
	g_published = true;
	InterlockedExchange(&g_shared->presentFn, (LONG)(uintptr_t)FgOwnerPresent);
	InterlockedExchange(&g_shared->hookTarget, (LONG)(uintptr_t)FgHook);
	if (g_edgeReady)   // ticket 0657: the EXE's listeners first (a stub never jumps to 0), then this image's pass-throughs
	{
		InterlockedExchange(&g_shared->edgeOrig[0], (LONG)g_ew.end.builder);
		InterlockedExchange(&g_shared->edgeOrig[1], (LONG)g_ew.era.builder);
		InterlockedExchange(&g_shared->edgeTarget[0], (LONG)(uintptr_t)EdgeEnd);
		InterlockedExchange(&g_shared->edgeTarget[1], (LONG)(uintptr_t)EdgeEra);
	}
	if (g_treadReady)   // ticket 0784: the EXE's listener first, then this image's pass-through
	{
		InterlockedExchange(&g_shared->treadOrig, (LONG)g_tw.builder);
		InterlockedExchange(&g_shared->treadTarget, (LONG)(uintptr_t)TreadPass);
	}
	L("farm_guard: owner %s: the hook stub 0x%08X now calls this image's guard, a composite's Presents reach its jobs (shared page 0x%08X, "
	  "attach %ld, detaches %ld in this process)", g_ownerName, g_shared->hookStub, g_shared->self, g_shared->attaches, g_shared->detaches);
}

struct JobsLock
{
	bool on;
	JobsLock() : on(g_jobsLockReady) { if (on) EnterCriticalSection(&g_jobsLock); }
	~JobsLock() { if (on) LeaveCriticalSection(&g_jobsLock); }
};

static void __cdecl FgOwnerPresent(uint32_t gridValid, uint32_t gridSeq)
{
	g_lastFwdQpc = Qpc();
	if (!g_ready || g_dead) return;
	JobsLock l;
	PresentJobs(gridValid, gridSeq, true);
}

void FarmGuard_OnTick(const FarmGuardEnv& env)
{
	if (env.log) g_log = env.log;
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	if (!ReadyCheck(env)) return;
	Publish();
	JobsLock l;
	ConfigMaintain(env);
	if (!g_lastFwdQpc) PresentJobs(0, 0, false);   // no composite hands over Presents: the burst lines from here
}

// The composite's copy: an owner's page in the environment = this copy installs nothing and hands the Present over.
static bool Follow(const FarmGuardEnv& env)
{
	if (!g_followChecked)
	{
		g_followChecked = true;
		char why[200] = "";
		const uintptr_t p = FarmGuard_FindShared(why, sizeof(why));
		if (p) { g_shared = (FgShared*)p; g_role = 2; }
		else if (why[0]) L("farm_guard: LOUD: %s: this composite installs the guard itself", why);
	}
	if (g_role != 2) return false;
	if (!g_followLogged)
	{
		g_followLogged = true;
		char owner[17] = ""; memcpy(owner, g_shared->owner, 16);
		L("farm_guard: the %s owns the guard in this process (ticket 0535; shared page 0x%08X): this composite installs nothing "
		  "and hands its Presents to it (the in-game and graph lines are the owner's, in its log)", owner[0] ? owner : "(unnamed)", g_shared->self);
	}
	const FgPresentFn f = (FgPresentFn)(uintptr_t)g_shared->presentFn;
	if (f) { InterlockedIncrement(&g_shared->forwarded); f(env.gridValid, env.gridSeq); }
	return true;
}

void FarmGuard_OnPresent(const FarmGuardEnv& env)
{
	if (env.log) g_log = env.log;
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	if (g_role != 1 && Follow(env)) return;   // ticket 0535
	if (!ReadyCheck(env)) return;
	PresentJobs(env.gridValid, env.gridSeq, true);
	ConfigMaintain(env);
}

void FarmGuard_GetStats(FgStats* s) { *s = g_st; s->areaCalls = (uint32_t)g_areaN; }

void FarmGuard_TestUseArea(const FgWorld* aw)
{
	if (!aw) { g_areaReady = false; g_st.areaReady = 0; memset(&g_aw, 0, sizeof(g_aw)); return; }
	g_aw = *aw; g_areaFn = (FgListenerFn)aw->builder; g_areaReady = true; g_st.areaReady = 1;
}

void FarmGuard_TestUse(const FgWorld* w, const wchar_t* recordDir)
{
	g_w = *w;
	g_builder = (FgListenerFn)w->builder;
	g_ready = true; g_dead = false; g_test = true; g_tripped = false;
	memset(&g_st, 0, sizeof(g_st)); g_st.ready = 1;
	g_bCalls = g_bChecks = g_bBad = g_bSkipped = g_bServer = 0; g_bCheckMs = g_bCheckUsMax = 0; g_loads = 0;
	g_skipBuilder = false; g_skipLogged = -1;
	g_r2Stopped = false; g_r2Have = false; g_bRule2 = 0; memset(&g_r2, 0, sizeof(g_r2));   // ticket 4855
	g_postReady = w->poster && w->plotOnMap && w->artId && w->queueIndex && w->queueBase;   // ticket 0655: a world with the post
	g_st.postReady = g_postReady ? 1 : 0;
	g_postOff = false; g_postOffLogged = -1; g_postNullLogged = 0;
	g_areaReady = false; g_areaWant = false; g_areaN = 0; g_areaDone = 0; g_areaLastQpc = 0; memset(&g_aw, 0, sizeof(g_aw));   // 0655 P1
	if (g_areaLog) fclose(g_areaLog);
	g_areaLog = nullptr; g_areaLogTried = false;
	g_bPosts = g_bPostFail = g_bPostRefused = g_bPostOff = 0;
	g_recordDirChosen = false;
	ChooseRecordDir(recordDir);
	g_disabledLogged = false; g_lastScanBadLog = 0;
	g_seqSeen = g_seqDone = g_callsAtInGame = 0; g_seqQpc = 0; g_lastCallQpc = 0;
	if (g_evLog) fclose(g_evLog);
	g_evLog = nullptr; g_evLogTried = false; g_evLines = g_bEvLines = 0;
	g_quietMs = FG_QUIET_MS; g_inGameMs = FG_INGAME_MS;
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	FarmGuard_TestForgetRole();   // ticket 0535
	g_r2Gen = 0; g_validLogged = false;
	// ticket 0657: no edge until FarmGuard_TestUseEdge; the hold limits and the grid back to their defaults
	g_edgeReady = false; g_edgeWant = false; g_edgeOff = false; g_edgeTest = false; g_edgeOffLogged = -1; g_edgeWantLogged = -1;
	memset(&g_ew, 0, sizeof(g_ew)); g_edgeEndFn = g_edgeEraFn = nullptr;
	g_farmSec = nullptr; g_farmSecTried = 0; g_farmSecLogged = false;
	g_camValid = 0; g_camSeq = 0; g_holdSeq = -1; HoldClear();
	g_holdLoadMs = 20000.0; g_holdMissMs = 5000.0;
	g_tcWritten = 0; g_tcSwapped = false;
	g_bEndPlaced = g_bEndCentre = g_bEndHeld = g_bEndExpired = g_bEraCurves = 0;
	// ticket 0784: no tread filter until FarmGuard_TestUseTread
	g_treadReady = false; g_treadWant = false; g_treadWantLogged = -1; g_treadFn = nullptr; g_treadWhy[0] = 0;
	memset(&g_tw, 0, sizeof(g_tw)); g_treadLineDropped = 0; g_treadLineTick = 0;
}

void FarmGuard_TestUseTread(const FgWorld* tw)
{
	g_treadHash[0] = FgEffectNameHash(FG_TREAD_NAME0);
	g_treadHash[1] = FgEffectNameHash(FG_TREAD_NAME1);
	if (!tw) { g_treadReady = false; g_st.treadReady = 0; memset(&g_tw, 0, sizeof(g_tw)); return; }
	g_tw = *tw;
	g_treadFn = (FgListenerFn)tw->builder;
	g_treadReady = true; g_st.treadReady = 1;
}

void FarmGuard_TestUseEdge(const FgEdgeWorld* ew)
{
	if (!ew) { g_edgeReady = false; g_st.edgeReady = 0; memset(&g_ew, 0, sizeof(g_ew)); return; }
	g_ew = *ew;
	g_edgeEndFn = (FgListenerFn)ew->end.builder;
	g_edgeEraFn = (FgListenerFn)ew->era.builder;
	g_edgeReady = true; g_st.edgeReady = 1; g_edgeTest = true;
}

void FarmGuard_TestEdgePoints(const void* block)
{
	g_edgeTest = true;
	g_farmSec = (const VPFarmHeader*)block;
	g_st.edgeSection = block ? 1 : 0;
}

void FarmGuard_TestGrid(uint32_t gridValid, uint32_t gridSeq)
{   // as PresentJobs does at a Present on the main thread
	g_camValid = (LONG)gridValid; g_camSeq = (LONG)gridSeq;
	if ((LONG)gridSeq != g_holdSeq) { g_holdSeq = (LONG)gridSeq; HoldClear(); }
}

void FarmGuard_TestEdgeHold(double loadMs, double missingMs) { g_holdLoadMs = loadMs; g_holdMissMs = missingMs; }

void FarmGuard_TestForgetRole()
{
	g_role = 0; g_shared = nullptr; g_followChecked = g_followLogged = false; g_hookAddr = g_inertAddr = 0;
	strncpy_s(g_ownerName, "composite", _TRUNCATE);
	g_lastFwdQpc = g_lastBuiltQpc = 0; g_published = false;
}

void FarmGuard_TestTimes(double quietMs, double inGameMs) { g_quietMs = quietMs; g_inGameMs = inGameMs; }
