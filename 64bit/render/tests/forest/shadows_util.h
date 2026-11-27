// render/tests/forest/shadows_util.h - a .dtap reader for the shadow tests' proof modes (epic 0431, ticket 4552).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/status.h"

namespace c5r { namespace test4552 {

// A recorded decal tap image (.dtap = the image bytes, shell/common/shell_decal_tap.h: header 112 B, records 48 B, names
// 200 B, 8-byte vertices, the hash, area and walk blocks) into a DecalSet as the server's reader fills it (pass = flags
// bit 0, vertexCount = 3 x triangles, listOrder, hash; the area block one byte per tile; the walk per record), then
// decal::buildIndex. Checks: magic 'C5DT', version 1, sizes, every block inside the file. Errors: Truncated, BadMagic,
// BadVersion, BadLayout.
Status readDtap(const std::vector<uint8_t>& b, decal::DecalSet& out);
Status readFile(const std::string& path, std::vector<uint8_t>& out);

}} // namespace c5r::test4552
