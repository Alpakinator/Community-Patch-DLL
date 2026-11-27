"""Cost of the hand-over on the stand-in (issue 3302): keyed mutex + copy (the version-3 path) against ring + fence and
ring + CPU sync, interleaved, at the game's resolution. Run it under the quiet-machine lock:

    python perf/quietlock/quietlock.py run --take --who 3300-w2 --what "3302 cost series" --minutes 25 -- \
        python shell/frame/tests/cost_series.py --build D:/c5-64bit-work/3300/w2/build --out D:/c5-64bit-work/3300/w2/cost

Per run: ring_producer.exe (x64, the server's part: clear + --sp passes of the load shader, then the hand-over) and
ring_consumer.exe (x86, the composite's part: publish F, the "EXE's" own work, wait for F up to the bound, take, draw,
release, flush with at most 2 frames in flight) on a hidden desktop for --seconds; the first --warmup frames are not
counted. Arm order rotates per round. Output: runs.jsonl (one line per run with the consumer's result line), and
summary.json / summary.md with mean, sd, median, min, max and 95 % interval per arm and the differences against v3
(Welch, with the smallest detectable difference), the free commit at start and end.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "shell" / "tests"))
sys.path.insert(0, str(REPO / "tools" / "harness" / "tests"))
sys.path.insert(0, str(REPO / "perf" / "quietlock"))
import desktops  # noqa: E402
import shellrun  # noqa: E402

try:
    import gate  # noqa: E402  (free commit)
except Exception:  # noqa: BLE001
    gate = None

ARMS = {
    "v3": ["--mode", "v3"],
    "ring_fence": ["--mode", "ring", "--sync", "fence"],
    "ring_cpu": ["--mode", "ring", "--sync", "cpu"],
    "ring_fence4": ["--mode", "ring", "--sync", "fence", "--slots", "4"],
    "ring_fence_noretry": ["--mode", "ring", "--sync", "fence"],
}
# producer-only arguments per arm (the host's behaviour, not the hand-over's)
PARMS = {"ring_fence_noretry": ["--busy-retry", "0"]}
METRICS = ["mean_frame_us", "mean_handover_cpu_us", "mean_gpu_us", "mean_wait_us", "exact_share", "max_frame_us", "long_frames"]

# two-sided 97.5 % t quantiles by degrees of freedom
T975 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201,
        12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131, 20: 2.086, 30: 2.042}


def t975(df: float) -> float:
    d = max(1, int(math.floor(df)))
    return T975[max(k for k in T975 if k <= d)]   # conservative: the next lower tabulated df


def free_commit_gb():
    try:
        return round(gate.free_commit_bytes() / 2**30, 1) if gate else None
    except Exception:  # noqa: BLE001
        return None


def one_run(desk, b: Path, out: Path, arm: str, rnd: int, a) -> dict:
    tag = "r%d_%s" % (rnd, arm)
    base = "Local\\VPFrameCost_%d_%s" % (os.getpid(), tag)
    plog, clog, csv = out / (tag + "_producer.log"), out / (tag + "_consumer.log"), out / (tag + ".csv")
    common = ["--base", base]
    size = ["--width", str(a.width), "--height", str(a.height)]
    pr = shellrun.launch([str(b / "ring_producer.exe")] + common + size +
                         ["--passes", str(a.sp), "--iters", str(a.siters), "--slots", str(a.slots), "--idle-exit", "3",
                          "--seconds", str(a.seconds + 30),
                          "--log", str(plog)] + ARMS[arm] + PARMS.get(arm, []), desk, console=True, out_path=out / (tag + "_producer.out"))
    time.sleep(0.5)
    co = shellrun.launch([str(b / "ring_consumer.exe")] + common + size +
                         ["--seconds", str(a.seconds), "--warmup", str(a.warmup), "--bound-us", str(a.bound_us),
                          "--passes", str(a.cp), "--iters", str(a.citers), "--cpu-us", str(a.cpu_us), "--slots", str(a.slots),
                          "--csv", str(csv), "--log", str(clog)] + ARMS[arm],
                         desk, console=True, out_path=out / (tag + "_consumer.out"))
    try:
        cc = co.wait(a.seconds + 60)
        pc = pr.wait(30)
    finally:
        co.kill(); pr.kill(); co.close(); pr.close()
    txt = clog.read_text(errors="replace") if clog.exists() else ""
    m = re.search(r"result: (.*)$", txt, re.M)
    rec = {"arm": arm, "round": rnd, "consumer_exit": cc, "producer_exit": pc}
    if m:
        toks = m.group(1).split()
        r = {toks[i]: float(toks[i + 1]) for i in range(0, len(toks) - 1, 2)}
        r["exact_share"] = r["exact"] / max(1.0, r["frames"])
        rec.update(r)
    if csv.exists():  # frames over 20 ms after the warm-up (stalls, research 4.6), and where the time went
        longs = []
        for ln in csv.read_text().splitlines()[1:]:
            f = ln.split(",")
            if ln.startswith("#") or len(f) < 9:
                continue
            if int(f[0]) > a.warmup and (float(f[7]) > 20000 or float(f[8]) > 20000 or int(f[6]) > 20000):
                longs.append({"frame": int(f[0]), "frame_us_prev": int(f[6]), "throttle_us": float(f[7]), "exe_us": float(f[8]),
                              "wait_us": float(f[4])})
        rec["long_frames"] = len(longs)
        rec["long_detail"] = longs[:10]
    ptxt = plog.read_text(errors="replace") if plog.exists() else ""
    pm = re.search(r"done: busy_retried (\d+) draws (\d+) busy (\d+).*cpu_ms_mean ([\d.]+) publish_ms_mean ([\d.]+)", ptxt)
    if pm:
        rec["server_busy_retried"] = int(pm.group(1)); rec["server_draws"] = int(pm.group(2)); rec["server_busy"] = int(pm.group(3))
        rec["server_cpu_ms"] = float(pm.group(4)); rec["server_publish_ms"] = float(pm.group(5))
    return rec


def describe(xs):
    n = len(xs)
    m = statistics.fmean(xs)
    sd = statistics.stdev(xs) if n > 1 else 0.0
    h = t975(n - 1) * sd / math.sqrt(n) if n > 1 else float("nan")
    return {"n": n, "mean": m, "sd": sd, "median": statistics.median(xs), "min": min(xs), "max": max(xs), "ci95": [m - h, m + h]}


def welch(a, b):
    na, nb = len(a), len(b)
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    va, vb = statistics.variance(a) / na, statistics.variance(b) / nb
    se = math.sqrt(va + vb)
    df = (va + vb) ** 2 / ((va ** 2) / (na - 1) + (vb ** 2) / (nb - 1)) if se > 0 else na + nb - 2
    t = t975(df)
    # smallest difference detectable at alpha 0.05 with power 0.8: (t_0.975 + 0.84) * se
    return {"diff": mb - ma, "ci95": [mb - ma - t * se, mb - ma + t * se], "df": df, "detectable": (t + 0.84) * se}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="output folder of shell/frame/build.cmd")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--seconds", type=float, default=12)
    ap.add_argument("--warmup", type=int, default=300)
    ap.add_argument("--width", type=int, default=2554)
    ap.add_argument("--height", type=int, default=1411)
    ap.add_argument("--bound-us", type=int, default=3400)
    ap.add_argument("--sp", type=int, default=1, help="server: load passes per frame")
    ap.add_argument("--siters", type=int, default=0, help="server: loop steps of the load shader")
    ap.add_argument("--cp", type=int, default=1, help="consumer (the EXE's work): load passes per frame")
    ap.add_argument("--citers", type=int, default=0)
    ap.add_argument("--cpu-us", type=int, default=0, help="consumer: CPU work between publish and insertion")
    ap.add_argument("--slots", type=int, default=3)
    ap.add_argument("--arms", default="v3,ring_fence,ring_cpu")
    a = ap.parse_args()
    b = Path(a.build) / "bin"
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    arms = a.arms.split(",")
    meta = {"start": time.strftime("%Y-%m-%d %H:%M:%S"), "free_commit_gb_start": free_commit_gb(), "args": vars(a)}
    print(json.dumps(meta), flush=True)
    d = desktops.HiddenDesktop("C5C")
    runs = []
    try:
        with open(out / "runs.jsonl", "a") as f:
            for rnd in range(a.rounds):
                order = arms[rnd % len(arms):] + arms[:rnd % len(arms)]
                for arm in order:
                    rec = one_run(d.name, b, out, arm, rnd, a)
                    rec["time"] = time.strftime("%H:%M:%S")
                    f.write(json.dumps(rec) + "\n"); f.flush()
                    runs.append(rec)
                    print("%s r%d %-10s frame %.1f us ho_cpu %.2f us gpu %.1f us wait %.1f us exact %.4f long %s exits %s/%s" % (
                        rec["time"], rnd, arm, rec.get("mean_frame_us", -1), rec.get("mean_handover_cpu_us", -1),
                        rec.get("mean_gpu_us", -1), rec.get("mean_wait_us", -1), rec.get("exact_share", -1), rec.get("long_frames"),
                        rec["consumer_exit"], rec["producer_exit"]), flush=True)
    finally:
        d.close()
    meta["end"] = time.strftime("%Y-%m-%d %H:%M:%S")
    meta["free_commit_gb_end"] = free_commit_gb()
    summ = {"meta": meta, "arms": {}, "vs_v3": {}}
    for arm in arms:
        rs = [r for r in runs if r["arm"] == arm and "frames" in r]
        summ["arms"][arm] = {m: describe([r[m] for r in rs]) for m in METRICS + ["server_cpu_ms", "server_publish_ms"]
                             if rs and all(m in r for r in rs)}
    if "v3" in arms:
        base = [r for r in runs if r["arm"] == "v3" and "frames" in r]
        for arm in arms:
            if arm == "v3":
                continue
            rs = [r for r in runs if r["arm"] == arm and "frames" in r]
            if len(rs) > 1 and len(base) > 1:
                summ["vs_v3"][arm] = {m: welch([r[m] for r in base], [r[m] for r in rs]) for m in METRICS}
    (out / "summary.json").write_text(json.dumps(summ, indent=1))
    print(json.dumps(summ, indent=1))
    bad = [r for r in runs if r.get("consumer_exit") != 0 or r.get("producer_exit") != 0 or "frames" not in r]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
