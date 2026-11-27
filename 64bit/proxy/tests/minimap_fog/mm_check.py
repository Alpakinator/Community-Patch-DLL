"""Ticket 0727: read the EXE minimap's active block in a running game (read only) and check it the way the fix needs it:
the counts of unexplored cells (whole map, and a block of plots), the box, the transform, the defer and redraw words,
and every cell's six edge bits against its neighbours' bit 0 (the invariant the fog handler and the fix both keep,
research/05-graphics-composition/observer-switch-minimap.md 6.6). Prints counts and fields, never bytes of the EXE's
code and never an address of a build other than 4390913 (D83, D202).

    python -I proxy/tests/minimap_fog/mm_check.py --pid PID [--block X0 Y0 W H] [--expect-block explored|unexplored]
        [--expect-box C0 R0 C1 R1]

The four .data items come from the build table (exebuild/symbols/proxy.tsv: mmf_slot, mmf_blocks, mmf_system, mmf_wrap)
through tools/exebuild/resolve.py for the build the process's own header names.
The neighbour of (column, row) in direction d uses a table per row parity (the same six directions as the handler's).
Exit 0 read ok and every expectation held (edges consistent, the transform the identity, --expect-* as given), 1 an
expectation failed, 2 bad arguments, 3 the process or a field could not be read.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(REPO, 'tools', 'exebuild'))
import resolve  # noqa: E402

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, wt.LPCVOID, wt.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
EVEN = [(0, 1), (-1, 0), (0, -1), (-1, -1), (1, 0), (-1, 1)]
ODD = [(1, 1), (-1, 0), (1, -1), (0, -1), (1, 0), (0, 1)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--block", type=int, nargs=4, metavar=("X0", "Y0", "W", "H"))
    ap.add_argument("--expect-block", choices=("explored", "unexplored"))
    ap.add_argument("--expect-box", type=int, nargs=4, metavar=("C0", "R0", "C1", "R1"))
    a = ap.parse_args()
    if a.expect_block and not a.block:
        print("--expect-block needs --block")
        return 2
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, False, a.pid)
    if not h:
        print("cannot open pid %d (error %d)" % (a.pid, ctypes.get_last_error()))
        return 3

    def rd(addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            raise OSError("a read of %d bytes failed (error %d)" % (n, ctypes.get_last_error()))
        return buf.raw

    def u32(addr):
        return struct.unpack("<I", rd(addr, 4))[0]

    bad = 0
    try:
        psapi = ctypes.WinDLL("psapi")
        mods = (ctypes.c_void_p * 1024)()
        need = wt.DWORD()
        if not psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3) or not mods[0]:
            print("cannot list the modules of pid %d" % a.pid)
            return 3
        base = mods[0]
        stamp = None
        try:
            pe = u32(base + 0x3C)
            stamp = u32(base + pe + 8)
            soi = u32(base + pe + 24 + 56)
            rs = resolve.for_header(stamp, soi)
        except resolve.UnknownBuild as e:
            print(str(e))
            return 3
        print("build: %s" % rs.label)
        slot_a, list_a, sys_a, wrap_a = (rs.at(base, n) for n in ("mmf_slot", "mmf_blocks", "mmf_system", "mmf_wrap"))
        slot = struct.unpack("<i", rd(slot_a, 4))[0]
        defer, redraw = struct.unpack("<II", rd(sys_a + 0x18, 8))
        wrap_box = rd(wrap_a, 1)[0]
        sentinel = u32(list_a)
        node, slots, blk = u32(sentinel), [], None
        for _ in range(80):
            if node == sentinel or node == 0:
                break
            s = struct.unpack("<i", rd(node + 8, 4))[0]
            slots.append(s)
            if s == slot:
                blk = node + 0xC
            node = u32(node)
        print("active slot %d; blocks for slots %s; defer bit %d; redraw word 0x%X; handler wrap byte %d"
              % (slot, slots, defer & 1, redraw, wrap_box))
        if blk is None:
            print("no block for the active slot")
            return 3
        m = struct.unpack("<16i", rd(blk, 64))
        # (x, y, -x-y) -> (q, r) must be (x, y) (the fix's check; the matrix itself need not be the identity)
        ident = (m[0] - m[8], m[4] - m[8], m[12], m[1] - m[9], m[5] - m[9], m[13]) == (1, 0, 0, 0, 1, 0)
        print("transform (q, r rows %s, %s): %s" % (m[0:13:4], m[1:14:4], "(x, y, -x-y) -> (x, y)" if ident else
                                                 "NOT (x, y, -x-y) -> (x, y)"))
        bad += not ident
        w, hgt = struct.unpack("<ii", rd(blk + 0x94, 8))
        wrap = rd(blk + 0x9C, 1)[0]
        box = struct.unpack("<6i", rd(blk + 0x400B0, 24))
        none = rd(blk + 0x400C8, 1)[0]
        cells = rd(blk + 0xA0, 256 * hgt * 4)
        spare = rd(blk + 0x400A0 + 2, 1)[0]

        def flags(c, r):
            if r < 0 or r >= hgt or (not wrap and (c < 0 or c >= w)):
                return spare
            return cells[(r * 256 + (c % w)) * 4 + 2]

        hidden = sum(flags(x, y) & 1 for y in range(hgt) for x in range(w))
        print("grid %dx%d wrap=%d no_plot_yet=%d box first %d,%d low %d,%d high %d,%d" % ((w, hgt, wrap, none) + box))
        print("cells marked unexplored: %d of %d" % (hidden, w * hgt))
        errs = 0
        for r in range(hgt):
            for c in range(w):
                f = flags(c, r)
                t = ODD if r & 1 else EVEN
                for d in range(6):
                    g = flags(c + t[d][0], r + t[d][1])
                    if bool((f >> (d + 2)) & 1) != bool((f ^ g) & 1):
                        errs += 1
        print("edge bits inconsistent with the neighbours: %d of %d" % (errs, w * hgt * 6))
        bad += errs != 0
        if a.block:
            x0, y0, bw, bh = a.block
            pts = [((x0 + dx) % w, y0 + dy) for dy in range(bh) for dx in range(bw)]
            n = sum(flags(x, y) & 1 for x, y in pts)
            print("block x0=%d y0=%d %dx%d: %d of %d cells marked unexplored" % (x0, y0, bw, bh, n, len(pts)))
            if a.expect_block:
                want = len(pts) if a.expect_block == "unexplored" else 0
                ok = n == want
                print("%s the block is %s" % ("ok  " if ok else "FAIL", a.expect_block))
                bad += not ok
        if a.expect_box:
            ok = (box[2], box[3], box[4], box[5]) == tuple(a.expect_box)
            print("%s the box is low %d,%d high %d,%d" % (("ok  " if ok else "FAIL",) + tuple(a.expect_box)))
            bad += not ok
    except (OSError, resolve.NotCarried, resolve.TableError) as e:
        print(str(e))
        return 3
    finally:
        k32.CloseHandle(h)
    print("mm_check: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
