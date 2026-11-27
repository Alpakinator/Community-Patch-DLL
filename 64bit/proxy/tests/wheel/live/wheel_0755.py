"""Ticket 0755 ([Extras] FixSmoothWheel): the product path on a test machine (the laptop) with the key on or off, and
5042's wheel driver (perf/evidence/5042/wheel_5042.py `run`) by real input and by posted messages. Own code, 2026-11-25,
GPL-3.0-or-later; it imports tools/capture/capture_job.py (its launch, the owner's [Renderer] set) and
tools/testbed/jobs/product_path.py (prep, stop, restore by hash) unchanged.

A headed testbed job (a game-instance@laptop place, --headed), one key value per job:

    python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --name "0755 wheel key=1" --timeout 3600 \
        --file <server_build out>/bin=pp/bin --file <proxy CvGameCore_Expansion2.dll>=pp/proxy/CvGameCore_Expansion2.dll \
        --file <shell_composite.dll>=pp/composite/shell_composite.dll --file <purity.tsv for that x64 DLL>=w0755/purity.tsv \
        --file <wheel_probe.dll>=w5042/wheel_probe.dll --file <W5042-TINY_0008.Civ5Save>=saves/W5042-TINY_0008.Civ5Save \
        --file tools/harness/native/bin=native --file <vp-dll-dev skill>/assets=frontend-assets -- \
        "%TESTBED_PYTHON%" proxy/tests/wheel/live/wheel_0755.py --key 1

Steps: product_path.prep (the D28 layout, the composite, D33 with Overlay, vpproxy.ini mode=on, every changed file
backed up by SHA-256), VP64Options.ini = the owner's [Renderer] set (capture_job's OWNER_OPTIONS) + [Extras]
FixSmoothWheel = --key, the shipped purity.tsv beside the x64 DLL (the pack's place); capture_job.launch (frontend
prepare --load, cli.py launch --mode offscreen --park 0,0, in game); then for each --lanes lane `wheel_5042.py run
--sum-repeats 0` (the 5042 probe as a pass-through instrument above the proxy's thunk: it logs what the thunk let
through and reads the zoom steps from the EXE's camera); the safe stop; the run's logs copied to out/; the purity file
removed; product_path.restore (by hash) and provision.py verify.

Results: %TESTBED_OUT%/w0755.json (the vpproxy.log `purity:` and `wheel:` lines, each lane's rc and summary), out/w0755/<lane>/
wheel_5042.json and probe/wheel_probe.tsv, out/vpproxy.log, server.log. 5042's own "fails" lines (messages taken
!= sent) are EXPECTED with the key on: the probe sees the thunk's output. The verdict is the zoom steps per arm.
Exit: 0 every lane ran (its rc 0 or 1), the logs show purity loaded and the wheel line, stopped and restored; 2 prep
failed (restored); 3 the game did not reach the map; 4 a lane failed to run (rc 2 or more) or a log line is missing;
6 the stop was refused (left running, nothing restored); 7 the restore failed; 8 an application dialog is open.
"""
import argparse
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("TESTBED_SRC") or os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
for _p in (os.path.join(SRC, "tools", "capture"), os.path.join(SRC, "tools", "testbed", "jobs"),
           os.path.join(SRC, "tools", "hi")):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import capture_job as cj  # noqa: E402  (launch, options_text, parse_options, OWNER_OPTIONS; it imports product_path)
import realinput as RI  # noqa: E402  (the fence: refused on the workstation)

pp = cj.pp
hp = pp.hp
OUT, WORK, BIN, HARNESS = pp.OUT, pp.WORK, pp.BIN, pp.HARNESS
SAVE_PREFIX = "TBW0755-"
DRIVER = os.path.join(SRC, "perf", "evidence", "5042", "wheel_5042.py")


def options_text(key):
    return cj.options_text(cj.parse_options(cj.OWNER_OPTIONS)) + "\n[Extras]\nFixSmoothWheel = %d\n" % key


def log_lines(path, *needles):
    out = []
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for ln in f:
                if any(n in ln for n in needles):
                    out.append(ln.rstrip()[:600])
    except OSError:
        pass
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--key", type=int, choices=[0, 1], required=True, help="[Extras] FixSmoothWheel")
    ap.add_argument("--instance", default=None, help="default: the job's own instance (TESTBED_INSTANCES)")
    ap.add_argument("--save", default="W5042-TINY_0008", help="shipped as saves/NAME.Civ5Save, else %%TESTBED_SAVES%%")
    ap.add_argument("--lanes", default="real,posted")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--ingame-timeout", type=float, default=900)
    ap.add_argument("--settle-load", type=float, default=20)
    a = ap.parse_args(argv)
    RI.fence()
    a.instance = a.instance or next((x for x in os.environ.get("TESTBED_INSTANCES", "").split(",") if x in ("tb1", "tb2")),
                                    "tb1")
    a.save_copy = SAVE_PREFIX + a.save
    a.state = os.path.join(WORK, "w0755")
    a.keep_on, a.x64_name, a.uncapped = [], None, False
    os.makedirs(a.state, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    shipped_save = os.path.join(BIN, "saves", a.save + ".Civ5Save")
    if os.path.isfile(shipped_save):
        os.environ["TESTBED_SAVES"] = os.path.dirname(shipped_save)   # product_path.prep reads the save from there
    res = {"ticket": "0755", "started": pp.now(), "args": vars(a), "session": hp.session_info(),
           "host": os.environ.get("TESTBED_HOST"), "job": os.environ.get("TESTBED_JOB")}
    env = pp.setup_env()
    for q in (SRC, HARNESS):
        if q not in sys.path:
            sys.path.insert(0, q)
    s = hp.Steps(env)
    p = pp.paths(a.instance)
    inst_dir = str(p["inst"].root)
    res["shipped"] = {}
    for rel in ("bin/server.exe", "bin/CvGameCore_Expansion2.dll", "proxy/CvGameCore_Expansion2.dll",
                "composite/shell_composite.dll"):
        f = os.path.join(pp.PP, *rel.split("/"))
        res["shipped"][rel] = pp.sha256(f) if os.path.isfile(f) else None
    purity = os.path.join(BIN, "w0755", "purity.tsv")
    probe = os.path.join(BIN, "w5042", "wheel_probe.dll")
    res["shipped"]["purity.tsv"] = pp.sha256(purity) if os.path.isfile(purity) else None
    res["shipped"]["wheel_probe.dll"] = pp.sha256(probe) if os.path.isfile(probe) else None
    if not all(res["shipped"].values()):
        res["error"] = "a shipped file is missing (see shipped)"
        return finish(res, inst_dir, 2)
    res["before"] = pp.files_info(p)
    prep_rec = {"start": pp.now()}
    code, running = 0, False
    laid_purity = None
    try:
        try:
            pp.prep(s, p, a, prep_rec)
            with open(p["options"], "w", encoding="utf-8") as f:
                f.write(options_text(a.key))
            prep_rec["options_written"] = options_text(a.key)
            laid_purity = os.path.join(p["vp64"], "purity.tsv")
            if os.path.exists(laid_purity):
                raise RuntimeError("%s exists already" % laid_purity)
            shutil.copy2(purity, laid_purity)
            prep_rec["purity_laid"] = {"file": laid_purity, "sha256": pp.sha256(laid_purity)}
        finally:
            prep_rec["end"] = pp.now()
            with open(os.path.join(a.state, "prep.json"), "w", encoding="utf-8") as f:
                json.dump(prep_rec, f, indent=1, default=str)
            res["prep"] = prep_rec
        res["files_after_prep"] = pp.files_info(p)
        code, running = cj.launch(s, a, a.instance, res)
        if code == 0:
            res["lanes"] = {}
            for lane in [x.strip() for x in a.lanes.split(",") if x.strip()]:
                pp.check_cancel()
                d = os.path.join(OUT, "w0755", lane)
                t0 = time.time()
                rc, out = s.run("w5042-" + lane, [DRIVER, "-i", a.instance, "run", "--dll", probe, "--out", d,
                                                  "--repeats", str(a.repeats), "--sum-repeats", "0", "--lane", lane],
                                timeout=2400)
                lr = {"rc": rc, "s": round(time.time() - t0, 1), "tail": out[-3000:]}
                try:
                    with open(os.path.join(d, "wheel_5042.json"), encoding="utf-8") as f:
                        w = json.load(f)
                    lr["summary"] = [{k: x[k] for k in ("label", "sent", "delta_sum", "steps", "taken", "saturated")}
                                     for x in w.get("summary", [])]
                    lr["table"] = w.get("table")
                    lr["fails"] = w.get("fails")
                except (OSError, ValueError) as e:
                    lr["summary_error"] = "%s: %s" % (type(e).__name__, e)
                res["lanes"][lane] = lr
                if rc not in (0, 1):
                    code = 4
    except Exception as e:  # noqa: BLE001  (Cancelled included)
        res["error"] = "%s: %s" % (type(e).__name__, e)
        code = code or 2
        running = pp._running(s, a)
    if running:
        if code == 8:
            res["stop"] = "left running: an application dialog is open (D29)"
        elif not pp.stop_game(s, a, res):
            res["stop"] = "refused: left running; nothing restored"
            code = 6
    srv = pp.newest_run(p)
    if srv and os.path.isdir(srv):
        for fn in pp.LOG_FILES:
            f = os.path.join(srv, fn)
            if os.path.isfile(f):
                shutil.copy2(f, os.path.join(OUT, fn))
    vplog = os.path.join(OUT, "vpproxy.log")
    res["vpproxy_purity"] = log_lines(vplog, "purity:")
    res["vpproxy_wheel"] = log_lines(vplog, "wheel:")
    if code == 0 and (not any("purity: " in x and "method keys" in x for x in res["vpproxy_purity"])
                      or not any(("[Extras] FixSmoothWheel" in x) if a.key else ("wheel: off" in x)
                                 for x in res["vpproxy_wheel"])):
        res["log_check"] = "missing: the purity load or the wheel install line"
        code = 4
    if code not in (6, 8) and "backup" in prep_rec:
        res["restore"] = {}
        if laid_purity and os.path.isfile(laid_purity):
            os.unlink(laid_purity)
            res["restore"]["purity_removed"] = laid_purity
        ok = pp.restore(s, p, a, res["restore"])
        if not ok:
            code = 7
        res["after_restore"] = pp.files_info(p)
    return finish(res, inst_dir, code)


def finish(res, inst_dir, code):
    res["collected"] = hp.collect(inst_dir) if inst_dir else []
    res["ended"] = pp.now()
    res["exit"] = code
    with open(os.path.join(OUT, "w0755.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1, default=str)
    print(json.dumps({k: v for k, v in res.items() if k not in ("collected", "session")}, indent=1, default=str)[:8000])
    return code


if __name__ == "__main__":
    sys.exit(main())
