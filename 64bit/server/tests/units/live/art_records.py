"""Test-only reader of the EXE's unit art-record table (epic 0375, issue 4453; decision 3 (a) of
docs/briefs/epic-0375-units-server-scene.md). Read-only (ReadProcessMemory; nothing is written), Python only, never
built into a shipped binary (D144/D147), and refuses any process that is not the DX11 EXE of a pool instance.

    python -I server/tests/units/live/art_records.py --instance c5h-c [--json OUT] [--tag NAME]
    python -I server/tests/units/live/art_records.py --pid PID            # must be a pool instance's EXE
    python -I server/tests/units/live/art_records.py --instance c5h-c --discover UNITS.json   # key offsets (12.7.6)
    python -I server/tests/units/live/art_records.py --instance c5h-c --art [--tag NAME]  # the art define per record

Layout read (research/05-graphics-composition/unit-formations.md 12.1 and 12.7.6, nothing else): the Unit system
(UnitSim, the static object `ar_unitsim` of exebuild/symbols/tools.tsv, `addresses.md`; its VA in the running build
from tools/exebuild/resolve.py, ticket 0593: build 4390913 from the catalogue, another known build from its local
table, setting exe_tables) holds the art-record container at +0xC8: free-list head
+0xC8 (0x0FFFFFFF = empty), live count +0xCC, array +0xD0, size +0xD4, capacity +0xD8, live list count +0xDC, first
+0xE0, last +0xE4. Element 0x344 bytes: +0 next (live list while live, free list when free), +4 bit 31 in use and the
low 31 bits the previous live index, +8 the record proper; the seed at element +0x30C. The unit key inside the record:
12.7.6 (owner and unit id offsets, established live by this tool's --discover on 2026-11-16).

Output per sample (stdout, one line each; --json writes the same as JSON):
  T <tag> size <n> live <n> capacity <n> freehead <i|-1>
  R <record index> <owner> <unit id> <seed>        for every live record, in live-list (creation) order
  F <i> <i> ...                                    the free list from its head
  --art (issue 4469): A <record index> <owner> <unit id> <path>=<ART_DEF_...> ... for every live record: every
  ASCII "ART_DEF_" name reached from the element by a pointer (path +OFF) or a pointer in the first 0x80 bytes of the
  pointed-to block (path +OFF>+OFF2); "-" when none. A scan of data, no layout assumed beyond 12.1's element.
Exit codes: 0 read, 2 usage, 3 refused (not a pool instance's DX11 EXE, or no such instance / process),
4 the table failed a consistency check (named), 5 a read failed.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import re
import struct
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
SYM_UNITSIM = "ar_unitsim"       # catalogue name (exebuild/symbols/tools.tsv): UnitSimSystem static object


def _resolve():
    """tools/exebuild/resolve.py (ticket 0593): the known builds and each build's EXE addresses."""
    import importlib.util
    m = sys.modules.get("c5_exebuild_resolve")
    if m is None:
        spec = importlib.util.spec_from_file_location("c5_exebuild_resolve",
                                                      str(REPO / "tools" / "exebuild" / "resolve.py"))
        m = importlib.util.module_from_spec(spec)
        sys.modules["c5_exebuild_resolve"] = m
        spec.loader.exec_module(m)
    return m


resolve = _resolve()
IMAGE_BASE = resolve.IMAGE_BASE
UNITSIM_VA = resolve.table.anchor(SYM_UNITSIM)   # its VA in build 4390913 (a Proc of another build carries its own)
C_FREEHEAD, C_LIVE, C_ARRAY, C_SIZE, C_CAP, C_LCOUNT, C_LFIRST, C_LLAST = (0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4)
ELEM = 0x344
E_SEED = 0x30C
NONE = 0x0FFFFFFF
# 12.7.6 (established by --discover, 2026-11-16): the unit key in the element (offsets from the element's start).
E_OWNER = 0x2F8                  # int32, the owner (PlayerTypes)
E_ID = 0x2F4                     # int32, the unit id
MAX_RECORDS = 1 << 16

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                           ctypes.POINTER(wintypes.DWORD)]
psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD,
                                       ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_VM_READ = 0x1000, 0x10


class Refused(Exception):
    pass


class Inconsistent(Exception):
    pass


def pool_instances():
    cp = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "harness" / "pool.py"), "list", "--json"],
                        capture_output=True, text=True, creationflags=0x08000000)
    if cp.returncode:
        raise Refused("pool.py list failed: %s" % cp.stderr.strip()[-300:])
    return json.loads(cp.stdout)


def image_path(h):
    buf = ctypes.create_unicode_buffer(1024)
    n = wintypes.DWORD(1024)
    if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
        raise Refused("QueryFullProcessImageNameW failed: %d" % ctypes.get_last_error())
    return Path(buf.value)


class Proc:
    def __init__(self, pid):
        self.pid = pid
        self.h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, int(pid))
        if not self.h:
            raise Refused("OpenProcess(%d) failed: %d" % (pid, ctypes.get_last_error()))

    def close(self):
        if self.h:
            k32.CloseHandle(self.h)
            self.h = None

    def rd(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            raise OSError("ReadProcessMemory(%#x, %d) failed: %d" % (addr, n, ctypes.get_last_error()))
        return buf.raw

    def u32(self, addr):
        return struct.unpack("<I", self.rd(addr, 4))[0]

    def base(self):
        mods = (wintypes.HMODULE * 1)()
        need = wintypes.DWORD(0)
        if not psapi.EnumProcessModulesEx(self.h, mods, ctypes.sizeof(mods), ctypes.byref(need), 0x01):
            raise Refused("EnumProcessModulesEx failed: %d" % ctypes.get_last_error())
        return mods[0] or 0


def open_checked(pid=None, instance=None):
    """The pool check: the process must be alive in a pool instance's record, its image the DX11 EXE inside that
    instance's folder, and its PE timestamp the DX11 build. Anything else is refused (exit 3)."""
    insts = pool_instances()
    if instance is not None:
        rows = [r for r in insts if r.get("name") == instance]
        if not rows:
            raise Refused("no pool instance named %r" % instance)
        cands = [int(p) for p in rows[0].get("alive") or []]
    else:
        rows = [r for r in insts if int(pid) in [int(p) for p in (r.get("alive") or [])]]
        if not rows:
            raise Refused("pid %d is not a process of any pool instance (pool.py list)" % pid)
        cands = [int(pid)]
    inst_dir = Path(rows[0]["dir"]).resolve()
    for c in cands:
        try:
            p = Proc(c)
        except Refused:
            continue
        try:
            img = image_path(p.h).resolve()
            if img.name.lower() != "civilizationv_dx11.exe" or inst_dir not in img.parents:
                p.close()
                continue
            b = p.base()
            try:
                ts, soi, _machine = resolve.header_of_image(p.rd, b)
                p.unitsim_va = resolve.for_header(ts, soi).va(SYM_UNITSIM)
            except (resolve.UnknownBuild, resolve.TableError, resolve.NotCarried) as e:
                p.close()
                raise Refused("pid %d: image %s: %s" % (c, img, e))
            p.load_base = b   # the EXE may be loaded above 0x400000 (seen 0x4A0000 with the harness wrapper, 12.7.6)
            return p, rows[0]["name"]
        except OSError as e:
            p.close()
            raise Refused("pid %d: %s" % (c, e))
    raise Refused("no CivilizationV_DX11.exe of pool instance %s among pids %s" % (rows[0]["name"], cands))


def read_table(p, raw=False):
    um = getattr(p, "unitsim_va", UNITSIM_VA) - IMAGE_BASE + p.load_base
    head, live, arr, size, cap, lcount, lfirst, llast = struct.unpack("<8I", p.rd(um + C_FREEHEAD, 32))
    t = {"freehead": -1 if head == NONE else head, "live": live, "array": arr, "size": size, "capacity": cap,
         "lcount": lcount, "lfirst": lfirst, "llast": llast}
    if not (size <= cap <= MAX_RECORDS) or live > size or (size and not arr):
        raise Inconsistent("header: size %d capacity %d live %d array %#x" % (size, cap, live, arr))
    data = p.rd(arr, size * ELEM) if size else b""
    recs, seen = [], set()
    i = lfirst if lcount else NONE
    prev = None
    while i != NONE and i < size:
        if i in seen:
            raise Inconsistent("live list loops at %d" % i)
        seen.add(i)
        e = data[i * ELEM:(i + 1) * ELEM]
        nxt, w4 = struct.unpack_from("<II", e, 0)
        if not (w4 & 0x80000000):
            raise Inconsistent("live record %d not marked in use" % i)
        seed = struct.unpack_from("<I", e, E_SEED)[0]
        r = {"index": i, "seed": seed}
        if E_OWNER is not None:
            r["owner"] = struct.unpack_from("<i", e, E_OWNER)[0]
            r["id"] = struct.unpack_from("<i", e, E_ID)[0]
        if raw:
            r["raw"] = e
        recs.append(r)
        prev = i
        i = nxt
    if len(recs) != live:
        raise Inconsistent("live list holds %d records, live count %d" % (len(recs), live))
    if live and prev != llast:
        raise Inconsistent("live list ends at %s, last %d" % (prev, llast))
    free, fseen = [], set()
    i = head
    while i != NONE:
        if i >= size or i in fseen or i in seen:
            raise Inconsistent("free list broken at %d" % i)
        fseen.add(i)
        free.append(i)
        i = struct.unpack_from("<I", data, i * ELEM)[0]
    if len(free) + live != size:
        raise Inconsistent("free %d + live %d != size %d" % (len(free), live, size))
    return t, recs, free


ART_NAME = re.compile(rb"ART_DEF_[A-Z0-9_]+")


def art_names(p, raw, depth2=0x80):
    """Issue 4469: the "ART_DEF_" names one element reaches through pointers (see --art). [(path, name), ...] in element
    offset order; a pointer that cannot be read is skipped."""
    def name_at(a):
        try:
            m = ART_NAME.match(p.rd(a, 64))
        except OSError:
            return None
        return m.group().decode("ascii") if m else None

    out = []
    for off in range(8, ELEM - 3, 4):
        d = struct.unpack_from("<I", raw, off)[0]
        if d < 0x10000 or d >= 0x7FFF0000:
            continue
        s = name_at(d)
        if s:
            out.append(("+%#x" % off, s))
            continue
        try:
            blk = p.rd(d, depth2)
        except OSError:
            continue
        for o2 in range(0, depth2, 4):
            d2 = struct.unpack_from("<I", blk, o2)[0]
            if 0x10000 <= d2 < 0x7FFF0000:
                s2 = name_at(d2)
                if s2:
                    out.append(("+%#x>+%#x" % (off, o2), s2))
    return out


def discover(recs, units):
    """Find the element offsets of the unit id (int32) and the owner (int32 or int8) such that every live record's
    (owner, id) is one of the game's units (UNITS.json: [[owner, id], ...] from the Lua channel) and all are distinct."""
    want = {(int(o), int(i)) for o, i in units}
    ids = {i for _, i in want}
    out = []
    for oid in range(8, ELEM - 3, 4):
        vals = [struct.unpack_from("<i", r["raw"], oid)[0] for r in recs]
        if len(set(vals)) != len(vals) or not set(vals) <= ids:
            continue
        for oo in range(8, ELEM):
            if oo == oid:
                continue
            ows = [r["raw"][oo] if r["raw"][oo] < 128 else r["raw"][oo] - 256 for r in recs]
            if all((ow, v) in want for ow, v in zip(ows, vals)):
                wide = oo % 4 == 0 and all(struct.unpack_from("<i", r["raw"], oo)[0] == ow for r, ow in zip(recs, ows))
                out.append({"id_offset": oid, "owner_offset": oo, "owner_int32": wide})
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--instance")
    g.add_argument("--pid", type=int)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--tag", default="sample")
    ap.add_argument("--discover", type=Path, help="UNITS.json [[owner, id], ...]: print the key offsets")
    ap.add_argument("--art", action="store_true", help="also the art define names each record reaches (issue 4469)")
    a = ap.parse_args(argv)
    try:
        p, name = open_checked(pid=a.pid, instance=a.instance)
    except Refused as e:
        print("art_records: REFUSED: %s" % e)
        return 3
    try:
        t, recs, free = read_table(p, raw=bool(a.discover) or a.art)
        if a.art:
            for r in recs:
                r["art"] = art_names(p, r["raw"])
    except Inconsistent as e:
        print("art_records: INCONSISTENT: %s" % e)
        return 4
    except OSError as e:
        print("art_records: READ FAILED: %s" % e)
        return 5
    finally:
        p.close()
    if a.discover:
        units = json.loads(a.discover.read_text(encoding="utf-8"))
        cands = discover(recs, units)
        print(json.dumps({"instance": name, "live": len(recs), "units": len(units), "candidates": cands}))
        return 0 if cands else 4
    print("T %s size %d live %d capacity %d freehead %d" % (a.tag, t["size"], t["live"], t["capacity"], t["freehead"]))
    for r in recs:
        print("R %d %s %s %d" % (r["index"], r.get("owner", "?"), r.get("id", "?"), r["seed"]))
    print("F" + "".join(" %d" % i for i in free))
    if a.art:
        for r in recs:
            print("A %d %s %s %s" % (r["index"], r.get("owner", "?"), r.get("id", "?"),
                                     " ".join("%s=%s" % x for x in r["art"]) or "-"))
    for r in recs:
        r.pop("raw", None)
    if a.json:
        a.json.write_text(json.dumps({"tag": a.tag, "instance": name, "table": t, "records": recs, "free": free}),
                          encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
