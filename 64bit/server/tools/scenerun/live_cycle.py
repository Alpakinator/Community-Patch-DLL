"""The live cycle timed beside scenerun (task 3906, epic 0301): start the client, load the Tiny save, take a screenshot,
on a workstation pool instance, N times. Written 2026-11-15 for this repository (GPL-3.0-or-later). The numbers are
workflow indications (PERFORMANCE.md), set beside scenerun's time per shot; never a product figure.

    python -I server/tools/scenerun/live_cycle.py prep    --instance c5h-a --work DIR [--save-src C5H-0167-TINY_0008]
    python -I server/tools/scenerun/live_cycle.py cycle   --instance c5h-a --work DIR [--runs 3]
    python -I server/tools/scenerun/live_cycle.py restore --instance c5h-a --work DIR

prep (the instance claimed by $C5_POOL_OWNER, not running): every file of the harness's standard backup set backed up
by SHA-256 into WORK/backup; the save copy C5H-0301-RUN_<turn> of the instance's Tiny save (SHA-256 recorded); the
front end set to load it (tools/harness/frontend.prepare: the instance's own MainMenu/FrontEnd Lua backed up by hash).
cycle, per run: `quietlock.py check --quiet` (noted); t0; `cli.py launch` (the no-focus launcher, gated; the guard log
and the start-up watch read from the harness state); `frontend.ensure_in_game` (the load); the popups closed through
the game's own popup queue (tools/scenario/lua/popups.lua, D114); `shot.shot` recorded as DIAGNOSTIC; t_shot; then
`cli.py stop --human-turn-hook tools.harness.turns:stop_readiness` (waits until the newest save has stopped changing;
never a kill on a timer). The cycle time is t_shot - t0; the stop time is reported apart.
restore: the front end restored by hash, the save copy removed, the backup restored by hash (created files removed),
`provision.py verify --quick`. A STOP file in WORK ends the cycles before the next launch.
Exit 0 ok; 2 refused (claim, running, save); 3 the game did not come up; 4 the stop failed (the game is left running);
5 a step failed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "visualcheck"))
PY = sys.executable


def log(msg):
    print("live_cycle %s: %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def need_claim(name):
    owner = os.environ.get("C5_POOL_OWNER", "")
    out = subprocess.run([PY, "-I", str(ROOT / "tools/harness/pool.py"), "list"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.split() and line.split()[0] == name:
            if owner and ("claimed by %s " % owner) in line + " ":
                return
            raise SystemExit("refused: %s is not claimed by C5_POOL_OWNER=%r: %s" % (name, owner, line.strip()))
    raise SystemExit("refused: %s not in the pool" % name)


def cmd_prep(a, inst):
    from tools.harness import backup, frontend
    need_claim(inst.name)
    if inst.refresh():
        log("refused: %s is running" % inst.name)
        return 2
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    bdir = work / "backup"
    if not (bdir / "manifest.json").exists():
        m = backup.backup([str(p) for p in backup.standard_files(inst)], bdir, "3906 live cycle")
        log("backup: %d files by SHA-256 -> %s" % (len(m.entries), bdir))
    saves = Path(inst.user_dir) / "ModdedSaves" / "single"
    src = saves / (a.save_src + ".Civ5Save")
    if not src.is_file():
        log("refused: no save %s" % src)
        return 2
    dst = saves / ("C5H-0301-RUN_%04d.Civ5Save" % a.turn)
    if not dst.exists():
        shutil.copy2(str(src), str(dst))
    rec = {"source": str(src), "source_sha256": sha256(src), "copy": str(dst), "copy_sha256": sha256(dst)}
    (work / "save.json").write_text(json.dumps(rec, indent=1), encoding="utf-8")
    log("save copy %s (%s)" % (dst.name, rec["copy_sha256"][:16]))
    st = frontend.prepare(inst, ("cp", "vp", "eui"), load=dst.stem)
    log("front end prepared to load %s: ok %s" % (dst.stem, st.get("ok", True)))
    return 0


def quiet():
    cp = subprocess.run([PY, "-I", str(ROOT / "perf/quietlock/quietlock.py"), "check", "--quiet"], capture_output=True,
                        text=True)
    return {"exit": cp.returncode, "lines": [x for x in cp.stdout.splitlines() if x.strip()][:10]}


def cmd_cycle(a, inst):
    from tools.harness import frontend, lua, shot
    from tools.harness.instance import Instance
    need_claim(inst.name)
    work = Path(a.work)
    res_path = work / "cycles.json"
    res = json.loads(res_path.read_text(encoding="utf-8")) if res_path.exists() else {"runs": []}
    popups_lua = (ROOT / "tools" / "scenario" / "lua" / "popups.lua").read_text(encoding="utf-8")
    code = 0
    for k in range(a.runs):
        if (work / "STOP").exists():
            log("STOP file: no further launch")
            break
        # the front end's load token is good for one launch: prepare it again before each (not timed)
        save_stem = Path(json.loads((work / "save.json").read_text(encoding="utf-8"))["copy"]).stem
        frontend.prepare(Instance.load(inst.name), ("cp", "vp", "eui"), load=save_stem)
        q = quiet()
        r = {"run": len(res["runs"]) + 1, "quiet_exit": q["exit"], "quiet": q["lines"], "start": time.strftime("%Y-%m-%dT%H:%M:%S")}
        t0 = time.perf_counter()
        cp = subprocess.run([PY, "-I", str(ROOT / "tools/harness/cli.py"), "-i", inst.name, "launch"], capture_output=True,
                            text=True)
        r["launch_exit"] = cp.returncode
        r["t_launch_s"] = round(time.perf_counter() - t0, 2)
        (work / ("launch_%d.log" % r["run"])).write_text(cp.stdout + cp.stderr, encoding="utf-8")
        if cp.returncode:
            log("launch exit %d: %s" % (cp.returncode, (cp.stdout + cp.stderr)[-400:]))
            r["error"] = "launch"
            res["runs"].append(r)
            code = 3
            break
        inst = Instance.load(inst.name)
        inst.refresh()
        st = inst.load_state()
        r["guard_log"] = st.get("guard_log")
        r["startup_watch"] = st.get("startup_watch")
        e = frontend.ensure_in_game(inst, timeout=900)
        r["t_in_game_s"] = round(time.perf_counter() - t0, 2)
        r["in_game"] = [e.ok, e.reason]
        if not e.ok:
            log("not in game: %s" % e.reason)
            r["error"] = "not in game"
            res["runs"].append(r)
            code = 3
            break
        pr = lua.run_lua(inst, popups_lua, state="InGame", timeout=60)
        r["popups"] = lua.values(pr) if pr.get("ok") else pr.get("error")
        time.sleep(a.settle)
        s = shot.shot(inst, work / ("live_%d.png" % r["run"]), visual={
            "what": ("DIAGNOSTIC: 3906 live cycle %d: the stock game (instance %s, its own DLL) started through the no-focus "
                     "launcher, the Tiny save copy C5H-0301-RUN_%04d loaded, popups closed (D114: %s), one screenshot; "
                     "taken to time the live cycle against scenerun's offline shot, not a comparison") % (
                r["run"], inst.name, a.turn, r["popups"]),
            "result": "info", "issue": "3906"})
        r["t_shot_s"] = round(time.perf_counter() - t0, 2)
        r["shot"] = {"result": s.get("result"), "path": s.get("path"), "visual": s.get("visual")}
        log("run %d: launch %.1f s, in game %.1f s, shot %.1f s" % (r["run"], r["t_launch_s"], r["t_in_game_s"], r["t_shot_s"]))
        t1 = time.perf_counter()
        cp = subprocess.run([PY, "-I", str(ROOT / "tools/harness/cli.py"), "-i", inst.name, "stop", "--human-turn-hook",
                             "tools.harness.turns:stop_readiness"], capture_output=True, text=True)
        r["stop_exit"] = cp.returncode
        r["t_stop_s"] = round(time.perf_counter() - t1, 2)
        (work / ("stop_%d.log" % r["run"])).write_text(cp.stdout + cp.stderr, encoding="utf-8")
        res["runs"].append(r)
        res_path.write_text(json.dumps(res, indent=1, default=str), encoding="utf-8")
        if cp.returncode:
            log("stop exit %d: %s (the game is left running)" % (cp.returncode, (cp.stdout + cp.stderr)[-400:]))
            code = 4
            break
        log("run %d stopped in %.1f s" % (r["run"], r["t_stop_s"]))
    ok = [r["t_shot_s"] for r in res["runs"] if "t_shot_s" in r and not r.get("error")]
    if ok:
        res["summary"] = {"n": len(ok), "median_s": round(statistics.median(ok), 2), "min_s": min(ok), "max_s": max(ok),
                          "in_game_median_s": round(statistics.median([r["t_in_game_s"] for r in res["runs"] if "t_shot_s" in r]), 2),
                          "stop_median_s": round(statistics.median([r["t_stop_s"] for r in res["runs"] if "t_stop_s" in r]), 2)}
        log("summary %s" % json.dumps(res["summary"]))
    res_path.write_text(json.dumps(res, indent=1, default=str), encoding="utf-8")
    return code


def cmd_restore(a, inst):
    from tools.harness import backup, frontend
    need_claim(inst.name)
    if inst.refresh():
        log("refused: %s is running" % inst.name)
        return 2
    work = Path(a.work)
    r = frontend.restore(inst)
    log("front end restore: %s" % json.dumps(r, default=str)[:300])
    sj = work / "save.json"
    if sj.exists():
        dst = Path(json.loads(sj.read_text(encoding="utf-8"))["copy"])
        if dst.exists():
            dst.unlink()
        log("save copy %s removed" % dst.name)
    if (work / "backup" / "manifest.json").exists():
        rep = backup.restore(work / "backup", remove_created=True)
        log("backup restore ok %s: %s" % (rep.ok, json.dumps(getattr(rep, "__dict__", {}), default=str)[:400]))
        if not rep.ok:
            return 5
    cp = subprocess.run([PY, "-I", str(ROOT / "tools/instances/provision.py"), "verify", inst.name, "--quick"],
                        capture_output=True, text=True)
    log("provision verify --quick exit %d: %s" % (cp.returncode, (cp.stdout + cp.stderr).strip()[-300:]))
    return 0 if cp.returncode == 0 and r.get("ok", True) else 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("prep", "cycle", "restore"))
    ap.add_argument("--instance", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--save-src", default="C5H-0167-TINY_0008")
    ap.add_argument("--turn", type=int, default=8)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--settle", type=float, default=2.0)
    a = ap.parse_args()
    for k in ("C5_POOL_OWNER", "C5_JOB_OWNER", "C5_QUIETLOCK_WHO"):
        if not os.environ.get(k):
            log("refused: set %s (COMMON 16)" % k)
            return 2
    from tools.harness.instance import Instance
    inst = Instance.load(a.instance)
    return {"prep": cmd_prep, "cycle": cmd_cycle, "restore": cmd_restore}[a.cmd](a, inst)


if __name__ == "__main__":
    sys.exit(main())
