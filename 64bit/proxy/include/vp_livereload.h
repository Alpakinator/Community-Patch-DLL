// vp_livereload.h - the live reload of the EXE's ice and coast waves after a live terrain change (epic 0465, ticket 0586).
// Written 2026-11-19 for this repository (GPL-3.0-or-later). C++03, no standard library: compiled by clang-cl -m32 (the
// proxy, VC9 headers) and by clang-cl -m64 (server.exe's render units, which include it by a relative path).
// Design and findings: research/05-graphics-composition/live-terrain.md section 10.
//
// Who does what:
//   - server.exe (srv_liveterrain_icewaves.cpp) makes the named section Local\VP64LiveReload.<EXE pid> once it draws live
//     terrain, decides from the DLL's plot table which ice plots the EXE should hold and whether the coast waves must be
//     built again, and posts one request at a time (reqSeq; never a second one before ackSeq == reqSeq);
//   - the proxy (proxy_livereload.cpp, inside the EXE) opens the section and, on the EXE's main thread in the in-game UI
//     context (the Lua bridge's per-frame drain), writes the requested ice bits into the EXE's per-plot ice grid (a data
//     write, D4), calls the EXE's own Lua globals Ice_Reload() / Waves_Reload() by name, and acknowledges (ackSeq);
//   - after a successful Waves_Reload the server raises the water tap's requestSeq (shell_shared.h), so the composite
//     copies the new wave chunks; the ice tap copies the new ice mesh by itself (new handles, polar-ice.md 9).
// The ice grid (live-terrain.md 4.8 [L]): Ice = the build table's ice_ice (0xD68F9C in build 4390913), grid at Ice + 0xA0,
// byte [y * 255 + x], bit 0x80 = ice; W, H at Ice + 0x94 / + 0x98. Only on an EXE build the patch knows, with the
// addresses from its build table (ticket 0619, epic 0590; fail-closed on any other build, D200).
#pragma once
#include <stdint.h>

namespace vplr {

enum
{
	kMagic = 0x524C5056u,   // "VPLR"
	kVersion = 1,
	kMaxW = 255,            // the grid's row stride
	kMaxH = 254,            // rows that fit the grid's room in the ice object (Ice + 0xA0 + 255 * H <= Ice + 0xFEA0)
	kBitmapBytes = 8192,    // one bit per plot, y * W + x (65,536 bits >= 255 * 254)
	kSectionBytes = 16384
};

// reqFlags
enum
{
	kReqIce = 1u << 0,      // write the ice bits of `ice` into the grid, then Ice_Reload()
	kReqWaves = 1u << 1     // Waves_Reload()
};

// Results (iceResult, wavesResult)
enum
{
	kOk = 0,
	kNotAsked = 1,
	kRefusedExe = -1,       // the EXE is no build the patch knows, or its table lacks a symbol (no read, write or call)
	kRefusedPattern = -2,   // Ice_Reload's code at ice_reload is not "push <cam_grid>; mov ecx, <ice_ice>; call" (rebased)
	kGridMismatch = -3,     // the grid's W x H is not the request's map, or the flips found differ from the request's
	kNotWritable = -4,      // the grid range is not committed PAGE_READWRITE memory inside the EXE's image
	kNoFunction = -5,       // the global is not a function in the in-game UI context
	kLuaError = -6,         // the call raised an error (text in `why`)
	kBadRequest = -7        // the request's sizes are out of range
};

#pragma pack(push, 4)
struct Section
{
	uint32_t magic;           // kMagic, written by the server last at creation
	uint32_t version;         // kVersion
	uint32_t bytes;           // sizeof(Section)
	uint32_t serverPid;
	// ---- the request (server; written before reqSeq, never while reqSeq != ackSeq) ----
	volatile uint32_t reqSeq; // 1, 2, ...: a new request (written last, after a barrier)
	uint32_t reqFlags;        // kReqIce | kReqWaves
	uint32_t loadSeq;         // the server's field load seq (diagnostic; the proxy logs it)
	uint32_t mapW, mapH;      // the DLL's map in plots
	uint32_t iceCount;        // bits set in `ice`
	uint32_t expectFlips;     // plots whose ice bit the proxy must change: the grid as the last acknowledged request (or
	                          // the load) left it, against `ice`. The proxy refuses (kGridMismatch) when it counts others
	uint8_t ice[kBitmapBytes];   // bit (y * mapW + x) set: the EXE's grid should hold ice there
	// ---- the answer (proxy; written before ackSeq) ----
	volatile uint32_t ackSeq; // = reqSeq once done (written last, after a barrier)
	int32_t iceResult;        // kOk, kNotAsked or an error above
	int32_t wavesResult;
	uint32_t bitsSet, bitsCleared;   // grid bytes changed
	uint32_t iceMicros, wavesMicros; // the two calls' time on the main thread
	uint32_t proxyPid;
	char why[160];            // the first error's text ("" when both are kOk / kNotAsked)
};
#pragma pack(pop)

// The section's name: Local\VP64LiveReload.<EXE pid> (wide). Formatted by each side with its own swprintf.
#define VPLR_SECTION_FMT L"Local\\VP64LiveReload.%u"

// The ice grid inside the Ice object (live-terrain.md 4.8, polar-ice.md 3). The Ice object, the grid Ice_Reload passes and
// Ice_Reload's site are build-table symbols (ice_ice, cam_grid, ice_reload; ticket 0619), not constants here.
enum
{
	kIceOffW = 0x94,
	kIceOffH = 0x98,
	kIceOffGrid = 0xA0,
	kIceStride = 255,
	kIceBit = 0x80
};

inline bool BitGet(const uint8_t* bm, uint32_t i)
{
	return (bm[i >> 3] >> (i & 7)) & 1;
}
inline void BitSet(uint8_t* bm, uint32_t i, bool on)
{
	if(on)
		bm[i >> 3] = (uint8_t)(bm[i >> 3] | (1u << (i & 7)));
	else
		bm[i >> 3] = (uint8_t)(bm[i >> 3] & ~(1u << (i & 7)));
}

}  // namespace vplr
