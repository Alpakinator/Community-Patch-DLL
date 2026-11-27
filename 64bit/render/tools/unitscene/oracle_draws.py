"""Issue 4355 (epic 0041, DoD I): the B and C parts of the oracle run, called by oracle.py. Own code, 2026-11-15.

B (draw inputs): 4352's catalogue checker, `c5r_units_test_draw_oracle --manifest` over the manifest that 4352's
render/evidence/0041/4352/oracle_manifest.py writes (3907's 20 unit frames and 3901's export), parsed per unit:
art row (P10), load status, per mesh vertex / index bytes, material, programs, t0 / t1 by SHA-256, header rows; per
model the figure-major id rule; per frame and pass cb1's read lanes.

C (draws per pass): per unit and pass, the module's own draws (`c5r_units_test_draw_oracle --draw`: our meshes,
textures, programs, states and environment textures; the captured palette, ids and cb1) against the captured draws
replayed by terrainreplay (models/tools/unitreplay/unit_manifest.py), both on the hardware device into a target
cleared to 0 (depth to 1). Scene and reflection: one captured replay per frame and pass with every unit draw, the unit's
pixels = the replay's id buffer on that unit's draws; mask (no depth, the id buffer stays empty): one captured replay
per unit, the pixels either picture wrote. Statistics per COMMON 25 (equal pixels, median, p95, maximum of the per-pixel
largest channel difference); per frame and pass one DIAGNOSTIC picture set (ours, captured, difference x8) recorded
in Visual Checks (issue 4355). A unit's draws come from 0327's K3 table (frame, object, draw: the scene-pass skinned
draws) and their reflection / mask counterparts (same index count and ids); material 87 draws by their cb0 position
(nearest unit within 64 world units); 3901's by elimination (the warrior's draws are 369-371).
"""
from __future__ import annotations

import csv
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from collections import defaultdict
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[3]
NONE = 0xFFFFFFFF
I_ROOT = os.path.expanduser("~").replace("\\", "/") + "/c5-instances/c5h-c"
MODS = I_ROOT + "/Documents/My Games/Sid Meier's Civilization 5/MODS"
MODS_LIST = ";".join(MODS + "/" + m for m in ("(1) Community Patch", "(2) Vox Populi",
                                                  "(3a) VP - EUI Compatibility Files"))
UNITS_3901 = [("1", "UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR", "CIVILIZATION_POLYNESIA", "ERA_ANCIENT", "50", "13"),
              ("2", "UNIT_SHOSHONE_PATHFINDER", "ART_DEF_UNIT_U_SHOSHONE_PATHFINDER", "CIVILIZATION_POLYNESIA",
               "ERA_ANCIENT", "51", "15")]
EXPORT_3901 = "F:/c5-64bit-work/0327/exports/3901"
WARRIOR_3901_SCENE = {369, 370, 371}


def short(frame_key):
    """units_land_L01 -> L01, 3901 -> 3901"""
    return frame_key.rsplit("_", 1)[-1]


def png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def run(cmd, log):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    log.write("$ %s\nexit %d\n%s%s\n" % (" ".join(str(c) for c in cmd), r.returncode, r.stdout[-2000:], r.stderr[-2000:]))
    return r


# ---------------------------------------------------------------------------------------------- B
def write_manifest(work: Path, index, db, log):
    utsv = work / "units_3901.tsv"
    utsv.write_text("".join("\t".join(u) + "\n" for u in UNITS_3901), encoding="utf-8")
    man = work / "manifest.tsv"
    r = run([sys.executable, "-I", REPO / "render/evidence/0041/4352/oracle_manifest.py", "--index", index, "--db", db,
             "--out", man, "--extra", "3901=%s|%s" % (EXPORT_3901, utsv)], log)
    if r.returncode != 0:
        raise RuntimeError("oracle_manifest.py exit %d" % r.returncode)
    return man


def read_manifest(man):
    frames, draws = {}, defaultdict(list)
    for line in open(man, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if f[0] == "frame":
            frames[f[1]] = f[2]
        elif f[0] == "draw" and len(f) >= 21:
            draws[f[1]].append({"n": int(f[2]), "pass": f[3], "ic": int(f[11]), "count": int(f[12]),
                                "ids": f[14], "cb0": f[17], "vs": f[4]})
    return frames, draws


def run_b(work: Path, man, oracle_exe, game, db, exe, log):
    rep = work / "b_report.tsv"
    r = run([oracle_exe, "--manifest", man, "--game", game, "--mods", MODS_LIST, "--db", db, "--exe", exe,
             "--out", rep], log)
    if r.returncode != 0:
        raise RuntimeError("c5r_units_test_draw_oracle --manifest exit %d" % r.returncode)
    units = {}
    cb1 = {}
    meshes = defaultdict(dict)   # (frame, object) -> member -> meshes of its model drawn in the scene pass
    summary = ""
    for line in open(rep, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if f[0] == "unit":
            units[(f[1], f[2])] = {"P10": f[6], "meshes": 0, "vb_exact": 0, "vb_ties": 0, "vb_differ": 0, "absent": 0,
                                   "ib": 0, "t0": 0, "t1": 0, "programs": 0, "hdr_ok": 0, "hdr_n": 0, "ids": [],
                                   "fail": ""}
        elif f[0] == "mesh":
            u = units.setdefault((f[1], f[2]), {"meshes": 0})
            dr = (f[10].split("/") + ["0", "0", "0"])[:3]
            if dr[2] not in ("0", ""):
                meshes[(f[1], f[2])].setdefault(f[4], set()).add((f[5], f[6], f[8]))
            u["meshes"] += 1
            vb = f[11]
            u["vb_exact"] += vb == "exact"
            u["vb_ties"] += vb.startswith("ties")
            u["vb_differ"] += vb.startswith("differ")
            u["absent"] += vb in ("absent", "-", "nodraw") or f[10] in ("0/0/0",)
            u["ib"] += f[12] == "eq"
            u["t0"] += f[13] == "eq"
            u["t1"] += f[14] in ("eq", "none")
            u["programs"] += f[15] == "eq"
            ok, n = (f[16].split("/") + ["0"])[:2]
            u["hdr_ok"] += int(ok or 0)
            u["hdr_n"] += int(n or 0)
        elif f[0] == "ids":
            units.setdefault((f[1], f[2]), {}).setdefault("ids", []).append("%s: %s" % (f[3], f[4]))
        elif f[0] == "unitfail":
            units.setdefault((f[1], f[2]), {})["fail"] = f[4]
        elif f[0] == "cb1":
            cb1[(f[1], f[2])] = f[3]
        elif f[0] == "summary":
            summary = f[1]
    return units, cb1, summary, {k: {m: len(v) for m, v in d.items()} for k, d in meshes.items()}


def b_text(u):
    if not u:
        return "no B row"
    if u.get("fail"):
        return "not loaded: " + u["fail"][:160]
    s = ("P10 %s; meshes %d: vb exact %d, ties %d, differ %d; ib eq %d; t0 eq %d; t1 eq %d; programs eq %d; "
         "header rows %d/%d" % (u.get("P10", "?"), u["meshes"], u["vb_exact"], u["vb_ties"], u["vb_differ"], u["ib"],
                                u["t0"], u["t1"], u["programs"], u["hdr_ok"], u["hdr_n"]))
    if u.get("ids"):
        s += "; ids: " + " | ".join(u["ids"])[:200]
    return s


# ---------------------------------------------------------------------------------------------- unit -> draws
def unit_draws(insts, draws, frames, plot_centres=None):
    """{(short frame, object): {pass: [draw n]}}. plot_centres: {(short frame, object): (x, y)} (3909's figures.json
    `centre`), the positions material 87 draws are matched to."""
    out = defaultdict(lambda: defaultdict(set))
    scene_owner = {}
    centres = defaultdict(list)
    for k, c in (plot_centres or {}).items():
        centres[k].append((float(c[0]), float(c[1])))
    for r in insts:
        fr = short(r["frame"])
        if not r.get("draw"):
            continue
        scene_owner[(fr, int(r["draw"]))] = r["object"]
    for fr, ds in draws.items():
        by_key = {}
        for d in ds:
            if d["pass"] == "scene":
                if fr == "3901":
                    owner = "1" if d["n"] in WARRIOR_3901_SCENE else "2"
                    if d["vs"][:8] in ("82e59838", "59af33ff"):
                        owner = None
                else:
                    owner = scene_owner.get((fr, d["n"]))
                if owner is None and d["cb0"] not in ("-", ""):
                    owner = nearest_by_cb0(fr, d, frames, centres)
                if owner is not None:
                    out[(fr, owner)]["scene"].add(d["n"])
                    by_key[(d["ic"], id_set(d))] = owner
        for d in ds:
            if d["pass"] in ("reflection", "mask"):
                k = (d["ic"], id_set(d))
                owner = by_key.get(k)
                if owner is None and d["cb0"] not in ("-", ""):
                    owner = nearest_by_cb0(fr, d, frames, centres)
                if owner is not None:
                    out[(fr, owner)][d["pass"]].add(d["n"])
    return out


def id_set(d):
    """A draw's ids as a set: the order of the ids within a draw differs per pass (depth-sorted, unit-renderer.md
    10.4), so the reflection and mask draws of a scene draw hold the same ids in another order. Material 87 draws
    carry no ids: their cb0 file's digest instead (matched by position when it differs per pass)."""
    if d["ids"] in ("-", ""):
        return "cb0:%s" % d["cb0"].split("/")[-1].split("_")[-1]
    return tuple(sorted(int(x) for x in d["ids"].split(",") if x))


def nearest_by_cb0(fr, d, frames, centres):
    try:
        b = np.fromfile(os.path.join(frames[fr], d["cb0"]), dtype="<f4").reshape(-1, 4)
        x, y = float(b[0][3]), float(b[1][3])
    except (OSError, ValueError, IndexError):
        return None
    best, bd = None, 64.0
    for (f2, ob), pts in centres.items():
        if f2 != fr:
            continue
        p = np.median(np.array(pts), axis=0)
        dist = float(np.hypot(p[0] - x, p[1] - y))
        if dist < bd:
            best, bd = ob, dist
    return best


# ---------------------------------------------------------------------------------------------- C
def stats(v):
    if v.size == 0:
        return {"pixels": 0}
    return {"pixels": int(v.size), "equal": int((v == 0).sum()), "median": float(np.median(v)),
            "p95": float(np.percentile(v, 95)), "max": int(v.max())}


def load_rgba(p, W, H):
    b = np.fromfile(p, np.uint8)
    if b.size != W * H * 4:
        raise ValueError("%s: %d bytes, not %d x %d x 4" % (p, b.size, W, H))
    return b.reshape(H, W, 4)


def replay(export, out_dir, draw_list, name, tr_exe, log):
    out_dir.mkdir(parents=True, exist_ok=True)
    r = run([sys.executable, "-I", REPO / "models/tools/unitreplay/unit_manifest.py", export, out_dir, "--draws",
             ",".join(str(n) for n in draw_list), "--exe", tr_exe, "--name", name], log)
    if r.returncode != 0:
        return None
    info = json.loads((out_dir / ("%s.json" % name)).read_text())
    return info


def ours(oracle_exe, man, frame, draw_list, art, W, H, out_rgba, game, db, exe, log):
    r = run([oracle_exe, "--draw", "--manifest", man, "--frame", frame, "--draws", ",".join(str(n) for n in draw_list),
             "--art", art, "--game", game, "--mods", MODS_LIST, "--db", db, "--exe", exe, "--out-rgba", out_rgba,
             "--width", W, "--height", H], log)
    return r.returncode == 0, (r.stderr or r.stdout).strip()[-200:]


def run_c(work: Path, man, frames, mapping, arts, oracle_exe, tr_exe, game, db, exe, log, only=None, visual=True,
          stop=None, passes=("scene", "reflection", "mask")):
    """arts: {(short frame, object): art row}. Returns {(frame, object): {pass: stats|{'error': ...}}}."""
    res = defaultdict(dict)
    pics = work / "pictures"
    pics.mkdir(parents=True, exist_ok=True)
    tmp = work / "tmp"
    by_frame = defaultdict(list)
    for (fr, ob), passes in mapping.items():
        if only and fr not in only:
            continue
        by_frame[fr].append((ob, passes))
    vc = []
    for fr in sorted(by_frame):
        export = frames[fr]
        for pas in passes:
            if stop and os.path.exists(stop):
                return res, vc
            units = [(ob, sorted(p.get(pas, ()))) for ob, p in sorted(by_frame[fr], key=lambda x: int(x[0]))]
            units = [(ob, d) for ob, d in units if d]
            if not units:
                continue
            if tmp.exists():
                shutil.rmtree(tmp)
            tmp.mkdir(parents=True)
            frame_ours = frame_cap = frame_diff = None
            if pas != "mask":
                alld = sorted(n for _, d in units for n in d)
                info = replay(export, tmp, alld, "captured", tr_exe, log)
                if info is None:
                    for ob, _ in units:
                        res[(fr, ob)][pas] = {"error": "captured replay failed"}
                    continue
                W, H = info["size"]
                cap = load_rgba(tmp / "captured.rgba", W, H)
                ids = np.fromfile(tmp / "captured_id.u32", np.uint32).reshape(H, W)
                order = info["draws"]
                frame_ours = np.zeros((H, W, 3), np.uint8)
                frame_cap = cap[..., :3].copy()
                frame_diff = np.zeros((H, W), np.uint8)
            for ob, dl in units:
                art = arts.get((fr, ob))
                if not art:
                    res[(fr, ob)][pas] = {"error": "no art row"}
                    continue
                if pas == "mask":
                    ud = tmp / ("u%s" % ob)
                    info = replay(export, ud, dl, "captured", tr_exe, log)
                    if info is None:
                        res[(fr, ob)][pas] = {"error": "captured replay failed"}
                        continue
                    W, H = info["size"]
                    capu = load_rgba(ud / "captured.rgba", W, H)
                ok, why = ours(oracle_exe, man, fr, dl, art, W, H, tmp / "ours.rgba", game, db, exe, log)
                if not ok:
                    res[(fr, ob)][pas] = {"error": "ours: " + why}
                    continue
                o = load_rgba(tmp / "ours.rgba", W, H)
                if pas == "mask":
                    cover = capu.any(2) | o.any(2)
                    diff = np.abs(capu.astype(np.int16) - o.astype(np.int16))[..., :3].max(2)
                    if frame_ours is None:
                        frame_ours = np.zeros((H, W, 3), np.uint8)
                        frame_cap = np.zeros((H, W, 3), np.uint8)
                        frame_diff = np.zeros((H, W), np.uint8)
                    frame_cap[cover] = capu[..., :3][cover]
                    shutil.rmtree(ud, ignore_errors=True)
                else:
                    idx = [i for i, n in enumerate(order) if n in set(dl)]
                    cover = np.isin(ids, np.array(idx, np.uint32))
                    diff = np.abs(cap.astype(np.int16) - o.astype(np.int16))[..., :3].max(2)
                s = stats(diff[cover])
                s["draws"] = ",".join(str(n) for n in dl)
                res[(fr, ob)][pas] = s
                frame_ours[cover] = o[..., :3][cover]
                frame_diff[cover] = np.maximum(frame_diff[cover], np.clip(diff[cover] * 8, 0, 255).astype(np.uint8))
                (tmp / "ours.rgba").unlink(missing_ok=True)
            if frame_ours is not None and visual:
                cov = frame_ours.any(2) | (frame_diff > 0) | frame_cap.any(2)
                ys, xs = np.nonzero(cov)
                if ys.size:
                    y0, y1, x0, x1 = max(0, ys.min() - 8), min(cov.shape[0], ys.max() + 9), max(0, xs.min() - 8), \
                        min(cov.shape[1], xs.max() + 9)
                    base = pics / ("%s_%s" % (fr, pas))
                    png(str(base) + "_ours.png", np.ascontiguousarray(frame_ours[y0:y1, x0:x1]))
                    png(str(base) + "_captured.png", np.ascontiguousarray(frame_cap[y0:y1, x0:x1]))
                    d3 = np.repeat(frame_diff[y0:y1, x0:x1, None], 3, axis=2)
                    png(str(base) + "_diff8.png", np.ascontiguousarray(d3))
                    vc.append((fr, pas, str(base)))
        if tmp.exists():
            shutil.rmtree(tmp)
    return res, vc


def record_visual(vc, res, log):
    """One Visual Checks entry per frame and pass: ours with the captured replay as reference, the difference x8."""
    ids = []
    for fr, pas, base in vc:
        rows = [v[pas] for (f, _o), v in res.items() if f == fr and pas in v and "pixels" in v[pas]]
        pix = sum(r["pixels"] for r in rows)
        eq = sum(r.get("equal", 0) for r in rows)
        mx = max((r.get("max", 0) for r in rows), default=0)
        what = ("DIAGNOSTIC: unit module's own draws (epic 0041 oracle, 4355) frame %s %s pass, %d units, against the "
                "captured draws replayed by terrainreplay: %d pixels, %d equal, max %d (per unit in report_units.tsv)"
                % (fr, pas, len(rows), pix, eq, mx))
        r = run([sys.executable, "-I", REPO / "tools/visualcheck/visualcheck.py", "record", base + "_ours.png",
                 "--what", what, "--result", "pass" if pix == eq else "info", "--issue", "4355",
                 "--instance", "offline", "--reference", base + "_captured.png", "--ref-how", "same",
                 "--comparison", base + "_diff8.png"], log)
        ids.append(r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else "record failed")
    return ids
