"""The section's camera block (shell/common/shell_shared.h VPShellCamera, issue 3007) from Python.

`FIELDS` mirrors the C struct (checked against `shell_server.exe --print-layout` by test_camera.py). `Section` creates or
opens a named section of VPSHELL_SECTION_BYTES and writes a camera block into it the way shell_composite.dll does
(seqlock), so the offline accuracy test can hand the server a ground-truth camera without the game or the DLL.
"""
from __future__ import annotations

import ctypes
import struct
from ctypes import wintypes
from typing import Dict

SECTION_BYTES = 4096
CAMERA_OFFSET = 1024
STATUS = ["NONE", "OK", "OFF", "REFUSED", "NO_GVS", "BAD_GVS", "NOT_MAP", "INCONSISTENT", "TORN", "EXCEPTION"]

FIELDS = [
    ("seq", "I"), ("status", "I"), ("presentCount", "q"), ("frameStamp", "q"), ("readQpc", "q"), ("exeBase", "I"),
    ("viewMode", "I"), ("numViews", "I"), ("flags", "I"), ("proj", "16f"), ("view", "64f"), ("camWorld", "16f"),
    ("fovDeg", "f"), ("aspect", "f"), ("nearZ", "f"), ("farZ", "f"), ("vpX", "I"), ("vpY", "I"), ("vpW", "I"),
    ("vpH", "I"), ("vpMinZ", "f"), ("vpMaxZ", "f"), ("bbW", "I"), ("bbH", "I"), ("consistRot", "f"),
    ("consistTrans", "f"), ("tornRetries", "I"), ("readsOk", "I"), ("readsBad", "I"), ("gridSeq", "I"),
    ("gridValid", "I"), ("wrapX", "i"), ("wrapPeriod", "f"), ("aabbMin", "3f"), ("aabbMax", "3f"),
    ("worldHeight", "f"), ("w2h", "16f"), ("h2w", "16f"), ("sx", "f"), ("sy", "f"), ("heightScale", "f"),
    ("ox", "f"), ("oy", "f"), ("samplesX", "I"), ("samplesY", "I"), ("heightmapAddr", "I"), ("pad0", "I"),
]


def offsets() -> Dict[str, int]:
    out, at = {}, 0
    for name, fmt in FIELDS:
        out[name] = at
        at += struct.calcsize("<" + fmt)
    out["_end"] = at
    return out


def _flat(m):
    return [float(x) for row in m for x in row]


_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.CreateFileMappingW.restype = wintypes.HANDLE
_k.CreateFileMappingW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPCWSTR]
_k.MapViewOfFile.restype = ctypes.c_void_p
_k.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
_k.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
_k.CloseHandle.argtypes = [wintypes.HANDLE]
INVALID_HANDLE_VALUE = wintypes.HANDLE(-1)
PAGE_READWRITE = 0x04
FILE_MAP_ALL_ACCESS = 0xF001F


class Section:
    def __init__(self, name: str):
        self.h = _k.CreateFileMappingW(INVALID_HANDLE_VALUE, None, PAGE_READWRITE, 0, SECTION_BYTES, name)
        if not self.h:
            raise OSError("CreateFileMappingW(%s) failed: %d" % (name, ctypes.get_last_error()))
        self.p = _k.MapViewOfFile(self.h, FILE_MAP_ALL_ACCESS, 0, 0, SECTION_BYTES)
        if not self.p:
            raise OSError("MapViewOfFile failed: %d" % ctypes.get_last_error())
        self.seq = 0

    def write(self, offset: int, data: bytes) -> None:
        ctypes.memmove(self.p + offset, data, len(data))

    def read(self, offset: int, n: int) -> bytes:
        return ctypes.string_at(self.p + offset, n)

    def write_camera(self, values: Dict[str, object]) -> None:
        """Publish a camera block: seq odd, fields, seq even (as the DLL does)."""
        o = offsets()
        self.seq += 1
        self.write(CAMERA_OFFSET, struct.pack("<I", self.seq | 1))
        for name, fmt in FIELDS:
            if name in ("seq",) or name not in values:
                continue
            v = values[name]
            vals = v if isinstance(v, (list, tuple)) else [v]
            self.write(CAMERA_OFFSET + o[name], struct.pack("<" + fmt, *vals))
        self.seq = (self.seq | 1) + 1
        self.write(CAMERA_OFFSET, struct.pack("<I", self.seq))

    def close(self) -> None:
        if self.p:
            _k.UnmapViewOfFile(self.p)
            self.p = None
        if self.h:
            _k.CloseHandle(self.h)
            self.h = None


def block_from_ground_truth(cam: dict, grid: dict) -> Dict[str, object]:
    """A camera block from gt_reader.py's camera JSON and grid_constants.json (status OK, one Present)."""
    vp = cam["viewport"]
    g = cam.get("gvs_camera") or {}
    views = [_flat(cam["views"][i]) for i in range(4)]
    hh = grid["heightmap_header"]
    return {
        "status": 1, "presentCount": 1, "frameStamp": 1, "readQpc": 0, "exeBase": int(cam["exe_base"], 16),
        "viewMode": int(g.get("view_mode", 1)), "numViews": int(cam["num_views"]), "flags": 0,
        "proj": _flat(cam["proj"]), "view": [x for v in views for x in v], "camWorld": _flat(g["cam_to_world"]),
        "fovDeg": g["fov_y_deg"], "aspect": g["aspect"], "nearZ": g["near"], "farZ": g["far"],
        "vpX": vp["X"], "vpY": vp["Y"], "vpW": vp["Width"], "vpH": vp["Height"], "vpMinZ": vp["MinZ"],
        "vpMaxZ": vp["MaxZ"], "bbW": cam["backbuffer"][0], "bbH": cam["backbuffer"][1],
        "gridSeq": 1, "gridValid": 1, "wrapX": grid["wrap_x"], "wrapPeriod": grid["wrap_period"],
        "aabbMin": grid["aabb_min"], "aabbMax": grid["aabb_max"], "worldHeight": grid["world_height"],
        "w2h": _flat(grid["M_w2h"]), "h2w": _flat(grid["M_h2w"]), "sx": grid["grid_sx"], "sy": grid["grid_sy"],
        "heightScale": grid["grid_height_scale"], "ox": grid["grid_ox"], "oy": grid["grid_oy"],
        "samplesX": hh["samples_x_0C"], "samplesY": hh["samples_y_10"], "heightmapAddr": 0,
    }
