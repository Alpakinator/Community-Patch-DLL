"""Ticket 4801 (epic 0504 P1): the grade A testbed job, release against release + profile, H 300-305 then M 150-160,
interleaved, AI logging off, each through tbseries.py (perf/tbjobs/series.py). Python 3.8+ (%C5H_PYTHON%).

    %C5H_PYTHON% tools/x64-dll/pgo/tbjob.py [--h-runs 6] [--m-runs 12] [--label "4801 ..."]

Expects in %TESTBED_BIN%: rel/bin and pgo/bin (the pin layout's bin with each arm's DLL), saves/TS0438-H-T300.Civ5Save,
saves/TS0438-M-T150.Civ5Save, db/Civ5DebugDatabase.db (0438's statistics-on database). Writes %TESTBED_OUT%/H and
%TESTBED_OUT%/M (series.json, turnbench.json, cmp-rel-pgo/, ledger/). Exit 0 only when both series exit 0.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

GAMES = (("H", "TS0438-H-T300.Civ5Save", 300, 305), ("M", "TS0438-M-T150.Civ5Save", 150, 160))


def series_cmd(name, save, start, to, runs, label, b, out):
    return [sys.executable, os.path.join(HERE, "tbseries.py"), "--start-turn", str(start), "--",
            "--game", "huge", "--save", os.path.join(b, "saves", save), "--db", os.path.join(b, "db",
                                                                                           "Civ5DebugDatabase.db"),
            "--to-turn", str(to), "--runs", str(runs),
            "--arm", "rel=x64:%s:off" % os.path.join(b, "rel", "bin"),
            "--arm", "pgo=x64:%s:off" % os.path.join(b, "pgo", "bin"),
            "--compare", "rel,pgo", "--out-sub", name, "--label", "%s %s %d-%d" % (label, name, start, to),
            "--ledger-root", os.path.join(out, name, "ledger"), "--ledger-issue", "4801"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--h-runs", type=int, default=6)
    ap.add_argument("--m-runs", type=int, default=12)
    ap.add_argument("--label", default="4801 PGO rel vs pgo")
    ap.add_argument("--games", default="H,M")
    a = ap.parse_args()
    b, out = os.environ["TESTBED_BIN"], os.environ.get("TESTBED_OUT", ".")
    # the GPU and its driver (D175: every series names it); a read-only query
    gpu = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate | "
                          "Format-List"], capture_output=True, text=True, errors="replace")
    with open(os.path.join(out, "gpu.txt"), "w", encoding="utf-8") as f:
        f.write(gpu.stdout + gpu.stderr)
    print("GPU: " + " ".join(gpu.stdout.split()), flush=True)
    rc_all = 0
    for name, save, start, to in GAMES:
        if name not in a.games.split(","):
            continue
        runs = a.h_runs if name == "H" else a.m_runs
        cmd = series_cmd(name, save, start, to, runs, a.label, b, out)
        print("%s %s: %s" % (time.strftime("%H:%M:%S"), name, " ".join(cmd)), flush=True)
        rc = subprocess.run(cmd).returncode
        print("%s %s: exit %d" % (time.strftime("%H:%M:%S"), name, rc), flush=True)
        rc_all |= rc
    return 1 if rc_all else 0


if __name__ == "__main__":
    sys.exit(main())
