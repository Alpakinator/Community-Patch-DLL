"""Issue 4355 (epic 0041, DoD I): the oracle run of the renderer's unit module over the oracle set, one command, no
game instance. Own code, 2026-11-15. Research: research/05-graphics-composition/unit-renderer.md section 13.

    python -I -X utf8 render/tools/unitscene/oracle.py --out C:/c5-64bit-work/0041/4355/oracle
           --exe C:/c5-64bit-work/render-build/<key>/Release/tools/unitscene/unitscene.exe [--game INSTALL] [--frames F,...]

Every input defaults to the oracle set of worker-common.md (read only): 3907's index and frame records and exports,
3909's per-frame unit records, 0327's K3 fits of every catalogue instance (exact_t.py's instances.csv: clip, t, s,
yaw, T per mesh-instance), 0327's extraction (gr2 manifest, archives) with c5h-c's install and MODS, the catalogue's
database copy, the bundle's height field, 4354's catalogue text. Steps, each written to OUT:
  1. placement (DoD D): `unitscene placement` (4354's catalogue check) -> placement_report.tsv, the water level.
  2. scenes (DoD I, file form): one .c5units per catalogue frame from the index (art row, owner, plot), 4354's
     catalogue (facing, seed: unit-formations.md 12.1's rule), the frame records (unit ids) and the K3 fits (clip and
     t per figure, figures numbered by the id order, premise P8); `unitscene scene` reads every file back and writes
     it again (round trip); a broken copy must be refused (negative case).
  3. pose (DoD E, premise P9): every fitted instance's captured palette rows (t27 block of the draw's id, rows
     2 .. 1 + 3 n) against the module's pose layer at the fitted clip, t, s, yaw, T -> pose.tsv.
  4. P8: per unit, the members of its instances in id order against ArtDefine_UnitInfoMemberInfos row order.
  5. report: report_units.tsv (one row per placed unit: B to E), summary.json; B (draw inputs) and C (draws per
     pass) need 4352's UnitRenderer and are reported as not run with that cause until it is in the build.
Exit codes: 0 report written, 1 a step failed, 2 usage, 3 an input missing, 4 OUT outside the work folders (D83).
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import json
import math
import os
import sqlite3
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "models" / "tools" / "formations"))
sys.path.insert(0, str(REPO / "models" / "tools" / "posefit"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROWS = 98
WORK_ROOTS = ("c:/c5-64bit-work/", "f:/c5-64bit-work/")
INST = os.path.expanduser("~").replace("\\", "/") + "/c5-instances/c5h-c"
DEFAULTS = {
    "index": str(REPO / "shell" / "evidence" / "0301" / "3907" / "index.csv"),
    "runs": "F:/c5-64bit-work/0301/3907/runs",
    "units_dir": "F:/c5-64bit-work/0301/3909/out/units",
    "instances": "F:/c5-64bit-work/0327/4153/w2b/all/instances.csv",
    "manifest": "F:/c5-64bit-work/0327/extract/gr2_manifest.tsv",
    "fpk_root": "F:/c5-64bit-work/0327/extract/fpk",
    "install": INST + "/install",
    "mods": INST + "/Documents/My Games/Sid Meier's Civilization 5/MODS",
    "db": "F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db",
    # 4406 (MC #2108): the TINY_0009 bundle as 0364 re-recorded it at format 3; the 070309 bundle is in
    # scene-library/retired/ (unit-controller.md 15)
    "heightfield": "F:/c5-64bit-work/scene-library/bundles/C5H-0301-TINY_0009_20261115-225124_workstation/"
                   "heightfield/load0003.c5hf",
    "catalogue": str(REPO / "models" / "evidence" / "0041" / "4354" / "catalogue_figures.tsv"),
}
POSE_ROT_TOL = 0.05    # degrees: 0327's fit criterion (unit-playback.md 1, unit-formations.md 12.2)
POSE_TRANS_TOL = 0.01  # world units (0327: p95 0.0009)
FIT_TILT_TOL = 0.05    # degrees: a fit whose placement is not a rotation about z alone (rigid-mesh ambiguity: the free
                       # rotation absorbs any pose, runner-up ratio 1; or an aircraft): the pose cannot be compared


class InputError(Exception):
    pass


def under_work(p: Path) -> bool:
    s = str(p.resolve()).replace("\\", "/").lower()
    return any(s.startswith(r) for r in WORK_ROOTS)


def run(cmd, log):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    log.write("$ %s\nexit %d\n%s%s\n" % (" ".join(str(c) for c in cmd), r.returncode, r.stdout, r.stderr))
    return r


# ---------------------------------------------------------------------------------------------- inputs
def read_index(path):
    rows = [r for r in csv.DictReader(open(path, encoding="utf-8")) if r["kind"] == "unit"]
    if not rows:
        raise InputError("no unit rows in %s" % path)
    return rows


def read_catalogue(path):
    units = {}
    cur = None
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if f[0] == "unit":
            cur = {"frame": f[1], "object": int(f[2]), "art_row": f[3], "plot": (int(f[4]), int(f[5])),
                   "facing": int(f[6]), "seed": int(f[7]), "members": []}
            units[(f[1], int(f[2]))] = cur
        elif f[0] == "member" and cur is not None:
            cur["members"].append((f[1], int(f[2])))
    return units


def read_instances(path):
    return list(csv.DictReader(open(path, encoding="utf-8")))


def frame_key(scenario, frame):
    return "%s_%s" % (scenario, frame)


# ---------------------------------------------------------------------------------------------- P8 and figures
def unit_figures(insts, members, meshes_per_member):
    """P8 as a rule: the unit's blocks in block order, cut into figures in ArtDefine_UnitInfoMemberInfos row order, each
    member repeated NumMembers times and taking as many consecutive blocks as its model draws meshes (4352's B report).
    insts: the unit's K3 rows without the attachment instances of no member model. Returns (figures [(member,
    [instances])], verdict, why): "yes" when every instance whose member the fit attributed unambiguously (one
    candidate model) lies in a figure of that member, "no" when one does not, "n/a" when the blocks do not make the
    members' count or nothing is attributed."""
    seq = sorted(insts, key=lambda r: (r["_buf"], int(r["id"])))
    expected = [m for m, n in members for _ in range(n)]
    sizes = [meshes_per_member.get(m, 0) for m in expected]
    if not expected or not seq or any(z == 0 for z in sizes) or sum(sizes) != len(seq):
        return [], "n/a", "%d blocks, the members want %d (meshes per member %s)" % (
            len(seq), sum(sizes), ",".join("%s:%d" % (m.replace("ART_DEF_UNIT_MEMBER_", ""), meshes_per_member.get(m, 0))
                                           for m, _ in members))
    figs, at = [], 0
    for m, z in zip(expected, sizes):
        figs.append((m, seq[at:at + z]))
        at += z
    known = [(m, x) for m, xs in figs for x in xs if x["member"] and x.get("candidates", "1") == "1"]
    bad = [(m, x) for m, x in known if x["member"] != m]
    if bad:
        return figs, "no", "; ".join("id %s fitted to %s, figure of %s" % (x["id"], x["member"], m) for m, x in bad[:4])
    if not known:
        return figs, "n/a", "no instance attributed to one member model alone"
    return figs, "yes", "%d of %d blocks attributed" % (len(known), len(seq))


# ---------------------------------------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--exe", required=True, help="unitscene.exe of a render build")
    ap.add_argument("--game", default=os.environ.get("C5R_GAME_DIR", INST + "/install"))
    ap.add_argument("--frames", default="", help="only these frames (units_land_L01,...)")
    ap.add_argument("--no-c", action="store_true", help="skip C (the module's draws against the captured ones; GPU)")
    ap.add_argument("--c-frames", default="", help="C only for these short frames (L01,S03,3901,...)")
    ap.add_argument("--no-visual", action="store_true", help="C without Visual Checks entries (tests)")
    ap.add_argument("--reuse-c", action="store_true", help="take C from OUT/bc/c_results.json of an earlier run")
    ap.add_argument("--c-passes", default="", help="with --reuse-c: run C again for these passes (scene,reflection,mask)"
                    " and replace their results")
    for k, v in DEFAULTS.items():
        ap.add_argument("--" + k.replace("_", "-"), dest=k, default=v)
    a = ap.parse_args(argv)
    out = Path(a.out)
    if not under_work(out):
        print("oracle: OUT must be under C:/c5-64bit-work/ or F:/c5-64bit-work/ (D83)", file=sys.stderr)
        return 4
    for k in DEFAULTS:
        if not os.path.exists(getattr(a, k)):
            print("oracle: input %s missing: %s" % (k, getattr(a, k)), file=sys.stderr)
            return 3
    if not os.path.isfile(a.exe):
        print("oracle: unitscene.exe missing: %s" % a.exe, file=sys.stderr)
        return 3
    out.mkdir(parents=True, exist_ok=True)
    log = open(out / "oracle.log", "w", encoding="utf-8")
    only = set(a.frames.split(",")) if a.frames else None
    summary = {"inputs": {k: getattr(a, k) for k in DEFAULTS}, "game": a.game}
    build = Path(a.exe).resolve().parents[2]
    draw_oracle = build / "tests" / "units" / "c5r_units_test_draw_oracle.exe"
    tr_exe = build / "tools" / "terrainreplay" / "terrainreplay.exe"
    game_exe = os.path.join(a.game, "CivilizationV_DX11.exe")
    for p_ in (draw_oracle, tr_exe, game_exe):
        if not os.path.isfile(p_):
            print("oracle: %s missing (the render build's draw oracle and terrainreplay, the install's EXE)" % p_,
                  file=sys.stderr)
            return 3
    import oracle_draws as OD   # noqa: E402  (B and C)

    # 1. placement
    r = run([a.exe, "placement", "--game", a.game, "--heightfield", a.heightfield, "--db", a.db,
             "--catalogue", a.catalogue, "--out", out / "placement_report.tsv"], log)
    water = None
    if r.returncode == 0:
        line = [x for x in r.stdout.splitlines() if x.startswith("placement ")][0].split()
        water = float(line[2])
        summary["placement"] = dict(zip(line[3::2], (int(x) for x in line[4::2])))
    else:
        summary["placement"] = {"error": (r.stderr or r.stdout).strip()[-400:]}
    print("placement:", summary["placement"], "water", water)

    # B: 4352's draw-input checks over the manifest (3907's frames and 3901)
    bc = out / "bc"
    bc.mkdir(exist_ok=True)
    man = OD.write_manifest(bc, a.index, a.db, log)
    bunits, bcb1, bsum, bmeshes = OD.run_b(bc, man, draw_oracle, a.game, a.db, game_exe, log)
    summary["B"] = {"summary": bsum, "cb1": {"%s %s" % k: v for k, v in sorted(bcb1.items())}}
    print("B:", bsum)

    # inputs of 2-4
    index = read_index(a.index)
    cat = read_catalogue(a.catalogue)
    con = sqlite3.connect("file:%s?mode=ro" % a.db.replace("\\", "/"), uri=True)
    civ = {t: (c, s) for t, c, s in con.execute("select Type, DefaultPlayerColor, ArtStyleType from Civilizations")}
    civ.update({t: (c, s) for t, c, s in
                con.execute("select Type, DefaultPlayerColor, ArtStyleType from MinorCivilizations")})
    members = defaultdict(list)
    member_model = {t: m for t, m in con.execute("select Type, Model from ArtDefine_UnitMemberInfos")}
    for u, m, n in con.execute("select UnitInfoType, UnitMemberInfoType, NumMembers from ArtDefine_UnitInfoMemberInfos"
                               " order by rowid"):
        members[u].append((m, n))
    con.close()
    insts = read_instances(a.instances)
    import exact_t   # noqa: E402  (0327's art-file look-up over archives, install, MODS; per-file string database)
    art = exact_t.ArtFiles(a.manifest, a.fpk_root, a.install, a.mods)
    units_rec = {}

    def map_clip(clip, fitted_member, member):
        """The fitted clip as the figure's own member names it: the same file if its .fxsxml has it, else its clip
        with the same ec code (two members sharing a model's meshes, e.g. missionary _01 and _02)."""
        if fitted_member == member or not fitted_member:
            return clip
        a_ = art.member(member_model.get(fitted_member))["clips"]
        b_ = art.member(member_model.get(member))["clips"]
        if any(os.path.basename(c).lower() == clip.lower() for c, _ in b_):
            return clip
        ecs = next((e for c, e in a_ if os.path.basename(c).lower() == clip.lower()), set())
        return next((os.path.basename(c) for c, e in b_ if e & ecs), None)

    def unit_rec(frame):
        if frame not in units_rec:
            p = Path(a.units_dir) / (frame + ".json")
            units_rec[frame] = json.load(open(p, encoding="utf-8")) if p.is_file() else None
        return units_rec[frame]

    # attach the captured block's location to every fitted instance
    by_unit = defaultdict(list)
    for row in insts:
        fr = row["frame"]
        if only and fr not in only:
            continue
        rec = unit_rec(fr)
        row["_buf"] = ""
        if rec:
            d = next((d for d in rec["draws"] if str(d["n"]) == row["draw"]), None)
            if d and d.get("t27"):
                row["_buf"] = os.path.join(rec["export"], "buffers", "%s_%s.bin" % (d["t27"]["res"], d["t27"]["sha"]))
        by_unit[(fr, int(row["object"]))].append(row)

    # 3. pose: list and rows
    lst = ["key\tmodel\tmodel_gsd\tmesh\tclip\tclip_gsd\tt\tscale\tyaw_deg\tTx\tTy\tTz\toffset\tnbind\tindex_count"]
    rows_out, off, not_listed, tilt = [], 0, defaultdict(int), {}
    bufs = {}

    def gsd(path):
        s = art.sdb_of.get(os.path.normcase(os.path.normpath(path)), "UNION")
        return "-" if s == "NONE" else ("UNION" if s in ("UNION", "") else s)

    for (fr, ob), rows in sorted(by_unit.items()):
        for row in rows:
            if row["status"] != "ok":
                not_listed["fit: " + row["status"].split(":")[0]] += 1
                continue
            mp, cp = art.find(row["model"]), art.find(row["clip"])
            if not mp or not cp or not row["_buf"] or not os.path.isfile(row["_buf"]):
                not_listed["file missing"] += 1
                continue
            if row["_buf"] not in bufs:
                bufs[row["_buf"]] = np.fromfile(row["_buf"], dtype="<f4").reshape(-1, 4)
            buf = bufs[row["_buf"]]
            nb, iid = int(row["bindings"]), int(row["id"])
            if (iid + 1) * ROWS > len(buf):
                not_listed["block past the buffer"] += 1
                continue
            blk = buf[iid * ROWS + 2: iid * ROWS + 2 + 3 * nb].reshape(nb, 3, 4)
            rows_out.append(blk.astype("<f4").ravel())
            key = "%s|%d|%s|%s" % (fr, ob, row["draw"], row["id"])
            tilt[key] = float(row["tilt"] or 0)
            lst.append("\t".join([key, mp, gsd(mp), row["mesh"], cp, gsd(cp), row["t"], row["s"], row["yaw"],
                                  row["Tx"], row["Ty"], row["Tz"], str(off), str(nb), row["index_count"]]))
            off += nb * 12
    (out / "pose_list.tsv").write_text("\n".join(lst) + "\n", encoding="utf-8")
    (np.concatenate(rows_out) if rows_out else np.zeros(0, "<f4")).astype("<f4").tofile(out / "pose_rows.f32")
    (out / "gsd_union.txt").write_text("\n".join(sorted(set(art.manifest_sdbs))) + "\n", encoding="utf-8")
    r = run([a.exe, "pose", "--list", out / "pose_list.tsv", "--rows", out / "pose_rows.f32", "--out",
             out / "pose.tsv", "--gsd-union", out / "gsd_union.txt"], log)
    if r.returncode != 0:
        print("oracle: unitscene pose failed (exit %d), see oracle.log" % r.returncode, file=sys.stderr)
        return 1
    summary["pose_tool"] = r.stdout.strip()
    pose = {p["key"]: p for p in csv.DictReader(open(out / "pose.tsv", encoding="utf-8"), delimiter="\t")}
    print(summary["pose_tool"])

    # C: the module's own draws per unit and pass against the captured draws
    frames_m, draws_m = OD.read_manifest(man)
    fig_json = Path(a.units_dir).parent / "figures.json"
    plot_centres = {}
    if fig_json.is_file():
        for u in json.load(open(fig_json, encoding="utf-8")).get("units", []):
            if u.get("centre"):
                plot_centres[(OD.short(u["frame"]), str(u["object"]))] = u["centre"]
    mapping = OD.unit_draws(insts, draws_m, frames_m, plot_centres)
    arts = {(OD.short(frame_key(r["scenario"], r["frame"])), r["object"]): r["art_row"] for r in index}
    arts.update({("3901", u[0]): u[2] for u in OD.UNITS_3901})
    cres, vc_ids = {}, []
    cjson = bc / "c_results.json"
    if a.reuse_c:
        if not cjson.is_file():
            print("oracle: --reuse-c but no %s" % cjson, file=sys.stderr)
            return 3
        j = json.load(open(cjson, encoding="utf-8"))
        cres = {tuple(k.split("|")): v for k, v in j["results"].items()}
        vc_ids = j.get("visual_checks", [])
        if a.c_passes:
            pas_ = tuple(x for x in a.c_passes.split(",") if x)
            cf = set(a.c_frames.split(",")) if a.c_frames else None
            for v in cres.values():
                for p_ in pas_:
                    v.pop(p_, None)
            new, vc = OD.run_c(bc, man, frames_m, mapping, arts, draw_oracle, tr_exe, a.game, a.db, game_exe, log,
                               only=cf, visual=not a.no_visual, stop=str(out / "STOP"), passes=pas_)
            for k_, v in new.items():
                cres.setdefault(k_, {}).update(v)
            if not a.no_visual:
                vc_ids = vc_ids + OD.record_visual(vc, cres, log)
            cjson.write_text(json.dumps({"results": {"%s|%s" % k: v for k, v in cres.items()},
                                         "visual_checks": vc_ids}, indent=1), encoding="utf-8")
    elif not a.no_c:
        cf = set(a.c_frames.split(",")) if a.c_frames else None
        cres, vc = OD.run_c(bc, man, frames_m, mapping, arts, draw_oracle, tr_exe, a.game, a.db, game_exe, log,
                            only=cf, visual=not a.no_visual, stop=str(out / "STOP"))
        if not a.no_visual:
            vc_ids = OD.record_visual(vc, cres, log)
        cjson.write_text(json.dumps({"results": {"%s|%s" % k: v for k, v in cres.items()}, "visual_checks": vc_ids},
                                    indent=1), encoding="utf-8")
    summary["C_visual_checks"] = vc_ids

    def c_text(key, pas):
        if a.no_c and not a.reuse_c:
            return "not run (--no-c)"
        v = cres.get(key, {}).get(pas)
        if v is None:
            if not mapping.get(key, {}).get(pas):
                return "no %s draw mapped to this unit" % pas
            return "not run (frame filter)"
        if "error" in v:
            return "error: " + v["error"]
        if not v.get("pixels"):
            return "0 pixels (draws %s)" % v.get("draws")
        return "pixels %d equal %d median %g p95 %g max %d (draws %s)" % (
            v["pixels"], v["equal"], v["median"], v["p95"], v["max"], v["draws"])

    # 2 + 4: scenes, P8, the per-unit report
    scenes_dir = out / "scenes"
    scenes_dir.mkdir(exist_ok=True)
    frames = defaultdict(list)
    for row in index:
        fk = frame_key(row["scenario"], row["frame"])
        if not only or fk in only:
            frames[fk].append(row)
    report = []
    figs_of = {}
    p8 = defaultdict(int)
    for fk, objs in sorted(frames.items()):
        scen, frame = fk.rsplit("_", 1)
        rec_path = Path(a.runs) / scen / (frame + ".json")
        frec = json.load(open(rec_path, encoding="utf-8")) if rec_path.is_file() else {"objects": []}
        players, units = {}, []
        for row in objs:
            ob = int(row["object"])
            pid = int(row["owner_id"])
            colour, style = civ.get(row["owner_civ"], ("", ""))
            players.setdefault(pid, (colour or "PLAYERCOLOR_UNKNOWN", row["owner_civ"], row["owner_era"]))
            c = cat.get((fk, ob), {})
            fo = frec["objects"][ob - 1] if ob - 1 < len(frec.get("objects", [])) else {}
            uid = int(fo.get("unit_id", ob))
            mem = members.get(row["art_row"], [])
            ui = by_unit.get((fk, ob), [])
            figs, order_ok, why = unit_figures(
                [x for x in ui if x["_buf"]], mem,
                bmeshes.get((OD.short(fk), str(ob)), {}))
            p8[order_ok] += 1
            figs_of[(fk, ob)] = (figs, mem)
            figure_lines = []
            for k, (m, xs) in enumerate(figs):
                # the figure's clip and t from its most telling mesh fit: an unambiguous one (runner-up at least 1.5
                # times the cost; a rigid mesh fits any clip, ratio 1) with the most bound bones
                ok = sorted([x for x in xs if x["status"] == "ok"],
                            key=lambda x: (float(x["runner_up_ratio"] or 1) >= 1.5, int(x["bindings"] or 0)),
                            reverse=True)
                clip = map_clip(ok[0]["clip"], ok[0]["member"], m) if ok else None
                if clip and k < 64:
                    figure_lines.append("figure\t%d\t%s\t%s" % (k, clip, "%.9g" % max(0.0, float(ok[0]["t"]))))
            units.append("\t".join(str(v) for v in [
                "unit", pid, uid, row["type"], row["art_row"], row["plot_x"], row["plot_y"], "-", "-", "-",
                c.get("facing", 2), c.get("seed", 0), 0, 100, "-", 0, 1, 0, 0]))
            units.extend(figure_lines)
            # pose per unit
            keys = ["%s|%d|%s|%s" % (fk, ob, x["draw"], x["id"]) for x in ui]
            pr = [pose[k] for k in keys if k in pose]
            okp = [p for p in pr if p["status"] == "Ok" and tilt.get(p["key"], 0) < FIT_TILT_TOL]
            tilted = [p for p in pr if p["status"] == "Ok" and tilt.get(p["key"], 0) >= FIT_TILT_TOL]
            within = [p for p in okp if float(p["rot_max_deg"]) < POSE_ROT_TOL and float(p["trans_max"]) < POSE_TRANS_TOL]
            causes = sorted({p["status"] + ": " + p["message"][:80] for p in pr if p["status"] != "Ok"} |
                            ({"fit tilt >= %.2f deg on %d mesh-instance(s): not an Rz placement" % (FIT_TILT_TOL, len(tilted))}
                             if tilted else set()) |
                            {"fit " + x["status"][:80] for x in ui if x["status"] != "ok"})
            report.append({
                "frame": fk, "object": ob, "type": row["type"], "art_row": row["art_row"], "owner": pid,
                "members": sum(n for _, n in mem), "instances": len(ui), "figures_from_ids": len(figs),
                "B": OD.b_text(bunits.get((OD.short(fk), str(ob)))),
                "C_scene": c_text((OD.short(fk), str(ob)), "scene"),
                "C_reflection": c_text((OD.short(fk), str(ob)), "reflection"),
                "C_mask": c_text((OD.short(fk), str(ob)), "mask"),
                "D": "placement_report.tsv (4354's check)",
                "E_pose_compared": len(okp), "E_within": len(within),
                "E_rot_max_deg": max((float(p["rot_max_deg"]) for p in okp), default=float("nan")),
                "E_trans_max": max((float(p["trans_max"]) for p in okp), default=float("nan")),
                "P8_order": order_ok, "P8_note": why, "not_covered": "; ".join(causes)[:400]})
        text = ["c5units\t1", "scene\town\t%.9g" % (water if water is not None else 0.0)]
        for pid in sorted(players):
            colour, civt, era = players[pid]
            text.append("player\t%d\t%s\t%s\t%s" % (pid, colour, civt, era))
        text += units
        (scenes_dir / (fk + ".c5units")).write_text("\n".join(text) + "\n", encoding="utf-8")
    # the negative case: a copy with a figure before its unit must be refused (Orphan) and nothing used
    bad_dir = out / "scenes_negative"
    bad_dir.mkdir(exist_ok=True)
    (bad_dir / "orphan.c5units").write_text("c5units\t1\nscene\town\t0\nfigure\t0\tIDLE\t0\n", encoding="utf-8")
    rt = out / "scenes_roundtrip"
    rt.mkdir(exist_ok=True)
    r = run([a.exe, "scene"] + sorted(str(p) for p in scenes_dir.glob("*.c5units")) + ["--write", rt], log)
    neg = run([a.exe, "scene", bad_dir / "orphan.c5units"], log)

    # the module's whole CPU path on the scenes (UnitRenderer::prepare: art, models, pose, placement), its status and
    # warnings per unit, and its palette blocks against the captured ones (block k of the unit = the unit's k-th
    # captured block in id order, P8; rows 0-1 and the bound bones' rows 2 .. 1 + 3 n)
    prep = out / "prepare"
    prep.mkdir(exist_ok=True)
    pr_ = run([a.exe, "prepare", "--game", a.game, "--mods", OD.MODS_LIST, "--db", a.db, "--exe", game_exe,
               "--heightfield", a.heightfield, "--rows", str(prep) + os.sep] +
              sorted(str(p) for p in scenes_dir.glob("*.c5units")), log)
    module = {}
    for line in pr_.stdout.splitlines():
        if line.startswith("unit "):
            f = line.split(" ", 5)
            rest = f[5].split(" | ", 1)
            module[(f[1].replace(".c5units", ""), int(f[2]), int(f[3]))] = (f[4], rest[0], rest[1] if len(rest) > 1 else "")
    summary["module_prepare"] = {"exit": pr_.returncode, "units": len(module),
                                 "codes": dict(Counter(v[0] for v in module.values())),
                                 "with_warnings": sum(1 for v in module.values() if "warnings 0" not in v[1])}
    uid_of = {}
    for fk_ in frames:
        rec_path = Path(a.runs) / fk_.rsplit("_", 1)[0] / (fk_.rsplit("_", 1)[1] + ".json")
        if rec_path.is_file():
            for i_, o_ in enumerate(json.load(open(rec_path, encoding="utf-8")).get("objects", [])):
                uid_of[(fk_, i_ + 1)] = int(o_.get("unit_id", i_ + 1))
    pal_cache, blk_cache, mblk_cache = {}, {}, {}
    mod_stats = []
    for row in report:
        row.setdefault("module", "")
        row.setdefault("E_module", "")
        fk_, ob_ = row["frame"], row["object"]
        if fk_ == "3901":
            continue
        pid = int(row["owner"])
        m = module.get((fk_, pid, uid_of.get((fk_, ob_), ob_)))
        if not m:
            row["module"] = "not in the prepare output"
            continue
        row["module"] = "%s %s%s" % (m[0], m[1], (" | " + m[2][:300]) if m[2] else "")
        if m[0] != "Ok":
            continue
        if fk_ not in pal_cache:
            pal_cache[fk_] = np.fromfile(prep / (fk_ + ".c5units.f32"), dtype="<f4").reshape(-1, 98, 4)
            mblk_cache[fk_] = defaultdict(list)
            for b in csv.DictReader(open(prep / (fk_ + ".c5units.meshblocks.tsv"), encoding="utf-8"), delimiter="\t"):
                mblk_cache[fk_][(int(b["player"]), int(b["id"]))].append(b)
            blk_cache[fk_] = {(int(b["player"]), int(b["id"])): (int(b["first_block"]), int(b["blocks"]))
                              for b in csv.DictReader(open(prep / (fk_ + ".c5units.blocks.tsv"), encoding="utf-8"),
                                                      delimiter="\t")}
        first, nblk = blk_cache[fk_].get((pid, uid_of.get((fk_, ob_), ob_)), (0, 0))
        ours_by_key = {}
        for mb_ in mblk_cache[fk_].get((pid, uid_of.get((fk_, ob_), ob_)), []):
            ours_by_key[(int(mb_["member_row"]), int(mb_["instance"]), mb_["mesh"], int(mb_["index_count"]))] = \
                int(mb_["block"])
        figs_, mem_ = figs_of.get((fk_, ob_), ([], []))
        caps = []
        k = 0
        for ri, (_m, n_) in enumerate(mem_):
            for j in range(n_):
                if k < len(figs_):
                    for x in figs_[k][1]:
                        caps.append(((ri, j, x["mesh"], int(x["index_count"] or 0)), x))
                k += 1
        if not figs_:
            row["E_module"] = "blocks ours %d; captured not split into figures (P8 n/a: %s)" % (nblk, row["P8_note"][:80])
            continue
        worst = [0.0, 0.0, 0.0, 0.0]
        compared = unmatched = 0
        for key_, x in caps:
            if not x.get("bindings") or not x["mesh"]:
                continue
            b_ = ours_by_key.get(key_)
            if b_ is None:
                unmatched += 1
                continue
            nb = int(x["bindings"])
            if x["_buf"] not in bufs:
                bufs[x["_buf"]] = np.fromfile(x["_buf"], dtype="<f4").reshape(-1, 4)
            iid = int(x["id"])
            cap = bufs[x["_buf"]][iid * ROWS:(iid + 1) * ROWS]
            our = pal_cache[fk_][b_]
            d = np.abs(cap[2:2 + 3 * nb].astype(float) - our[2:2 + 3 * nb].astype(float)).reshape(nb, 3, 4)
            worst[0] = max(worst[0], float(d[:, :, :3].max()))
            worst[1] = max(worst[1], float(np.linalg.norm(cap[2:2 + 3 * nb].reshape(nb, 3, 4)[:, :, 3] -
                                                          our[2:2 + 3 * nb].reshape(nb, 3, 4)[:, :, 3], axis=1).max()))
            worst[2] = max(worst[2], float(np.abs(cap[0] - our[0]).max()))
            worst[3] = max(worst[3], float(np.abs(cap[1][:3] - our[1][:3]).max()))
            compared += 1
        order_note = ""
        if any(ours_by_key.get(key_) is not None and ours_by_key[key_] - first != i_
               for i_, (key_, _x) in enumerate(sorted(caps, key=lambda c: (c[1]["_buf"], int(c[1]["id"]))))):
            order_note = "; block order within a figure differs from the capture's"
        row["E_module"] = ("blocks %d, compared %d, unmatched %d: bone rotation element max %.3g, translation max %.3g, "
                           "row 0 max %.3g, row 1 xyz max %.3g%s" % (nblk, compared, unmatched, *worst, order_note))
        mod_stats.append((compared, *worst))
    summary["module_palette"] = {
        "units_compared": len(mod_stats), "blocks_compared": sum(x[0] for x in mod_stats),
        "units_block_order_differs": sum(1 for r_ in report if "block order within a figure differs" in r_.get("E_module", "")),
        "units_rot_elem_below_1e-3": sum(1 for x in mod_stats if x[0] and x[1] < 1e-3),
        "units_trans_below_0.01": sum(1 for x in mod_stats if x[0] and x[2] < 0.01),
        "units_row0_within_1_255": sum(1 for x in mod_stats if x[0] and x[3] <= 1 / 255 + 1e-6),
        "units_row1_equal": sum(1 for x in mod_stats if x[0] and x[4] < 1e-6)}
    summary["scenes"] = {"files": len(frames), "exit": r.returncode,
                         "read_ok": sum(1 for x in r.stdout.splitlines() if " ok players " in x),
                         "round_trip_ok": sum(1 for x in r.stdout.splitlines() if "round trip ok" in x),
                         "negative_exit": neg.returncode, "negative": neg.stdout.strip()}
    neg_ok = neg.returncode == 1 and "refused Orphan at line 3" in neg.stdout
    summary["scenes"]["negative_refused_as_expected"] = neg_ok

    # 3901's two units (in the B and C runs; no scene file, no K3 table rows of 0327's catalogue)
    for u in OD.UNITS_3901:
        key = ("3901", u[0])
        report.append({k: "" for k in report[0]} | {
            "frame": "3901", "object": int(u[0]), "type": u[1], "art_row": u[2], "owner": 0,
            "B": OD.b_text(bunits.get(key)), "C_scene": c_text(key, "scene"),
            "C_reflection": c_text(key, "reflection"), "C_mask": c_text(key, "mask"),
            "D": "not in 4354's catalogue (3901)", "not_covered": "E and P8 not run for 3901 (no K3 table rows)"})
    c_rows = [v for v in cres.values()]
    summary["C"] = {}
    for pas in ("scene", "reflection", "mask"):
        rows = [v[pas] for v in c_rows if pas in v]
        okr = [r for r in rows if r.get("pixels")]
        summary["C"][pas] = {"units": len(rows), "with_pixels": len(okr),
                             "bit_equal": sum(1 for r in okr if r["equal"] == r["pixels"]),
                             "errors": sum(1 for r in rows if "error" in r),
                             "pixels": sum(r["pixels"] for r in okr), "equal": sum(r["equal"] for r in okr),
                             "max": max((r["max"] for r in okr), default=0)}
    with open(out / "report_units.tsv", "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(report[0].keys()), delimiter="\t")
        w.writeheader()
        w.writerows(report)
    allp = list(pose.values())
    okp = [p for p in allp if p["status"] == "Ok" and tilt.get(p["key"], 0) < FIT_TILT_TOL]
    rot = np.array([float(p["rot_max_deg"]) for p in okp]) if okp else np.zeros(1)
    tr = np.array([float(p["trans_max"]) for p in okp]) if okp else np.zeros(1)
    summary["pose"] = {
        "instances_in_fits": len(insts), "listed": len(allp), "compared": len(okp),
        "fit_tilt_not_compared": sum(1 for p in allp if p["status"] == "Ok" and tilt.get(p["key"], 0) >= FIT_TILT_TOL),
        "within": int(((rot < POSE_ROT_TOL) & (tr < POSE_TRANS_TOL)).sum()),
        "rot_deg_median": float(np.median(rot)), "rot_deg_p95": float(np.percentile(rot, 95)),
        "rot_deg_max": float(rot.max()), "trans_median": float(np.median(tr)), "trans_p95": float(np.percentile(tr, 95)),
        "trans_max": float(tr.max()),
        "statuses": dict(sorted(((s, sum(1 for p in allp if p["status"] == s)) for s in {p["status"] for p in allp}))),
        "not_listed": dict(not_listed)}
    summary["P8"] = dict(p8)
    summary["units"] = len(report)
    (out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("pose", "P8", "scenes", "units", "C")}, indent=1))
    log.close()
    return 0 if (r.returncode == 0 and neg_ok) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except InputError as e:
        print("oracle: %s" % e, file=sys.stderr)
        sys.exit(3)
