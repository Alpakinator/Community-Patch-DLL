"""Live driver of the height-field tap (issue 3203, epic 3200) on a harness instance. Never launches or stops the game
(that is `tools/harness/cli.py`'s), never clicks, never writes into the game's memory.

    python shell/tests/tap_live.py config --instance c5h-d --arm d33|stock     # darkred.py's D33 set on this instance
    python shell/tests/tap_live.py consumer --instance c5h-d --section NAME --out DIR [--seconds S]
        creates the main section NAME (magic, version 3; no server), then takes every READY tap the composite DLL
        publishes (tapcheck.consume: checks, FNV-1a 64, payload saved as DIR/tap_l<load>_c<copy>.c5hf, ack) and
        samples the EXE's memory (tools/harness/memory.py) before the copy, at READY, after the ack and 3 s later,
        plus the system commit charge; events as JSON lines in DIR/consumer.jsonl. Ends after S seconds, when the
        game's pid is gone, or when DIR/stop exists.
    python shell/tests/tap_live.py rawdump --instance c5h-d --out DIR
        P3: the GridHeightmap's raw blocks read from outside by ReadProcessMemory (object 0xC8 B, both directories and
        pools, M_h2w, the wrap flag), as `hfbake decode` takes them: DIR/grid.bin dir16.bin pool16.bin dir8.bin
        pool8.bin h2w.bin, DIR/rawdump.json.
    python shell/tests/tap_live.py reload --instance c5h-d --save NAME
        a second load in the same process: Events.PlayerChoseToLoadGame(<instance save>) from a one-shot
        Events.LocalMachineAppUpdate handler of the InGame Lua state (the in-game load menu's own call).
The D33 set and its marker are darkred.py's (shell/tools/darkred.py hard-codes instance c5h-b; `config` points its
paths at --instance and calls its set_config). Back the files up by hash first (tools/harness/backup.py).
"""
from __future__ import annotations

import argparse
import ctypes
import json
import struct
import sys
import time
from ctypes import wintypes
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for p in (str(ROOT), str(HERE), str(ROOT / "shell" / "tools")):
    if p not in sys.path:
        sys.path.insert(0, p)

import tapcheck  # noqa: E402


def now():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


class PERFORMANCE_INFORMATION(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("CommitTotal", ctypes.c_size_t), ("CommitLimit", ctypes.c_size_t),
                ("CommitPeak", ctypes.c_size_t), ("PhysicalTotal", ctypes.c_size_t),
                ("PhysicalAvailable", ctypes.c_size_t), ("SystemCache", ctypes.c_size_t),
                ("KernelTotal", ctypes.c_size_t), ("KernelPaged", ctypes.c_size_t),
                ("KernelNonpaged", ctypes.c_size_t), ("PageSize", ctypes.c_size_t),
                ("HandleCount", wintypes.DWORD), ("ProcessCount", wintypes.DWORD), ("ThreadCount", wintypes.DWORD)]


def commit_mb():
    pi = PERFORMANCE_INFORMATION()
    pi.cb = ctypes.sizeof(pi)
    ctypes.WinDLL("psapi").GetPerformanceInfo(ctypes.byref(pi), pi.cb)
    return round(pi.CommitTotal * pi.PageSize / 2**20, 1)


def inst_pid(inst):
    st = inst.load_state()
    return int(st["pid"]) if st and st.get("pid") else None


def mem(pid):
    from tools.harness import memory
    try:
        s = memory.sample(pid)
    except OSError as e:
        return {"error": str(e)}
    keep = {k: s[k] for k in ("private_mb", "ws_mb", "committed_mb", "reserved_mb", "free_mb", "largest_free_mb",
                              "largest_free_below_2g_mb") if k in s}
    keep["system_commit_mb"] = commit_mb()
    return keep


def cmd_config(a):
    import darkred
    from tools.harness.instance import Instance
    darkred.check_claim(a.instance)   # claimed by C5_POOL_OWNER, or refused (2026-11-13)
    inst = Instance.load(a.instance)
    darkred.INST = Path(inst.root)
    darkred.CONFIG = Path(inst.user_dir) / "config.ini"
    darkred.MARKER = Path(inst.crashlogs_dir) / "viewmask.enabled"
    log("darkred config --arm %s on %s (%s, %s)" % (a.arm, a.instance, darkred.CONFIG, darkred.MARKER))
    darkred.cmd_config(argparse.Namespace(arm=a.arm, on=[], whitemask=False))
    return 0


def cmd_consumer(a):
    from tools.harness.instance import Instance
    inst = Instance.load(a.instance)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ev = open(out / "consumer.jsonl", "a", encoding="utf-8")

    def emit(kind, **kw):
        kw.update(event=kind, time=now())
        ev.write(json.dumps(kw) + "\n")
        ev.flush()
        log("%s %s" % (kind, json.dumps(kw)[:400]))

    main = tapcheck.Mapping(a.section, create=4096)
    magic = struct.unpack("<I", main.read(0, 4))[0]
    if magic == 0:
        main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
    emit("start", section=a.section, existed=magic != 0)
    seen = set()
    last_state = None
    t_end = time.time() + a.seconds
    pending_after = []   # (time, load, copy)
    try:
        while time.time() < t_end and not (out / "stop").exists():
            pid = inst_pid(inst)
            raw = tapcheck.read_block(main, tapcheck.TAP_OFFSET, tapcheck.TAP_SIZE, tries=50)
            t = tapcheck.parse_tap(raw) if raw else {}
            st = (t.get("stateName"), t.get("loadSeq"), t.get("copySeq")) if t else None
            if st != last_state and t:
                emit("state", state=t["stateName"], fail=t["failName"], load=t["loadSeq"], copy=t["copySeq"],
                     section_name=t["sectionName"], payload=t["payloadBytes"], copy_ms=round(t["copyMs"], 1),
                     fnv="%016X" % t["fnv1a64"], mem=mem(pid) if pid else None)
                last_state = st
            if t and t["stateName"] == "READY" and (t["loadSeq"], t["copySeq"]) not in seen:
                key = (t["loadSeq"], t["copySeq"])
                seen.add(key)
                before = mem(pid) if pid else None
                dst = out / ("tap_l%d_c%d.c5hf" % key)
                t0 = time.time()
                res = tapcheck.consume(a.section, wait_s=2, out=dst)
                emit("consumed", load=key[0], copy=key[1], code=res.code, msg=res.msg, ack=res.ack,
                     seconds=round(time.time() - t0, 1), out=str(dst) if res.code == 0 else None,
                     c5hf={k: v for k, v in res.c5hf.items() if k != "need"}, mem_at_ready=before)
                pending_after.append((time.time() + 3.0, key))
            for item in list(pending_after):
                if time.time() >= item[0]:
                    pending_after.remove(item)
                    raw = tapcheck.read_block(main, tapcheck.TAP_OFFSET, tapcheck.TAP_SIZE, tries=50)
                    t2 = tapcheck.parse_tap(raw) if raw else {}
                    emit("after_ack_3s", load=item[1][0], copy=item[1][1], state=t2.get("stateName"),
                         section_open=tapcheck.exists(t2.get("sectionName", "")) if t2 else None,
                         mem=mem(pid) if pid else None)
            time.sleep(0.05)
    finally:
        emit("end")
        main.close()
        ev.close()
    return 0


def cmd_rawdump(a):
    from tools.harness.instance import Instance
    import gt_reader as gt
    inst = Instance.load(a.instance)
    pid = inst_pid(inst)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rd = gt.Reader(pid)
    t0 = time.time()
    try:
        gp = rd.u32(0xD90FC4)
        grid = rd.va(0xD91270)
        if gp != grid:
            raise SystemExit("[0xD90FC4] = 0x%08X is not the GridHeightmap 0x%08X: no map" % (gp, grid))
        obj = rd.rva(0xD91270, 0xC8)
        (out / "grid.bin").write_bytes(obj)
        (out / "h2w.bin").write_bytes(rd.rva(0xD91030, 64))
        wrap = rd.u32(0xD90FBC)
        info = {"time": now(), "pid": pid, "exe_base": "0x%08X" % rd.base, "grid": "0x%08X" % grid, "wrap": wrap}
        for name, at, tb in (("16", 0x0C, 512), ("8", 0x4C, 256)):
            w, h, tx, ty, d, fh, iu, pool, psz, pcap = struct.unpack_from("<10I", obj, at)
            dirb = rd.rd(d, tx * ty * 8)
            (out / ("dir%s.bin" % name)).write_bytes(dirb)
            chunks = []
            left, addr = psz * tb, pool
            while left:
                n = min(left, 16 << 20)
                chunks.append(rd.rd(addr, n))
                addr += n
                left -= n
            (out / ("pool%s.bin" % name)).write_bytes(b"".join(chunks))
            states = [struct.unpack_from("<H", dirb, i * 8)[0] for i in range(tx * ty)]
            info["layer" + name] = {"w": w, "h": h, "tilesX": tx, "tilesY": ty, "dir": "0x%08X" % d,
                                    "freeHead": "0x%08X" % fh, "inUse": iu, "pool": "0x%08X" % pool, "poolSize": psz,
                                    "poolCapacity": pcap, "constant_tiles": states.count(1), "pooled_tiles": states.count(0)}
        info["water_z"] = struct.unpack_from("<f", obj, 4)[0]
        info["seconds"] = round(time.time() - t0, 1)
    finally:
        rd.close()
    (out / "rawdump.json").write_text(json.dumps(info, indent=1))
    print(json.dumps(info, indent=1))
    return 0


ARM = r"""
W2MT = W2MT or {}
local rec = { armed = os.clock() }
W2MT["@KEY@"] = rec
local handler
handler = function()
  if rec.ran then return end
  rec.ran = os.clock()
  pcall(Events.LocalMachineAppUpdate.Remove, handler)
  local ok, err = pcall(function()
@BODY@
  end)
  rec.ok = ok
  if not ok then rec.err = tostring(err) end
end
Events.LocalMachineAppUpdate.Add(handler)
return "armed"
"""


def cmd_reload(a):
    from tools.harness.instance import Instance
    from tools.harness import lua
    from tools.harness.frontend import lua_quote
    inst = Instance.load(a.instance)
    p = Path(inst.user_dir) / "ModdedSaves" / "single" / (a.save + ".Civ5Save")
    if not p.exists():
        raise SystemExit("no such instance save: %s" % p)
    key = "w2_%d" % int(time.time())
    body = "Events.PlayerChoseToLoadGame(%s, false)" % lua_quote(str(p))
    v = lua.values(lua.run_lua(inst, ARM.replace("@KEY@", key).replace("@BODY@", body), state="InGame", timeout=20))
    log("armed: %s (%s)" % (v, body))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sp = ap.add_subparsers(dest="cmd", required=True)
    c = sp.add_parser("config")
    c.add_argument("--instance", required=True)
    c.add_argument("--arm", choices=["d33", "stock"], required=True)
    c = sp.add_parser("consumer")
    c.add_argument("--instance", required=True)
    c.add_argument("--section", required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--seconds", type=float, default=3600)
    c = sp.add_parser("rawdump")
    c.add_argument("--instance", required=True)
    c.add_argument("--out", required=True)
    c = sp.add_parser("reload")
    c.add_argument("--instance", required=True)
    c.add_argument("--save", required=True)
    a = ap.parse_args(argv)
    return {"config": cmd_config, "consumer": cmd_consumer, "rawdump": cmd_rawdump, "reload": cmd_reload}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
