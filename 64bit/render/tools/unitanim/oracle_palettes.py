"""oracle_palettes.py - DoD E for blended programs (epic 0374, issue 4406 round 2; research unit-controller.md 15.8).
Used by oracle_anim.py's `palettes` step: the controller's pose programs (the driver's programs.csv and masks.csv) are
turned into palette rows by the module's pose step (`unitscene program`: poseFigureProgram, the step behind
ModulePose::rowsProgram, unplaced: scale 1, yaw 0, T 0), the placement is solved against the captured block as
posefit.py does (rotation, scale, translation by least squares), and the residual is scored with 0041's tolerances
(rotation element < 1e-3, translation < 0.01 world units). The same is done for 4407's fitted two-clip program of the
block, as the reference line. Inputs are read at run time from the work folders (captured palettes, the extracted unit
models); outputs are numbers (D83).
"""
import glob
import json
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "models", "tools", "posefit"))
import posefit as pf  # noqa: E402

ROT_ELEM_TOL = 1e-3
TRANS_TOL = 0.01


def program_text(nodes, masks):
    """programs.csv node rows (+ masks.csv rows of the figure and frame) -> unitscene's program text."""
    items = []
    for n in nodes:
        if n["kind"] == "sample":
            items.append("S|%s|%s" % (n["clip"], n["t"]))
        else:
            it = "B|%s|%s|%s" % (n["a"], n["b"], n["w"])
            if n["mask"] not in ("-", ""):
                it += "|" + n["mask"]
            items.append(it)
    for m in sorted(masks, key=lambda r: int(r["mask"])):
        items.append("M|%s|%s|%s" % (m["binding"], m["default_weight"], m["target_weight"]))
    return ";".join(items)


def _gsd(units_dir):
    g = glob.glob(os.path.join(units_dir, "*_stringdatabase.gsd"))
    return g[0] if g else "-"


_MODELS, _FIGURES, _STRINGS = {}, {}, {}


def capture(bones, inst_id, mesh_name, model_path, gsd):
    """(palette rows (nb, 3, 4), nb, index_count) of block `inst_id` drawn with mesh `mesh_name`, or None. Models,
    string databases and capture files are read once per process."""
    if gsd not in _STRINGS:
        _STRINGS[gsd] = pf.gr2read.load_gsd(gsd)[0] if gsd != "-" else None
    if model_path not in _MODELS:
        _MODELS[model_path] = pf.Model(model_path, _STRINGS[gsd])
    if bones not in _FIGURES:
        _FIGURES[bones] = pf.load_figures([bones], None)
    m = _MODELS[model_path]
    for f in _FIGURES[bones]:
        if f["id"] != inst_id:
            continue
        me = next((x for x in m.meshes if x["index_count"] == f["index_count"] and x["name"] == mesh_name), None)
        if me:
            nb = len(me["bindings"])
            return f["rows"][2:2 + 3 * nb].reshape(nb, 3, 4), nb, me["index_count"]
    return None


def compare(blocks, unitscene, units_dir, work):
    """blocks: [{key, bones, id, mesh, model, program}] -> {key: result}. `program` is unitscene's text form."""
    os.makedirs(work, exist_ok=True)
    gsd = _gsd(units_dir)
    rows, lines, caps, res = [], [], {}, {}
    for b in blocks:
        model_path = os.path.join(units_dir, b["model"])
        c = capture(b["bones"], b["id"], b["mesh"], model_path, gsd)
        if c is None:
            res[b["key"]] = {"status": "NoCapture", "message": "block %s / %s not in %s" % (b["id"], b["mesh"],
                                                                                           b["bones"])}
            continue
        pal, nb, ic = c
        caps[b["key"]] = pal
        off = sum(len(r) for r in rows)
        rows.append(pal.reshape(-1).astype(np.float32))
        lines.append("\t".join([b["key"], model_path, gsd, b["mesh"], units_dir, gsd, "1", "0", "0", "0", "0",
                                str(off), str(nb), str(ic), b["program"]]))
    if not lines:
        return res
    lst, rf, out, ours = (os.path.join(work, n) for n in ("program_list.tsv", "captured.f32", "program_out.tsv",
                                                            "ours.f32"))
    with open(lst, "w", encoding="utf-8", newline="\n") as f:
        f.write("key\tmodel\tmodel_gsd\tmesh\tclip_dir\tclip_gsd\tscale\tyaw_deg\tTx\tTy\tTz\toffset\tnbind\t"
                "index_count\tprogram\n")
        f.write("\n".join(lines) + "\n")
    np.concatenate(rows).astype("<f4").tofile(rf)
    p = subprocess.run([unitscene, "program", "--list", lst, "--rows", rf, "--out", out, "--rows-out", ours],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError("unitscene program exit %d: %s" % (p.returncode, (p.stderr or p.stdout).strip()[:400]))
    ourv = np.fromfile(ours, dtype="<f4").astype(float)
    with open(out, encoding="utf-8") as f:
        hdr = f.readline().rstrip("\n").split("\t")
        for line in f:
            r = dict(zip(hdr, line.rstrip("\n").split("\t")))
            k = r["key"]
            if r["status"] != "Ok":
                res[k] = {"status": r["status"], "message": r.get("message", "")}
                continue
            pal = caps[k]
            nb = len(pal)
            o = int(r["ours_offset"])
            sk = ourv[o:o + nb * 12].reshape(nb, 3, 4)
            ra, s, ta = pf.place(sk, pal)
            pred = pf.predict(sk, ra, s, ta)
            rot, tr, _ = pf.errors(pred, pal)
            rem = float(np.abs(pal[:, :, :3] - pred[:, :, :3]).max())
            res[k] = {"status": "Ok", "nbind": nb, "scale": round(float(s), 6),
                      "rot_max_deg": round(float(rot.max()), 6), "rot_elem_max": round(rem, 6),
                      "trans_max": round(float(tr.max()), 6),
                      "within": bool(rem < ROT_ELEM_TOL and float(tr.max()) < TRANS_TOL)}
    return res
