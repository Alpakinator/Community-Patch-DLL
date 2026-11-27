// vp_exe_build.h - which Civilization V DX11 EXE build is running (epic 0590, decision D202). Written 2026-11-19 for
// this repository (GPL-3.0-or-later).
//
// The one list of EXE builds the patch knows, keyed by the DX11 EXE's PE TimeDateStamp and SizeOfImage. Every product
// module that touches the EXE asks VpExeBuildOfImage() here instead of comparing a stamp of its own. This file holds
// the builds' identities only, never an address: the per-build address tables are generated on the build machine by
// tools/exebuild/ and stay local (D202; research/02-exe-internals/exe-build-table.md).
//
// The Python mirror is tools/exebuild/builds.py; tools/exebuild/tests keep the two lists equal.
//
// Header only, C++03 (the proxy's toolchain), no Windows header needed. A module that has the EXE's base address
// (GetModuleHandleW(NULL) in the EXE process, the remote module base read by the server) passes a pointer to a copy of
// at least the first 4 KB of the image to VpExeBuildOfImage(), or the two header fields to VpExeBuildFromHeader().
#pragma once
#include <stdint.h>
#include <string.h>

// B(id, steam build, PE TimeDateStamp, SizeOfImage, label). Add a build by appending one line; ids stay stable.
#define VPEXE_KNOWN_BUILDS(B) \
	B(0, "4390913",  0x546CD0A8u, 0x06519000u, "Steam build 4390913 (2014-11-19 EXE, with CEG)") \
	B(1, "26577625", 0x6AF3837Du, 0x063A2000u, "Steam build 26577625 (Firaxis' 2026-11-19 update, no CEG)")

enum
{
	kVpExeBuildUnknown = -1,  // not one of the builds above: nothing of the patch may touch the EXE (0588, D118)
	kVpExeBuild4390913 = 0,
	kVpExeBuild26577625 = 1,
	kVpExeBuildCount = 2
};

struct VpExeBuildInfo
{
	int id;
	const char* steamBuild;
	uint32_t timeDateStamp;
	uint32_t sizeOfImage;
	const char* label;
};

inline const VpExeBuildInfo* VpExeBuildInfoOf(int id)
{
#define VPEXE_ROW_(i, sb, ts, soi, lab) { i, sb, ts, soi, lab },
	static const VpExeBuildInfo kRows[kVpExeBuildCount] = { VPEXE_KNOWN_BUILDS(VPEXE_ROW_) };
#undef VPEXE_ROW_
	return (id >= 0 && id < kVpExeBuildCount) ? &kRows[id] : 0;
}

// Whether the product may run the 64-bit path on a known build at all: true only once every consumer of the build's
// addresses is carried to the build table and the build's live smokes passed. The lead of epic 0590 flips a build here:
// build 26577625 was made ready by ticket 0606 (decision D214, 2026-11-20) after stage 2 (tickets 4963-4966),
// 4967's live smokes and 0606's two runs on the flipped build. A known build left out here takes ticket 0588's path
// (known, named, not supported). The proxy ANDs this with the table's
// VpExeBuildSupported (vp_exe_symbols.h: the binary's table for the build is complete). Mirrored in
// tools/exebuild/builds.py PRODUCT_READY (tools/exebuild/tests keeps the two equal).
inline bool VpExeBuildProductReady(int id)
{
	return id == kVpExeBuild4390913 || id == kVpExeBuild26577625;
}

// The build with this stamp and SizeOfImage, or kVpExeBuildUnknown. Both must match: a stamp alone is not enough.
inline int VpExeBuildFromHeader(uint32_t timeDateStamp, uint32_t sizeOfImage)
{
	for (int i = 0; i < kVpExeBuildCount; ++i)
	{
		const VpExeBuildInfo* b = VpExeBuildInfoOf(i);
		if (b->timeDateStamp == timeDateStamp && b->sizeOfImage == sizeOfImage)
			return i;
	}
	return kVpExeBuildUnknown;
}

// Reads the PE headers of an image (mapped, or the first bytes of the file: the header fields used are at the same
// offsets in both). bytes = how many bytes at image are readable. *stamp and *sizeOfImage (each may be NULL) get the
// values read, 0 when the image is not a 32-bit PE. Never reads past bytes.
inline int VpExeBuildOfImage(const void* image, uint32_t bytes, uint32_t* stamp, uint32_t* sizeOfImage)
{
	const unsigned char* p = (const unsigned char*)image;
	uint32_t ts = 0, soi = 0;
	if (p && bytes >= 0x40 && p[0] == 'M' && p[1] == 'Z')
	{
		uint32_t e;
		memcpy(&e, p + 0x3C, 4);
		// PE signature (4) + IMAGE_FILE_HEADER (20) + IMAGE_OPTIONAL_HEADER32 up to SizeOfImage (56 + 4)
		if (e <= bytes && bytes - e >= 4 + 20 + 60 && memcmp(p + e, "PE\0\0", 4) == 0)
		{
			uint16_t magic;
			memcpy(&magic, p + e + 24, 2);
			if (magic == 0x10B)
			{
				memcpy(&ts, p + e + 8, 4);
				memcpy(&soi, p + e + 24 + 56, 4);
			}
		}
	}
	if (stamp)
		*stamp = ts;
	if (sizeOfImage)
		*sizeOfImage = soi;
	return (ts || soi) ? VpExeBuildFromHeader(ts, soi) : kVpExeBuildUnknown;
}
