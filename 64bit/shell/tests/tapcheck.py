"""The height-field tap from the consumer's side (issue 3203, epic 3200): a stand-in for the server until its terrain
scene (issue 3205) reads the tap, and the checker of the tests and the live runs.

The tap block (shell/common/shell_shared.h VPShellTap, offset 2048 of the main section) names a pagefile-backed section
that shell_composite.dll fills once per map load with the EXE's height field in the C5HF format
(render/include/c5r/heightfield/heightfield.h: 128-byte header, u16 rows, optional u8 rows). `consume()` follows the
ack protocol written in shell_shared.h: read the block under its seqlock, refuse a stale tap (loadSeq != the camera
block's gridSeq), open the section, check its size, the C5HF rules of c5r::heightfield::readTap, the map size against
the camera block and the tap block, and FNV-1a 64 over the payload, optionally save the payload, close the section, and
ack (also a refused copy, with its result code, so that the DLL closes the section).

Command line (exit codes are the checks; 0 = accepted):
    python shell/tests/tapcheck.py consume --section NAME [--wait S] [--no-ack] [--out FILE.c5hf] [--expect-load N]
    python shell/tests/tapcheck.py r16-to-c5hf RAW.r16 W_PLOTS H_PLOTS OUT.c5hf [--wrap]   (epic 3000's dump -> C5HF)
    python shell/tests/tapcheck.py fnv FILE                                                (FNV-1a 64 of a file)
Exit codes: 0 ok, 2 usage, 10 main section missing / not initialised / wrong version, 11 tap not READY (FAILED, OFF,
NONE or WRITING when the wait ends), 12 stale (loadSeq != camera gridSeq, or != --expect-load), 13 tap section does not
open, 14 truncated (section smaller than the payload, payload not the size the C5HF header announces), 15 C5HF format
(magic, version, layout), 16 wrong map size (C5HF vs camera block vs tap block), 17 FNV-1a 64 mismatch.
ackResult written: 0 ok, c5r::Err (2 Truncated, 3 BadMagic, 4 BadVersion, 5 BadLayout) for 14/15, -1 hash, -2 open
failed, -3 stale, -4 map size.
"""
from __future__ import annotations

import argparse
import ctypes
import os
import struct
import sys
import time
from ctypes import wintypes
from pathlib import Path
from typing import Dict, Optional, Tuple

SECTION_BYTES = 4096
MAGIC = 0x4C485356
VERSION = 3
CAMERA_OFFSET = 1024
TAP_OFFSET = 2048
CAM_GRIDSEQ, CAM_GRIDVALID, CAM_SAMPLESX, CAM_SAMPLESY = 500, 504, 692, 696   # VPShellCamera (shell_shared.h)
STATES = ["NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF"]
FAILS = ["NONE", "GRID_INVALID", "SECTION", "EXCEPTION", "TOO_BIG", "ACK_TIMEOUT", "CHANGED", "NO_SECTION"]

# VPShellTap, offsets checked against the static_asserts of shell_shared.h (payloadBytes 40, copyMs 72, copySeq 76,
# sectionName 80, ackLoadSeq 208, requestSeq 220, ackCopySeq 224, size 288)
TAP_FIELDS = [("seq", "I", 0), ("state", "I", 4), ("loadSeq", "I", 8), ("failReason", "I", 12), ("mapW", "I", 16),
              ("mapH", "I", 20), ("samplesX", "I", 24), ("samplesY", "I", 28), ("flags", "I", 32), ("exePid", "I", 36),
              ("payloadBytes", "Q", 40), ("sectionBytes", "Q", 48), ("fnv1a64", "Q", 56), ("readyQpc", "q", 64),
              ("copyMs", "f", 72), ("copySeq", "I", 76), ("ackLoadSeq", "I", 208), ("ackResult", "i", 212),
              ("ackServerPid", "I", 216), ("requestSeq", "I", 220), ("ackCopySeq", "I", 224)]
TAP_NAME_OFFSET, TAP_NAME_CHARS, TAP_SIZE = 80, 64, 288

EXIT_OK, EXIT_USAGE, EXIT_MAIN, EXIT_NOT_READY, EXIT_STALE, EXIT_OPEN = 0, 2, 10, 11, 12, 13
EXIT_TRUNCATED, EXIT_FORMAT, EXIT_MAPSIZE, EXIT_HASH = 14, 15, 16, 17
ERR_TRUNCATED, ERR_BADMAGIC, ERR_BADVERSION, ERR_BADLAYOUT = 2, 3, 4, 5
ACK_HASH, ACK_OPEN, ACK_STALE, ACK_MAPSIZE = -1, -2, -3, -4

_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.OpenFileMappingW.restype = wintypes.HANDLE
_k.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
_k.CreateFileMappingW.restype = wintypes.HANDLE
_k.CreateFileMappingW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPCWSTR]
_k.MapViewOfFile.restype = ctypes.c_void_p
_k.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
_k.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
_k.CloseHandle.argtypes = [wintypes.HANDLE]
_k.VirtualQuery.restype = ctypes.c_size_t
FILE_MAP_WRITE, FILE_MAP_READ = 0x2, 0x4
PAGE_READWRITE = 0x04


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", wintypes.DWORD),
                ("PartitionId", wintypes.WORD), ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


_k.VirtualQuery.argtypes = [ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]


class Mapping:
    """An opened named section, mapped whole (size 0) or its first `size` bytes."""

    def __init__(self, name: str, write: bool = True, size: int = 0, create: int = 0):
        if create:
            self.h = _k.CreateFileMappingW(wintypes.HANDLE(-1), None, PAGE_READWRITE, (create >> 32) & 0xFFFFFFFF,
                                           create & 0xFFFFFFFF, name)
        else:
            self.h = _k.OpenFileMappingW(FILE_MAP_READ | (FILE_MAP_WRITE if write else 0), False, name)
        if not self.h:
            raise OSError(ctypes.get_last_error(), "section %r does not open (error %d)" % (name, ctypes.get_last_error()))
        self.p = _k.MapViewOfFile(self.h, FILE_MAP_READ | (FILE_MAP_WRITE if write or create else 0), 0, 0, size)
        if not self.p:
            err = ctypes.get_last_error()
            _k.CloseHandle(self.h)
            raise OSError(err, "MapViewOfFile(%r) failed (error %d)" % (name, err))
        m = MBI()
        _k.VirtualQuery(ctypes.c_void_p(self.p), ctypes.byref(m), ctypes.sizeof(m))
        self.size = int(m.RegionSize)   # whole view: the section size rounded up to a page

    def read(self, off: int, n: int) -> bytes:
        return ctypes.string_at(self.p + off, n)

    def write(self, off: int, data: bytes) -> None:
        ctypes.memmove(self.p + off, data, len(data))

    def close(self) -> None:
        if self.p:
            _k.UnmapViewOfFile(ctypes.c_void_p(self.p))
            self.p = None
        if self.h:
            _k.CloseHandle(self.h)
            self.h = None

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def exists(name: str) -> bool:
    """True when a section of that name is open somewhere (the DLL still holds the tap section)."""
    h = _k.OpenFileMappingW(FILE_MAP_READ, False, name)
    if h:
        _k.CloseHandle(h)
        return True
    return False


def parse_tap(raw: bytes) -> Dict[str, object]:
    d: Dict[str, object] = {}
    for name, fmt, off in TAP_FIELDS:
        d[name] = struct.unpack_from("<" + fmt, raw, off)[0]
    nm = raw[TAP_NAME_OFFSET:TAP_NAME_OFFSET + 2 * TAP_NAME_CHARS].decode("utf-16-le")
    d["sectionName"] = nm.split("\0", 1)[0]
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


def read_block(m: Mapping, off: int, n: int, tries: int = 200) -> Optional[bytes]:
    """A seqlocked block (seq at +0): even, non-zero and unchanged around the copy, else None."""
    for _ in range(tries):
        s0 = struct.unpack("<I", m.read(off, 4))[0]
        raw = m.read(off, n)
        s1 = struct.unpack("<I", m.read(off, 4))[0]
        if s0 == s1 and s0 and not (s0 & 1):
            return raw
        time.sleep(0.001)
    return None


def fnv1a64(data, h: int = 0xCBF29CE484222325) -> int:
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def tap_size(map_w: int, map_h: int, with_mat: bool) -> int:
    n = map_w * 64 * map_h * 64
    return 128 + n * 2 + (n if with_mat else 0)


def check_c5hf(head: bytes, n: int) -> Tuple[int, str, Dict[str, object]]:
    """The checks of c5r::heightfield::readTap on a C5HF image of n bytes whose first 128 bytes are `head`.
    Returns (c5r::Err code, message, fields); code 0 = ok."""
    if n < 128 or len(head) < 128:
        return ERR_TRUNCATED, "tap: need 128 header bytes, have %d" % n, {}
    if head[:4] != b"C5HF":
        return ERR_BADMAGIC, "tap: magic is not C5HF", {}
    ver, mw, mh, sx, sy, flags = struct.unpack_from("<6I", head, 4)
    if ver != 1:
        return ERR_BADVERSION, "tap: version %d" % ver, {}
    f = {"mapW": mw, "mapH": mh, "samplesX": sx, "samplesY": sy, "flags": flags}
    if mw == 0 or mh == 0 or mw > 256 or mh > 256:
        return ERR_BADLAYOUT, "tap: map %dx%d" % (mw, mh), f
    if sx != mw * 64 or sy != mh * 64:
        return ERR_BADLAYOUT, "tap: samples %dx%d != 64 x map %dx%d" % (sx, sy, mw, mh), f
    if flags & ~3:
        return ERR_BADLAYOUT, "tap: unknown flags %d" % flags, f
    if struct.unpack_from("<2I", head, 120) != (0, 0):
        return ERR_BADLAYOUT, "tap: reserved words not 0", f
    f.update(zip(("sx", "sy", "sz", "ox", "oy", "waterZ"), struct.unpack_from("<6f", head, 28)))
    f["mapSeed"] = struct.unpack_from("<I", head, 52)[0]
    need = tap_size(mw, mh, bool(flags & 2))
    f["need"] = need
    if n < need:
        return ERR_TRUNCATED, "tap: need %d bytes, have %d" % (need, n), f
    return 0, "ok", f


class Result:
    def __init__(self, code: int, msg: str, tap: Optional[dict] = None, c5hf: Optional[dict] = None, ack: Optional[int] = None):
        self.code, self.msg, self.tap, self.c5hf, self.ack = code, msg, tap or {}, c5hf or {}, ack

    def line(self) -> str:
        t = self.tap
        return "tapcheck: exit %d: %s (state %s fail %s load %s copy %s section %s payload %s ack %s)" % (
            self.code, self.msg, t.get("stateName"), t.get("failName"), t.get("loadSeq"), t.get("copySeq"),
            t.get("sectionName"), t.get("payloadBytes"), self.ack)


def _ack(m: Mapping, tap: dict, result: int) -> None:
    base = TAP_OFFSET
    m.write(base + 212, struct.pack("<i", result))
    m.write(base + 216, struct.pack("<I", os.getpid()))
    m.write(base + 224, struct.pack("<I", tap["copySeq"]))
    m.write(base + 208, struct.pack("<I", tap["loadSeq"]))   # last


def consume(main: str, wait_s: float = 10.0, ack: bool = True, out: Optional[Path] = None,
            expect_load: Optional[int] = None, keep_open_s: float = 0.0) -> Result:
    try:
        m = Mapping(main, write=True, size=SECTION_BYTES)
    except OSError as e:
        return Result(EXIT_MAIN, "main section %r: %s" % (main, e.args[-1]))
    try:
        magic, version = struct.unpack("<II", m.read(0, 8))
        if magic != MAGIC or version != VERSION:
            return Result(EXIT_MAIN, "main section magic 0x%08X version %d (want 0x%08X version %d)" % (magic, version, MAGIC, VERSION))
        t_end = time.time() + wait_s
        tap: Dict[str, object] = {}
        while True:
            raw = read_block(m, TAP_OFFSET, TAP_SIZE)
            tap = parse_tap(raw) if raw else {}
            if tap and tap["state"] in (2, 4, 5):   # READY, FAILED, OFF
                break
            if time.time() > t_end:
                return Result(EXIT_NOT_READY, "no READY tap within %.1f s" % wait_s, tap)
            time.sleep(0.02)
        if tap["state"] != 2:
            return Result(EXIT_NOT_READY, "tap state %s (fail %s)" % (tap["stateName"], tap["failName"]), tap)
        cam = read_block(m, CAMERA_OFFSET, 712)
        if cam is None:
            return Result(EXIT_STALE, "camera block never stable", tap)
        grid_seq, grid_valid = struct.unpack_from("<II", cam, CAM_GRIDSEQ)
        csx, csy = struct.unpack_from("<II", cam, CAM_SAMPLESX)
        want = expect_load if expect_load is not None else grid_seq
        if tap["loadSeq"] != want or tap["loadSeq"] != grid_seq or grid_valid != 1:
            r = Result(EXIT_STALE, "stale tap: loadSeq %d, camera gridSeq %d (valid %d), expected load %d" % (
                tap["loadSeq"], grid_seq, grid_valid, want), tap)
            if ack:
                _ack(m, tap, ACK_STALE)
                r.ack = ACK_STALE
            return r
        try:
            s = Mapping(tap["sectionName"], write=False)
        except OSError as e:
            r = Result(EXIT_OPEN, "tap section %r: %s" % (tap["sectionName"], e.args[-1]), tap)
            if ack:
                _ack(m, tap, ACK_OPEN)
                r.ack = ACK_OPEN
            return r
        code, msg, ackv, fields = EXIT_OK, "ok", 0, {}
        try:
            payload = int(tap["payloadBytes"])
            if s.size < payload:
                code, msg, ackv = EXIT_TRUNCATED, "truncated tap: section holds %d bytes, payload %d" % (s.size, payload), ERR_TRUNCATED
            else:
                head = s.read(0, min(128, payload))
                err, emsg, fields = check_c5hf(head, payload)
                if err == ERR_TRUNCATED:
                    code, msg, ackv = EXIT_TRUNCATED, "truncated tap: " + emsg, err
                elif err:
                    code, msg, ackv = EXIT_FORMAT, emsg, err
                elif fields["need"] != payload:
                    code, msg, ackv = EXIT_TRUNCATED, "tap block payload %d != C5HF size %d" % (payload, fields["need"]), ERR_TRUNCATED
                elif (fields["samplesX"], fields["samplesY"]) != (csx, csy) or \
                        (fields["samplesX"], fields["samplesY"], fields["mapW"], fields["mapH"], fields["flags"]) != \
                        (tap["samplesX"], tap["samplesY"], tap["mapW"], tap["mapH"], tap["flags"]):
                    code, msg, ackv = EXIT_MAPSIZE, "wrong map size: C5HF %dx%d samples (map %dx%d, flags %d), camera %dx%d, tap block %dx%d (map %dx%d, flags %d)" % (
                        fields["samplesX"], fields["samplesY"], fields["mapW"], fields["mapH"], fields["flags"], csx, csy,
                        tap["samplesX"], tap["samplesY"], tap["mapW"], tap["mapH"], tap["flags"]), ACK_MAPSIZE
                else:
                    data = s.read(0, payload)
                    h = fnv1a64(data)
                    fields["fnv"] = h
                    if h != tap["fnv1a64"]:
                        code, msg, ackv = EXIT_HASH, "FNV-1a 64 mismatch: payload %016X, tap block %016X" % (h, tap["fnv1a64"]), ACK_HASH
                    elif out is not None:
                        Path(out).write_bytes(data)
            if keep_open_s > 0:
                time.sleep(keep_open_s)
        finally:
            s.close()
        r = Result(code, msg, tap, fields)
        if ack:
            _ack(m, tap, ackv)
            r.ack = ackv
        return r
    finally:
        m.close()


# ---- the synthetic field of tap_test=synth / small (shell_composite.cpp TapSynthBuild) -------------------------------

def synth_expected(sx: int, sy: int):
    import numpy as np
    y, x = np.mgrid[0:sy, 0:sx].astype(np.int64)
    tx, ty = x >> 4, y >> 4
    h = (31 * x + 17 * y + (x >> 4) * (y >> 4)) & 0xFFFF
    h = np.where((tx + 3 * ty) % 11 == 0, (7 * tx + 131 * ty) & 0xFFFF, h).astype(np.uint16)
    b = (x ^ (3 * y)) & 0xFF
    b = np.where((5 * tx + ty) % 13 == 0, (tx + ty) & 0xFF, b).astype(np.uint8)
    return h, b


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("consume")
    c.add_argument("--section", required=True)
    c.add_argument("--wait", type=float, default=10.0)
    c.add_argument("--no-ack", action="store_true")
    c.add_argument("--out")
    c.add_argument("--expect-load", type=int)
    r = sub.add_parser("r16-to-c5hf")
    r.add_argument("raw")
    r.add_argument("w", type=int)
    r.add_argument("h", type=int)
    r.add_argument("out")
    r.add_argument("--wrap", action="store_true")
    f = sub.add_parser("fnv")
    f.add_argument("file")
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return EXIT_USAGE if e.code else 0
    if a.cmd == "consume":
        res = consume(a.section, a.wait, not a.no_ack, Path(a.out) if a.out else None, a.expect_load)
        print(res.line())
        return res.code
    if a.cmd == "r16-to-c5hf":
        raw = Path(a.raw).read_bytes()
        n = a.w * 64 * a.h * 64
        if len(raw) != 2 * n:
            print("tapcheck: %s has %d bytes, a %dx%d plot map needs %d" % (a.raw, len(raw), a.w, a.h, 2 * n))
            return EXIT_TRUNCATED
        head = bytearray(128)
        head[0:4] = b"C5HF"
        struct.pack_into("<6I", head, 4, 1, a.w, a.h, a.w * 64, a.h * 64, 1 if a.wrap else 0)
        struct.pack_into("<5f", head, 28, 1.0, 0.866, 0.2, a.w * 32.0, a.h * 32 * 0.866)
        Path(a.out).write_bytes(bytes(head) + raw)
        print("tapcheck: wrote %s (%d bytes)" % (a.out, 128 + len(raw)))
        return 0
    if a.cmd == "fnv":
        print("%016X" % fnv1a64(Path(a.file).read_bytes()))
        return 0
    ap.print_usage()
    return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main())
