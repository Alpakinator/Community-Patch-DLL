"""Ticket 4801 (epic 0504 P1): perf/tbjobs/series.py for a loaded save that does not start at turn 350 (0438's M and H
saves), as a testbed job. Python 3.8+ (%C5H_PYTHON% on the testbed).

    %C5H_PYTHON% tools/x64-dll/pgo/tbseries.py --start-turn 300 -- <series.py arguments with --game huge --save SAVE>

series.py's `--game huge` plays any `--save` through tools/testbed/jobs/hostruns.py, but takes its start turn as 350
(the TB-HUGE_0350 save); the start turn decides which per-turn times count and whether a run is ok. This wrapper sets
the given start turn before each run (series.one) and writes it into series.json's meta at the end. Nothing of
series.py is changed. Exit code: series.py's.
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "perf", "tbjobs"))


def split_argv(argv):
    """(start turn, the arguments for series.py). SystemExit on a missing or bad --start-turn or a missing '--'."""
    if "--" not in argv:
        raise SystemExit("usage: tbseries.py --start-turn N -- <series.py arguments>")
    i = argv.index("--")
    own, rest = argv[:i], argv[i + 1:]
    if len(own) != 2 or own[0] != "--start-turn" or not own[1].isdigit():
        raise SystemExit("tbseries.py: --start-turn N before '--'")
    if "--game" in rest and rest[rest.index("--game") + 1:rest.index("--game") + 2] != ["huge"]:
        raise SystemExit("tbseries.py: series.py's --game must be huge (a loaded save; its default)")
    if "--save" not in rest:
        raise SystemExit("tbseries.py: give series.py --save")
    return int(own[1]), rest


def main():
    start, rest = split_argv(sys.argv[1:])
    import series  # noqa: E402

    orig = series.one

    def one(a, *args, **kw):
        a.start_turn = start
        return orig(a, *args, **kw)

    series.one = one
    sys.argv = [os.path.join(REPO, "perf", "tbjobs", "series.py")] + rest
    rc = series.main()
    p = os.path.join(series.OUT, "series.json")
    if os.path.isfile(p):
        with open(p, encoding="utf-8") as f:
            d = json.load(f)
        d["meta"]["start_turn"] = start
        d["meta"]["start_turn_set_by"] = "tools/x64-dll/pgo/tbseries.py (ticket 4801)"
        with open(p, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1)
    return rc


if __name__ == "__main__":
    sys.exit(main())
