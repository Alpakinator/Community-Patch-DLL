// decal_image_fixture.h - a synthetic decal tap image (shell/common/shell_decal_tap.h VPShellDecalImage, the .dtap file
// form of terrain-decals.md 10.4) for the scene bundle's decal member tests (issue 0364). Shared by the tests in this
// folder (a header here is not a test: build_scenebundle_tests.py). Written 2026-11-15 for this repository
// (GPL-3.0-or-later); the layout follows server/tests/render/decal/decal_tap_test.cpp's Build (4103), without the area and
// walk blocks. Every byte is made here (D83: nothing from the game).
#pragma once
#include "shell_decal_tap.h"
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace decalfixture {

inline size_t Align16(size_t x) { return (x + 15) & ~(size_t)15; }

// generation, loadSeq; three decals in a 1024 x 1024 sample world, three texture names (`names` replaces them: the player
// test names textures its stand-in install holds).
inline std::vector<uint8_t> MakeImage(uint32_t generation, uint32_t loadSeq,
                                      const std::vector<std::string>& names = {"roadsandrails_new_d.dds", "roadsandrails_new_h.dds",
                                                                               "blank_texture_spec_decal.dds"})
{
	struct D
	{
		float rect[4];
		float prio;
		uint32_t tri;
	};
	const D ds[3] = {{{10, 10, 20, 20}, 0.7f, 2}, {{100, 62, 130, 64}, 0.0f, 1}, {{300, 300, 700, 400}, 0.5f, 3}};
	const uint32_t n = 3;
	uint32_t verts = 0;
	for(const D& d : ds)
		verts += 3 * d.tri;
	size_t off = 112;
	const size_t decalOff = off;
	off = Align16(off + 48ull * n);
	const size_t nameOff = off;
	off = Align16(off + 200ull * names.size());
	const size_t vertOff = off;
	off = Align16(off + 8ull * verts);
	const size_t chOff = off;
	off += 16;   // one changed rectangle
	std::vector<uint8_t> b(off, 0);
	auto put32 = [&](size_t o, uint32_t v) { memcpy(&b[o], &v, 4); };
	put32(0, VPSHELL_DECAL_MAGIC);
	put32(4, VPSHELL_DECAL_IMAGE_VERSION);
	put32(8, 112);
	put32(12, 48);
	put32(16, generation);
	put32(20, loadSeq);
	put32(24, n);
	put32(28, (uint32_t)names.size());
	put32(32, verts);
	put32(36, 1);
	put32(40, (uint32_t)decalOff);
	put32(44, (uint32_t)nameOff);
	put32(48, (uint32_t)vertOff);
	put32(52, (uint32_t)chOff);
	const float world[4] = {0, 0, 1023, 1023};
	memcpy(&b[56], world, 16);
	const uint64_t total = off;
	memcpy(&b[72], &total, 8);
	uint32_t first = 0;
	for(uint32_t i = 0; i < n; ++i)
	{
		uint8_t* r = &b[decalOff + 48ull * i];
		const uint32_t flags = 0, tex[3] = {0, 1, 2}, listOrder = i;
		memcpy(r + 0, ds[i].rect, 16);
		memcpy(r + 16, &ds[i].prio, 4);
		memcpy(r + 20, &flags, 4);
		memcpy(r + 24, tex, 12);
		memcpy(r + 36, &first, 4);
		memcpy(r + 40, &ds[i].tri, 4);
		memcpy(r + 44, &listOrder, 4);
		for(uint32_t k = 0; k < 3 * ds[i].tri; ++k)
		{
			const uint16_t v[4] = {(uint16_t)(i * 131 + k), (uint16_t)(i * 7 + 3 * k), (uint16_t)(k * 17), (uint16_t)i};
			memcpy(&b[vertOff + 8ull * (first + k)], v, 8);
		}
		first += 3 * ds[i].tri;
	}
	for(size_t i = 0; i < names.size(); ++i)
	{
		uint8_t* r = &b[nameOff + 200 * i];
		const uint32_t handle = 1000 + (uint32_t)i;
		memcpy(r, &handle, 4);
		memcpy(r + 8, names[i].c_str(), names[i].size() < 191 ? names[i].size() : 191);
	}
	const float ch[4] = {10, 10, 20, 20};
	memcpy(&b[chOff], ch, 16);
	return b;
}

}  // namespace decalfixture
