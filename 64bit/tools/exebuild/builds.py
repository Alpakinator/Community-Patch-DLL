"""The Civ V DX11 EXE builds the patch knows (epic 0590, decision D202): the Python mirror of
exebuild/include/vp_exe_build.h. Identities only, never an address (the per-build address tables are generated locally
by tools/exebuild and never committed; research/02-exe-internals/exe-build-table.md).

A build is known when both the PE TimeDateStamp and SizeOfImage match. Tools that look at an install's file also know
the file sizes seen so far (Steam's signature makes copies differ in size; the size is information, not the key).
Standard library only, Python 3.8+ (the test machines' runner).

    import builds                             # with tools/exebuild on sys.path
    b = builds.of_file(path)                  # a Build or None
"""
from __future__ import annotations

import struct
from typing import NamedTuple, Optional, Tuple


class Build(NamedTuple):
    id: int
    steam_build: str
    stamp: int
    size_of_image: int
    label: str
    file_sizes: Tuple[int, ...]   # DX11 EXE file sizes seen on project machines (not part of the key)


# Keep in the order and with the ids of VPEXE_KNOWN_BUILDS in exebuild/include/vp_exe_build.h.
KNOWN = (
    Build(0, "4390913", 0x546CD0A8, 0x06519000, "Steam build 4390913 (2014-11-19 EXE, with CEG)",
          (16982584, 16982840)),
    Build(1, "26577625", 0x6AF3837D, 0x063A2000, "Steam build 26577625 (Firaxis' 2026-11-19 update, no CEG)",
          (15466264,)),
)
ANCHOR = KNOWN[0]          # the build the committed anchor VAs (exebuild/symbols/*.tsv) are taken from
# The builds the product may run the 64-bit path on (vp_exe_build.h VpExeBuildProductReady; the lead of epic 0590 flips a
# build when its carry and live smokes passed). Keep equal to the header (tools/exebuild/tests).
PRODUCT_READY = ("4390913", "26577625")  # 26577625: ticket 0606, D214 (2026-11-20)


def product_ready(build) -> bool:
    """True for a Build (or a Steam build id string) the product may run the 64-bit path on."""
    sb = build.steam_build if isinstance(build, Build) else str(build)
    return sb in PRODUCT_READY


def from_header(stamp: int, size_of_image: int) -> Optional[Build]:
    for b in KNOWN:
        if b.stamp == stamp and b.size_of_image == size_of_image:
            return b
    return None


def by_steam_build(steam_build: str) -> Optional[Build]:
    for b in KNOWN:
        if b.steam_build == str(steam_build):
            return b
    return None


def read_header(path) -> Tuple[Optional[int], Optional[int]]:
    """(TimeDateStamp, SizeOfImage) of a 32-bit PE file, or (None, None) if missing, unreadable or not PE32."""
    try:
        with open(str(path), "rb") as f:
            h = f.read(4096)
    except OSError:
        return None, None
    if len(h) < 0x40 or h[:2] != b"MZ":
        return None, None
    e = struct.unpack_from("<I", h, 0x3C)[0]
    if e + 24 + 60 > len(h) or h[e:e + 4] != b"PE\0\0" or struct.unpack_from("<H", h, e + 24)[0] != 0x10B:
        return None, None
    return struct.unpack_from("<I", h, e + 8)[0], struct.unpack_from("<I", h, e + 24 + 56)[0]


def of_file(path) -> Optional[Build]:
    stamp, soi = read_header(path)
    return None if stamp is None else from_header(stamp, soi)
