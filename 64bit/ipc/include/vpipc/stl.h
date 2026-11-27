// vpipc/stl.h - optional STL conveniences over Writer / Reader. Owner: epic 1200 coordinator.
// Header-only on purpose: it compiles against whichever standard library the including module uses (VC9's in
// the proxy, MSVC 14's in the server) and never puts an STL object into the library core or the section.
// C++03 API only, so it builds under the VC9 headers.
#pragma once
#include <string>

#include "vpipc/marshal.h"

namespace vpipc {

inline void write_string(Writer& w, const std::string& s) { w.str(s.c_str(), (uint32_t)s.size()); }
inline void write_wstring(Writer& w, const std::wstring& s) { w.wstr(s.c_str(), (uint32_t)s.size()); }

// Null strings read as "" here; use Reader::cstr() to tell them apart.
inline std::string read_string(Reader& r) {
    uint32_t n = 0;
    const char* p = r.cstr(&n);
    return p ? std::string(p, n) : std::string();
}
inline std::wstring read_wstring(Reader& r) {
    uint32_t n = 0;
    const wchar_t* p = r.wcstr(&n);
    return p ? std::wstring(p, n) : std::wstring();
}

}  // namespace vpipc
