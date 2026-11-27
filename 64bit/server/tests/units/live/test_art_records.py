"""Tests of the test-only art-record reader (issue 4453). No game needed.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest server/tests/units/live/test_art_records.py -q

The table walk and its consistency checks run on a synthetic process image (12.1's layout); the pool check is run for
real: the reader must refuse a process that is not a pool instance's EXE (exit 3) and an unknown instance (exit 3).
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import art_records as AR  # noqa: E402

TOOL = HERE / "art_records.py"
NOWIN = 0x08000000


class FakeProc:
    """A process image: the UnitSim object at 0x2D6F5F8 (load base 0x400000) and an element array."""

    def __init__(self, live, free, size, arr=0x10000000, cap=None):
        self.load_base = AR.IMAGE_BASE
        self.mem = {}
        cap = cap if cap is not None else max(size, 1)
        head = free[0] if free else AR.NONE
        lfirst = live[0][0] if live else AR.NONE
        llast = live[-1][0] if live else AR.NONE
        hdr = struct.pack("<8I", head, len(live), arr, size, cap, len(live), lfirst, llast)
        self.mem[AR.UNITSIM_VA + AR.C_FREEHEAD] = hdr
        data = bytearray(size * AR.ELEM)
        for k, (i, owner, uid) in enumerate(live):
            nxt = live[k + 1][0] if k + 1 < len(live) else AR.NONE
            prv = live[k - 1][0] if k else 0x7FFFFFFF
            struct.pack_into("<II", data, i * AR.ELEM, nxt, 0x80000000 | prv)
            struct.pack_into("<I", data, i * AR.ELEM + AR.E_SEED, i)
            struct.pack_into("<ii", data, i * AR.ELEM + AR.E_ID, uid, owner)
        for k, i in enumerate(free):
            nxt = free[k + 1] if k + 1 < len(free) else AR.NONE
            struct.pack_into("<II", data, i * AR.ELEM, nxt, 0)
        self.mem[arr] = bytes(data)

    def rd(self, addr, n):
        for base, b in self.mem.items():
            if base <= addr and addr + n <= base + len(b):
                return b[addr - base:addr - base + n]
        raise OSError("unmapped %#x" % addr)


def test_layout_constants_are_the_kb_ones():
    assert (AR.C_FREEHEAD, AR.C_LIVE, AR.C_ARRAY, AR.C_SIZE, AR.C_CAP) == (0xC8, 0xCC, 0xD0, 0xD4, 0xD8)
    assert (AR.ELEM, AR.E_SEED, AR.E_ID, AR.E_OWNER) == (0x344, 0x30C, 0x2F4, 0x2F8)


def test_read_table_walks_live_and_free_lists():
    p = FakeProc(live=[(2, 0, 1002), (0, 63, 1030), (3, 1, 1005)], free=[1, 4], size=5)
    t, recs, free = AR.read_table(p)
    assert [(r["index"], r["owner"], r["id"], r["seed"]) for r in recs] == [(2, 0, 1002, 2), (0, 63, 1030, 0),
                                                                            (3, 1, 1005, 3)]
    assert free == [1, 4] and t["size"] == 5 and t["freehead"] == 1


def test_rebased_exe():
    p = FakeProc(live=[(0, 0, 7)], free=[], size=1)
    p.mem = {k + 0xA0000 if k == AR.UNITSIM_VA + AR.C_FREEHEAD else k: v for k, v in p.mem.items()}
    p.load_base = 0x4A0000
    t, recs, free = AR.read_table(p)
    assert recs[0]["id"] == 7


@pytest.mark.parametrize("bad, text", [
    ("count", "live list holds"),
    ("loop", "loops"),
    ("notinuse", "not marked in use"),
    ("free", "free list broken"),
    ("header", "header"),
])
def test_inconsistent_tables_are_refused(bad, text):
    p = FakeProc(live=[(0, 0, 1), (1, 0, 2)], free=[2], size=3)
    hdr_addr = AR.UNITSIM_VA + AR.C_FREEHEAD
    arr = 0x10000000
    data = bytearray(p.mem[arr])
    hdr = bytearray(p.mem[hdr_addr])
    if bad == "count":
        struct.pack_into("<I", hdr, 4, 1)          # live count 1, list holds 2
    elif bad == "loop":
        struct.pack_into("<I", data, 1 * AR.ELEM, 0)   # record 1's next -> 0
    elif bad == "notinuse":
        struct.pack_into("<I", data, 1 * AR.ELEM + 4, 0)
    elif bad == "free":
        struct.pack_into("<I", data, 2 * AR.ELEM, 0)   # free 2 -> live 0
    elif bad == "header":
        struct.pack_into("<I", hdr, 12, 9)         # size 9 > capacity 3
    p.mem[arr] = bytes(data)
    p.mem[hdr_addr] = bytes(hdr)
    with pytest.raises(AR.Inconsistent) as e:
        AR.read_table(p)
    assert text in str(e.value)


def test_discover_finds_the_key_offsets():
    p = FakeProc(live=[(0, 0, 1002), (1, 63, 1030), (2, 1, 1005)], free=[], size=3)
    _, recs, _ = AR.read_table(p, raw=True)
    c = AR.discover(recs, [[0, 1002], [63, 1030], [1, 1005]])
    assert {"id_offset": AR.E_ID, "owner_offset": AR.E_OWNER, "owner_int32": True} in c


def test_art_names_follows_one_and_two_pointers():
    """Issue 4469 (--art): a name at a pointer of the element (+0x10), one behind a pointer in the pointed-to block
    (+0x20 > +0x8); a pointer to unmapped memory, a pointer to a non-name and a small integer are skipped."""
    p = FakeProc(live=[(0, 0, 1)], free=[], size=1)
    arr = 0x10000000
    data = bytearray(p.mem[arr])
    struct.pack_into("<I", data, 0x10, 0x20000000)      # -> "ART_DEF_UNIT_GALLEY\0"
    struct.pack_into("<I", data, 0x20, 0x20001000)      # -> block whose +8 -> "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE"
    struct.pack_into("<I", data, 0x24, 0x30000000)      # unmapped
    struct.pack_into("<I", data, 0x28, 0x20002000)      # -> "NOT_A_NAME"
    struct.pack_into("<I", data, 0x2C, 7)               # an integer
    p.mem[arr] = bytes(data)
    p.mem[0x20000000] = b"ART_DEF_UNIT_GALLEY\0" + bytes(64)
    blk = bytearray(0x80)
    struct.pack_into("<I", blk, 8, 0x20003000)
    p.mem[0x20001000] = bytes(blk)
    p.mem[0x20002000] = b"NOT_A_NAME" + bytes(0x80)
    p.mem[0x20003000] = b"ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE\0" + bytes(64)
    _, recs, _ = AR.read_table(p, raw=True)
    assert AR.art_names(p, recs[0]["raw"]) == [("+0x10", "ART_DEF_UNIT_GALLEY"),
                                               ("+0x20>+0x8", "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE")]


def test_art_names_none():
    p = FakeProc(live=[(0, 0, 1)], free=[], size=1)
    _, recs, _ = AR.read_table(p, raw=True)
    assert AR.art_names(p, recs[0]["raw"]) == []


def run(args):
    return subprocess.run([sys.executable, "-I", str(TOOL)] + args, capture_output=True, text=True,
                          creationflags=NOWIN, timeout=300)


def test_refuses_a_process_that_is_not_a_pool_instance():
    cp = run(["--pid", str(os.getpid())])
    assert cp.returncode == 3, cp.stdout + cp.stderr
    assert "REFUSED" in cp.stdout and "not a process of any pool instance" in cp.stdout


def test_refuses_an_unknown_instance():
    cp = run(["--instance", "no-such-instance-4453"])
    assert cp.returncode == 3 and "no pool instance named" in cp.stdout


def test_usage_error():
    cp = run([])
    assert cp.returncode == 2
