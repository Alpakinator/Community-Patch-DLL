// proxy_purity.h - the purity lists as data bound to the x64 DLL (epic 3500, W2 = issue 3503; contract set by the
// epic's lead, tools/release/README.md section 4). Replaces the compiled lists (proxy_pure.inc, kLuaPure in
// proxy_lua.cpp) once issue 3503 lands.
//
// Who calls what:
//   - the boot (proxy_rt.cpp, issue 3504) calls ProxyPurity::Load once, after it has checked the DLL pack, and before
//     ProxyLocal::InitTables; it logs `why` in one line either way;
//   - proxy_local.cpp (method keys) and proxy_lua.cpp (Lua names) ask IsPureMethodKey / IsPureLuaName (issue 3503).
// A list that was never loaded, or whose load failed, is empty: every call crosses (decision D30's safe default).
// Plain C++ for the VC9 headers (C++11 at most), like the rest of the proxy.
#pragma once
// The proxy's STL layout (VC9 headers): checked iterators and iterator debugging off, as everywhere in the proxy.
// They must be set before any standard header, or this header's std::string differs in layout from the one in the
// other proxy objects and the built proxy crashes at its first push_back (found by 3503 under cdb, 2026-11-13; MC #236).
#ifndef _SECURE_SCL
#define _SECURE_SCL 0
#endif
#ifndef _HAS_ITERATOR_DEBUGGING
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include <string>
#include <stddef.h>

namespace ProxyPurity
{
// Reads the purity file at `path` (format: tools/release/README.md section 4). `dllSha256Hex` is the SHA-256 of the
// x64 DLL the server will load, 64 hex digits in either case. Returns true only when the file is readable,
// well-formed, of a known format version and bound to that hash; the lists are then the file's. On false the lists are
// empty. `why` (may be NULL) receives one line for the log in both cases.
bool Load(const char* path, const char* dllSha256Hex, std::string* why);

// Clears both lists (every call crosses).
void Clear();

// True once Load has been called in this process, whatever its result (added 2026-11-13 on 3503's proposal, epic
// thread #170): lets ProxyLocal::InitTables tell a boot that loaded nothing from one that never tried.
bool LoadAttempted();

// A model key of an engine -> DLL method ("ICvCity1::GetOwner() const"), as proxy_local.cpp matches it today.
bool IsPureMethodKey(const char* key);

// A Lua function name "Table.Function" ("Plot.GetX"), as proxy_lua.cpp matches it today.
bool IsPureLuaName(const char* name);

size_t MethodCount();
size_t LuaCount();
}  // namespace ProxyPurity
