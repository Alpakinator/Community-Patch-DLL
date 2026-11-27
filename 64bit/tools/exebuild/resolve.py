"""resolve.py - the EXE addresses a Python tool reads in a running game, per known build (ticket 0593, epic 0590, D202).

The tools that read or write the EXE's memory from outside (perf/quietlock, the turn-study poller and profiler,
shell/tools/view_mask.py and gt_reader.py, tools/i0b/read_masks.py, the testbed's release_d33_runtime.py,
server/tests/units/live/art_records.py) name every EXE address by its catalogue symbol (exebuild/symbols/*.tsv) and
ask this module for the VA in the build they found. Standard library only, Python 3.8+ (the test machines' runner).

    import resolve                                  # a tool elsewhere loads it by path, as c5_exebuild_resolve
    rs = resolve.for_header(stamp, size_of_image)   # a Resolver; UnknownBuild when the header is no known build
    rs.va('cam_app')                                # VA at ImageBase 0x400000 in that build
    rs.rva('cam_app')                               # va - 0x400000
    rs.at(base, 'cam_app')                          # rebased on the module's load address
    rs.of_anchor(0xCC8E74)                          # the VA in that build of the symbol whose anchor is 0xCC8E74
    resolve.known_stamps()                          # the DX11 TimeDateStamps of every known build

Where the VAs come from:
- the anchor build (Steam build 4390913): the committed catalogue's anchors; no local table is needed, so the tools keep
  working on the machines without the exe_tables setting (oldpc, the laptop, the testbed hosts);
- every other known build: <exe_tables>/<steam build>/table.json through table.py. A missing setting or table raises
  table.TableError naming the setting and the generator command (COMMON 9: fail loud); a symbol the table does not
  carry raises NotCarried (the build is not supported for that tool until the table carries it).
Nothing here prints or stores a VA of a non-anchor build; callers keep those out of git, issues and messages (D202).
Format and commands: research/02-exe-internals/exe-build-table.md (section 12 for this module).
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from typing import Dict, Iterable, Optional

HERE = Path(__file__).resolve().parent
IMAGE_BASE = 0x400000


def _table():
    mod = sys.modules.get('c5_exebuild_table')
    if mod is None:
        spec = importlib.util.spec_from_file_location('c5_exebuild_table', HERE / 'table.py')
        mod = importlib.util.module_from_spec(spec)
        sys.modules['c5_exebuild_table'] = mod
        spec.loader.exec_module(mod)
    return mod


table = _table()
builds = table.builds
TableError = table.TableError
CatalogueError = table.CatalogueError


class UnknownBuild(Exception):
    """The EXE's header (TimeDateStamp, SizeOfImage) is not a build of tools/exebuild/builds.py."""


class NotCarried(Exception):
    """The build's local table does not carry a symbol (or the catalogue has no such symbol)."""


def known_stamps():
    """The DX11 EXE TimeDateStamps of every known build (builds.KNOWN order)."""
    return tuple(b.stamp for b in builds.KNOWN)


def describe_known():
    return ', '.join('%s (TimeDateStamp 0x%08X, SizeOfImage 0x%X)' % (b.steam_build, b.stamp, b.size_of_image)
                     for b in builds.KNOWN)


class Resolver:
    """The VAs of one build. Build 4390913 from the catalogue; any other build from its local table, loaded once."""

    def __init__(self, build, tables=None, environ=None, symbols_dir=None):
        self.build = build
        self.is_anchor = build.id == builds.ANCHOR.id
        self._cat = table.by_name(symbols_dir)
        self._anchor_to_name = {}
        for s in self._cat.values():
            if s.anchor is not None:
                self._anchor_to_name.setdefault(s.anchor, s.name)
        self._syms = None
        self.table_path = None
        if not self.is_anchor:
            t = table.load(build.steam_build, tables, environ)        # TableError names the setting
            self._syms = t.get('symbols', {})
            self.table_path = str(table.table_path(build.steam_build, tables, environ))

    @property
    def label(self):
        return self.build.label

    def va(self, name: str) -> int:
        s = self._cat.get(name)
        if s is None:
            raise NotCarried('no symbol %s in the catalogue (exebuild/symbols/*.tsv)' % name)
        if s.anchor is None:
            raise NotCarried('%s is an array, not one address' % name)
        if self.is_anchor:
            return s.anchor
        e = self._syms.get(name)
        if not e or not e.get('va'):
            raise NotCarried('the local table of build %s (%s) does not carry %s: run the generator again '
                             '(research/02-exe-internals/exe-build-table.md section 8)'
                             % (self.build.steam_build, self.table_path, name))
        return int(e['va'], 16)

    def vas(self, names: Iterable[str]) -> Dict[str, int]:
        return {n: self.va(n) for n in names}

    def rva(self, name: str) -> int:
        return self.va(name) - IMAGE_BASE

    def at(self, base: int, name: str) -> int:
        """The symbol's address in a process whose EXE is loaded at `base`."""
        return self.va(name) - IMAGE_BASE + int(base)

    def name_of_anchor(self, anchor_va: int) -> str:
        n = self._anchor_to_name.get(int(anchor_va))
        if n is None:
            raise NotCarried('0x%X is no catalogue anchor (exebuild/symbols/*.tsv): add a symbol for it' % anchor_va)
        return n

    def of_anchor(self, anchor_va: int) -> int:
        """The VA in this build of the catalogue symbol whose anchor (build 4390913 VA) is `anchor_va`. On the anchor
        build any VA is returned as it is (the old behaviour of the tools); on another build it must be catalogued."""
        if self.is_anchor:
            return int(anchor_va)
        return self.va(self.name_of_anchor(anchor_va))


def for_build(build, tables=None, environ=None, symbols_dir=None) -> Resolver:
    """build: a builds.Build, a steam build string or a build id."""
    if not hasattr(build, 'steam_build'):
        b = builds.by_steam_build(str(build)) if not isinstance(build, int) else \
            next((k for k in builds.KNOWN if k.id == build), None)
        if b is None:
            raise UnknownBuild('build %r is not a known build (%s)' % (build, describe_known()))
        build = b
    return Resolver(build, tables, environ, symbols_dir)


def for_header(stamp: Optional[int], size_of_image: Optional[int], tables=None, environ=None,
               symbols_dir=None) -> Resolver:
    """The resolver of the build with this DX11 EXE header; UnknownBuild names both values and the known builds."""
    b = builds.from_header(stamp, size_of_image) if stamp is not None else None
    if b is None:
        raise UnknownBuild('the EXE (TimeDateStamp %s, SizeOfImage %s) is not a known build; known: %s'
                           % ('0x%08X' % stamp if stamp is not None else 'unreadable',
                              '0x%X' % size_of_image if size_of_image is not None else 'unreadable', describe_known()))
    return Resolver(b, tables, environ, symbols_dir)


def for_file(path, tables=None, environ=None, symbols_dir=None) -> Resolver:
    stamp, soi = builds.read_header(path)
    return for_header(stamp, soi, tables, environ, symbols_dir)


def header_of_image(read, base: int):
    """(TimeDateStamp, SizeOfImage, Machine) of a mapped 32-bit image through `read(addr, n) -> bytes`."""
    import struct
    dos = read(base, 0x40)
    if dos[:2] != b'MZ':
        raise UnknownBuild('no MZ header at the module base 0x%X' % base)
    e = struct.unpack_from('<I', dos, 0x3C)[0]
    if not 0 < e < 0x1000:
        raise UnknownBuild('e_lfanew 0x%X out of range' % e)
    nt = read(base + e, 0x58)
    if nt[:4] != b'PE\0\0':
        raise UnknownBuild('no PE signature at the module base 0x%X' % base)
    return struct.unpack_from('<I', nt, 8)[0], struct.unpack_from('<I', nt, 0x18 + 0x38)[0], \
        struct.unpack_from('<H', nt, 4)[0]
