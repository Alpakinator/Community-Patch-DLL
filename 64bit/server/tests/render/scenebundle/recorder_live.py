"""Epic 0301, task 3904: a scene bundle from the joined arm on the product path (server.exe, proxy and composite of this
branch, the x64 pin's DLL; ServerRenderer = 1, render_handover=ring), the recorder's keys appended to the server.cfg the
proxy writes through vpproxy.ini server_cfg_extra= (TEST ONLY, issue 0307). Written 2026-11-15 (GPL-3.0-or-later).

The driver is shell/evidence/3308/run_3308.py's prep / run / restore (imported, as shell/evidence/0307/run_0307.py does):
the backup of every file it changes by SHA-256, set-proxy, the composite with its manifest, D33 with Overlay on, the save
copy, the launch through the harness (no-focus launcher; guard log and start-up watch read), the harness's safe stop,
the restore by hash. This wrapper sets its owner, the save copy, the extra server.cfg lines, no AI turn and no reload,
and leaves out the driver's end-of-run screenshot (no picture is taken here, D114). After `run` it checks the bundle
(3902's checker through recorder_test.exe --check) and stamps what the server cannot know (recorder_test.exe --stamp:
the save's SHA-256 and turn, the COMMON 18 pins, the repository commit, the build configuration).

    python -I server/tests/render/scenebundle/recorder_live.py prep    --save tiny|huge --instance c5h-a --work DIR
           --bin LAYOUT/bin --proxy B/proxy/bin/CvGameCore_Expansion2.dll --composite SHELLBUILD --bundle F:/.../bundles/<id>
           [--after N] [--frames N] [--dump all]
    python -I server/tests/render/scenebundle/recorder_live.py run     --save tiny|huge --instance c5h-a --work DIR
           --checker DIR/recorder_test.exe [--settle-load S]
    python -I server/tests/render/scenebundle/recorder_live.py restore --save tiny|huge --instance c5h-a --work DIR

The extra file is written into WORK (prep); nothing of it goes into the instance. The save copy (C5H-0301-TINY_0009 of
the instance's C5H-0167-TINY_0008, or C5H-0301-HUGE_0350 of C5H-VP8P-HUGE_0350, SHA-256 checked) is removed by restore.
The instance is claimed by C5_POOL_OWNER = 3904 before any step. Exit codes: run_3308.py's; run also 6 when the bundle
is missing or refused (the checker's line printed).
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
_spec = importlib.util.spec_from_file_location("run_3308", REPO / "shell" / "evidence" / "3308" / "run_3308.py")
R8 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R8)

WHO = os.environ.get("C5_RECORDER_OWNER", "3904")   # issue 3911: the owner of the run (claims, jobs, gates)
SAVES = {   # name: (source save in the instance, our copy, SHA-256 prefix of the source as run_3308 compares it, turn: the
    # fallback when the run's record has no game turn)
    "tiny": ("C5H-0167-TINY_0008", "C5H-0301-TINY_0009", "B2CEB663", 8),
    "huge": ("C5H-VP8P-HUGE_0350", "C5H-0301-HUGE_0350", "C499D3C6", 350),
    # issue 3911: the Tiny save again for the first bundle of format version 2 (the fog of war on, render_fow=1 default)
    "fog": ("C5H-0167-TINY_0008", "C5H-0301-FOG_0008", "B2CEB663", 8),
}


def opt(argv, name, default=None):
    if name in argv:
        i = argv.index(name)
        v = argv[i + 1]
        del argv[i:i + 2]
        return v
    return default


def file_sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def lock(name):
    d = {}
    for ln in (REPO / "deps" / name).read_text(encoding="utf-8").splitlines():
        if "=" in ln and not ln.lstrip().startswith("#") and not ln.startswith(" "):
            k, v = ln.split("=", 1)
            d[k.strip()] = v.strip()
    return d


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    save = opt(argv, "--save", "tiny")
    if save not in SAVES:
        print("--save tiny|huge|fog", file=sys.stderr)
        return 2
    bundle = opt(argv, "--bundle")
    after, frames, dump = opt(argv, "--after", "900"), opt(argv, "--frames", "8"), opt(argv, "--dump", "all")
    checker = opt(argv, "--checker")
    work = Path(argv[argv.index("--work") + 1])
    extra = work / "extra.cfg"
    src, copy_name, sha_prefix, turn = SAVES[save]
    R8.WHO = WHO
    R8.SAVE_SRC, R8.SAVE, R8.SAVE_SHA = src, copy_name, sha_prefix
    R8.VP_KEYS = list(R8.VP_KEYS) + ["server_cfg_extra=%s" % extra]
    real_sh = R8.sh

    def sh_no_shot(cmd, log, **kw):   # the driver's end-of-run screenshot is left out (no picture here, D114)
        if "shot" in [str(c) for c in cmd]:
            R8.say("screenshot left out (3904: no picture without its popups closed and a description)")
            return 0, ""
        return real_sh(cmd, log, **kw)

    R8.sh = sh_no_shot
    os.environ.update({"C5_POOL_OWNER": WHO, "C5_JOB_OWNER": WHO, "C5_QUIETLOCK_WHO": WHO})
    cmd = argv[0] if argv else ""
    if cmd == "prep":
        R8.R6.need_claim(argv[argv.index("--instance") + 1])
        if not bundle or not Path(bundle).is_absolute():
            print("prep needs --bundle F:/c5-64bit-work/scene-library/bundles/<bundle-id> (absolute)", file=sys.stderr)
            return 2
        if Path(bundle).exists() and any(Path(bundle).iterdir()):
            print("%s is not empty" % bundle, file=sys.stderr)
            return 2
        work.mkdir(parents=True, exist_ok=True)
        lines = ["# epic 0301, task 3904: the scene recorder (TEST ONLY, issue 0307's server_cfg_extra)",
                 "render_test_record=%s" % Path(bundle).as_posix(), "render_test_record_after=%s" % after,
                 "render_test_record_frames=%s" % frames, "render_test_record_dump=%s" % dump]
        extra.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))
        (work / "bundle.json").write_text(json.dumps({"bundle": str(Path(bundle).as_posix()), "save": save}), encoding="utf-8")
    if cmd == "restore":
        R8.R6.need_claim(argv[argv.index("--instance") + 1])
        p = R8.paths(R8.argparse.Namespace(instance=argv[argv.index("--instance") + 1], work=str(work)))
        rc, out = R8.sh([R8.PY, R8.LIVE_TOOLS / "set_ini.py", p["vpini"], "--unset", "server_cfg_extra"], work / "restore.log")
        R8.say("set_ini --unset server_cfg_extra rc %d" % rc)
    if cmd == "run":
        argv += ["--max-turns", "0", "--reloads", "0"]
        if "--settle-load" not in argv:
            argv += ["--settle-load", "90"]
    if "--stop-file" not in argv:
        argv += ["--stop-file", str(work / "STOP-GAMES")]
    code = R8.main(argv)
    if cmd == "run":
        b = json.loads((work / "bundle.json").read_text(encoding="utf-8"))["bundle"]
        if not checker or not Path(checker).exists():
            print("run needs --checker <build_scenebundle_tests out>/bin/recorder_test.exe", file=sys.stderr)
            return code or 6
        if not (Path(b) / "scene.json").exists():
            R8.say("no scene.json in %s" % b)
            return code or 6
        inst = argv[argv.index("--instance") + 1]
        p = R8.paths(R8.argparse.Namespace(instance=inst, work=str(work)))
        dl, il = lock("dll.lock"), lock("i0.lock")
        layout = Path(p["cp"]) / "VP64"
        try:   # the turn as the game reported it at the load (Game.GetGameTurn through the Lua channel), else the table's
            turn = int(json.loads((work / "out" / "run.json").read_text(encoding="utf-8"))["state_load"]["turn"])
        except Exception:  # noqa: BLE001
            pass
        stamp = ["save.name=%s" % copy_name, "save.sha256=%s" % file_sha(p["save_dir"] / (copy_name + ".Civ5Save")),
                 "save.turn=%d" % turn, "build_config=release",
                 "repo_commit=%s" % subprocess.run(["git", "-C", str(REPO), "rev-parse", "HEAD"], capture_output=True,
                                                   text=True).stdout.strip(),
                 "pins.dll_lock.commit=%s" % dl.get("commit", ""), "pins.i0_lock.commit=%s" % il.get("commit", ""),
                 "pins.dll_lock.dll_sha256=%s" % file_sha(layout / "CvGameCore_Expansion2.dll"),
                 "pins.proxy_sha256=%s" % file_sha(p["proxy"]), "pins.composite_sha256=%s" % file_sha(p["dll"])]
        cp = subprocess.run([checker, "--stamp", b] + stamp, capture_output=True, text=True)
        R8.say("stamp rc %d: %s" % (cp.returncode, (cp.stdout + cp.stderr).strip()))
        ck = subprocess.run([checker, "--check", b], capture_output=True, text=True)
        line = (ck.stdout + ck.stderr).strip()
        R8.say("checker rc %d: %s" % (ck.returncode, line))
        (work / "out" / "checker.txt").write_text("stamp rc %d: %s\ncheck rc %d: %s\n" % (cp.returncode, cp.stdout.strip(),
                                                                                       ck.returncode, line), encoding="utf-8")
        if ck.returncode:
            return code or 6
    if cmd == "restore":
        sp = R8.paths(R8.argparse.Namespace(instance=argv[argv.index("--instance") + 1], work=str(work)))
        copy = sp["save_dir"] / (copy_name + ".Civ5Save")
        R8.say("save copy %s %s" % (copy, "still there" if copy.exists() else "removed"))
    return code


if __name__ == "__main__":
    sys.exit(main())
