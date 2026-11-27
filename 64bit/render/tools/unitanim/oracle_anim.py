"""oracle_anim.py - the unit animation controller's oracle run (epic 0374, issue 4406; research unit-controller.md 10
and 15). One command, no game instance: it plays timelines through the offline driver (render/tools/unitanim,
`unitanim play`) on the player's files and compares the controller's choices with the recordings already made.

    python -I -X utf8 render/tools/unitanim/oracle_anim.py --exe <render build>/tools/unitanim/unitanim.exe
        --out F:/c5-64bit-work/0374/4406/oracle [--steps catalogue,sequences,transitions,random] [--quick]

Steps (each writes its table into OUT and its numbers into OUT/summary.json):
  catalogue    C on 0041's catalogue scenes (render/evidence/0041/4355/scenes: 4354's fits as figure lines): per unit
               the forced-choice points of its default state are found, each figure's variant is chosen so that the
               fitted clip is the one played, and frame 0 (dt 0) must give the fitted clip at the fitted t (0.005 s).
               Figures whose fitted clip the default state does not start are named with the cause (reachable or not
               from the default state, by 4408's parse of the machines and graphs).
  sequences    C on the recorded idle sequences (4405's Q1, 3908's P01 through 4407's table): every recorded figure
               on an idle loop at the first frame starts there (forced variant, its t), the recorded Present steps
               become the frames (each captured pair = two presented frames), and each later captured frame must give
               its clip at its t (0.005 s). K6 skips are reported per figure with their count, never absorbed.
               P01 frame 4681 (the fidget figure): scored with no EXE-level fade (the default) and with 4403's
               former rule (omni 6 -> 3, linear 0.3 s), against 4407's blend fit (fidget at 0.200 s, w 0.680).
  transitions  D on three transition kinds of a warrior (idle -> fidget, idle -> run, idle -> attack, and
               combat-ready -> charge run): the program per frame at dt 1/30 from the state change, the new clip's
               share of the pose per frame, against the recorded points (4407's 7.3).
  palettes     E for blended programs (4406 round 2; oracle_palettes.py): the controller's programs at P01 4681 (each
               reading) and for the 4353 mech blocks (idle B -> fidget B from the fit's t1, held), and 4407's fitted
               two-clip programs as the reference, through `unitscene program` against the captured blocks, with the
               placement solved as posefit does; 0041's tolerances (rotation element 1e-3, translation 0.01).
  live0395     ticket 0395's captures (research unit-controller.md 17; not in the default steps): fortify on (the
               ordered warrior's 1500 at the captured t, and a fade point when --fade0395 gives one), the clip-end
               overflow and the clocks between FORT's captures, fortify off by a move, 4405 B's Q8 on the war canoe
               (the embark override), and the battleship's off-centre aim through oracle_palettes (steer 0-2 x w 1/0).
  random       D's random parts on the real omni machine: a warrior idle in free mode over 10^5 figure updates (the
               RandomRoll 0.0007 and the fidget / swap split R1) and 1,000 warriors created at once (the idle A / B
               split R0), each against the file's value in a 99.9 % binomial interval.
Exit: 0 the report written (failed comparisons are rows in it), 1 a step could not run (the driver failed in a way
that is not a comparison), 2 usage, 3 an input missing, 4 OUT not under the work folders.
Inputs are read at run time from the work folders and the install copy; the outputs hold numbers and the game's own
file and clip names, and stay on F: (D83); render/evidence/0374/4406/ gets summary.json and the per-step counts.
"""
import argparse
import collections
import csv
import glob
import json
import math
import os
import re
import sqlite3
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
INST = os.path.expanduser("~").replace("\\", "/") + "/c5-instances/c5h-c"
MODS = INST + "/Documents/My Games/Sid Meier's Civilization 5/MODS"
MODS_LIST = ";".join(MODS + "/" + m for m in ("(1) Community Patch", "(2) Vox Populi",
                                                  "(3a) VP - EUI Compatibility Files"))
DEF = {
    "game": INST + "/install",
    "db": "F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db",
    "census": "F:/c5-64bit-work/0374/4408/census/census.json",
    "table4407": "F:/c5-64bit-work/0374/4407/table_4407.csv",
    "blends4407": "F:/c5-64bit-work/0374/4407/blends_a.json",
    "q1": os.path.join(REPO, "render", "evidence", "0374", "4405", "Q1.csv"),
    "scenes": os.path.join(REPO, "render", "evidence", "0041", "4355", "scenes"),
    # 4406 r2, the palettes step: the extracted unit models and clips (as 4407's blends.py), unitscene.exe ("" = the
    # one beside --exe's build tree)
    "table4405": "F:/c5-64bit-work/0374/4405/tables/table_4405.csv",   # 4406 r3 ("" = without)
    "units_dir": "F:/c5-64bit-work/0327/extract/fpk/UnitModels",
    "unitscene": "",
    "mechfits": os.path.join(REPO, "render", "evidence", "0374", "4406", "r2", "mech_idleb_aim_fits.json"),
    # ticket 0395, the live0395 step: the table of render/evidence/0395/table_0395.py, the fits and bones of the
    # captures, and the FADE blend fit ("" = none)
    "table0395": "F:/c5-64bit-work/0395/tables/table_0395.csv",
    "fits0395": "F:/c5-64bit-work/0395/fits",
    "bones0395": "F:/c5-64bit-work/0395/bones",
    "fade0395": "",
}
WORK_ROOTS = ("c:/c5-64bit-work/", "f:/c5-64bit-work/")
T_TOL = 0.005
W_TOL = 0.02
Z999 = 3.2905
OMNI = "1_omni_state_01.fsmxml"


class StepFailed(Exception):
    pass


# ------------------------------------------------------------------------------------------------ the driver
class Driver:
    def __init__(self, exe, out):
        self.exe, self.out, self.runs, self.seconds = exe, out, 0, 0.0

    def run(self, name, scene_text, records, frames, seed=1, forced=True, extra=()):
        """records: list of 'ev' lines (without 'ev\t') per frame index -> frames: list of (seq, dt, present).
        Returns (exit, programs rows, status rows, trigger rows)."""
        d = os.path.join(self.out, "runs", CLOCK[0], name)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "scene.c5units"), "w", encoding="utf-8", newline="\n") as f:
            f.write(scene_text)
        lines = ["c5anim\t1", "scene\tscene.c5units\t%d\t%s" % (seed, "forced" if forced else "free")]
        for i, (seq, dt, present) in enumerate(frames):
            for r in records.get(i, ()):
                lines.append("ev\t" + r)
            lines.append("frame\t%d\t%.17g\t%s\t-" % (seq, dt, "-" if present is None else "%.9g" % present))
        tl = os.path.join(d, "t.c5anim")
        with open(tl, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        res = os.path.join(d, "result")
        cmd = [self.exe, "play", "--timeline", tl, "--game", ARGS.game, "--mods", MODS_LIST, "--db", ARGS.db,
               "--out", res, "--no-c5units", "--clock", CLOCK[0]] + list(extra)
        t0 = time.time()
        p = subprocess.run(cmd, capture_output=True, text=True)
        self.seconds += time.time() - t0
        self.runs += 1
        if p.returncode not in (0, 1):
            raise StepFailed("%s: driver exit %d: %s" % (name, p.returncode, (p.stderr or p.stdout).strip()[:500]))
        return p.returncode, read_csv(os.path.join(res, "programs.csv")), read_csv(os.path.join(res, "status.csv"))


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def programs(rows):
    """(frame, player, id, figure) -> (state, [nodes])"""
    out = collections.OrderedDict()
    for r in rows:
        k = (int(r["frame"]), int(r["player"]), int(r["id"]), int(r["figure"]))
        out.setdefault(k, [int(r["state"]), []])[1].append(r)
    return out


def share(nodes, pred):
    """The share of the pose of the sources for which pred(node) holds, through the blend tree."""
    v = []
    for n in nodes:
        if n["kind"] == "sample":
            v.append(1.0 if pred(n) else 0.0)
        else:
            a, b, w = int(n["a"]), int(n["b"]), float(n["w"])
            if n["mask"] not in ("-", ""):
                v.append(v[a])   # a masked blend layers b on the mask's bones only: the pose stays a's (aim layer)
            else:
                v.append((1 - w) * v[a] + w * v[b])
    return v[-1] if v else 0.0


def main_sample(nodes):
    """The sample with the largest share (clip, ec, t, share)."""
    best = None
    for i, n in enumerate(nodes):
        if n["kind"] != "sample":
            continue
        s = share(nodes, lambda m, i=i: m is nodes[i])
        if best is None or s > best[3]:
            best = (n["clip"], int(n["ec"]), float(n["t"]), s)
    return best


MISSING = re.compile(r"((?:option|roll):[^ ]+:\d+) \(figure (\d+)\): no choice record")


def statuses(rows, frame=None):
    out = {}
    for r in rows:
        if frame is None or int(r["frame"]) == frame:
            out[(int(r["player"]), int(r["id"]))] = (r["status"], r["text"])
    return out


# ------------------------------------------------------------------------------------------------ scene files
def parse_scene(text):
    """(head lines, players, units: list of dict(fields, figures: list of (figure, clip, t)))"""
    head, units = [], []
    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "unit":
            units.append({"f": f, "player": int(f[1]), "id": int(f[2]), "type": f[3], "art": f[4],
                          "flags": int(f[15]), "figures": []})
        elif f[0] == "figure":
            units[-1]["figures"].append((int(f[1]), f[2], float(f[3])))
        elif f[0] in ("c5units", "scene", "player"):
            head.append(line)
    return head, units


def scene_text(head, units, with_figures=True):
    out = list(head)
    for u in units:
        out.append("\t".join(u["f"]))
        if with_figures:
            for fg, clip, t in u["figures"]:
                out.append("figure\t%d\t%s\t%.9g" % (fg, clip, t))
    return "\n".join(out) + "\n"


def discover(drv, name, text, units, records0=None, frames=None, rounds=5):
    """The forced-choice points a run needs, per unit: {(p, id): [point, ...]} (value 0 for each), found by running
    in forced mode and reading the ForcedChoiceMissing statuses."""
    pts = collections.defaultdict(list)
    frames = frames or [(1, 0.0, None)]
    for k in range(rounds):
        recs = collections.defaultdict(list)
        for i, rs in (records0 or {}).items():
            recs[i].extend(rs)
        for (p, i), lst in pts.items():
            for pt in lst:
                recs[0].append("%d\t%d\tchoice\t*\t%s\t0" % (p, i, pt))
        rc, prog, st = drv.run("%s_d%d" % (name, k), text, recs, frames)
        new = False
        for key, (code, txt) in statuses(st).items():
            if code == "ForcedChoiceMissing":
                m = MISSING.search(txt)
                if m and m.group(1) not in pts[key]:
                    pts[key].append(m.group(1))
                    new = True
        if not new:
            return dict(pts), prog, st
    return dict(pts), prog, st


# ------------------------------------------------------------------------------------------------ 4408's parse
class Census:
    def __init__(self, path, db):
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        self.fx = {k.lower(): v for k, v in d["fx"].items()}
        self.fsm = {k.lower(): v for k, v in d["fsm"].items()}
        self.dge = {k.lower(): v for k, v in d["dge"].items()}
        c = sqlite3.connect("file:%s?immutable=1" % db, uri=True)
        self.members = collections.defaultdict(list)
        for row, mem, n, model in c.execute(
                "select m.UnitInfoType, m.UnitMemberInfoType, m.NumMembers, i.Model from ArtDefine_UnitInfoMemberInfos m "
                "join ArtDefine_UnitMemberInfos i on i.Type = m.UnitMemberInfoType order by m.rowid"):
            self.members[row].append((mem, int(n), os.path.basename((model or "").replace("\\", "/")).lower()))
        self.unit_art = dict(c.execute("select Type, UnitArtInfo from Units"))

    def reachable_ecs(self, fxs, fortified):
        """The ec codes of the graphs of the states the member's machine reaches from its default state with path 4
        held at 1000 (1500 fortified) and only its own writes changing it."""
        fx = self.fx.get(fxs)
        if not fx or not fx.get("sm"):
            return None
        m = self.fsm.get(fx["sm"][0].lower())
        if not m:
            return None
        states = {s["id"]: s for s in m["states"]}
        out = collections.defaultdict(list)
        for s in m["sockets"]:
            out[s["from"]].append(s)
        start = (str(m["default"]), 1500 if fortified else 1000)
        seen, todo, ecs = {start}, [start], set()
        while todo:
            st, p4 = todo.pop()
            a = states.get(st, {}).get("asset", "")
            if a:
                g = self.dge.get(os.path.basename(a).lower())
                if g:
                    ecs.update(int(x) for x in g["dags"][0]["sources"])
            for s in out.get(st, ()):
                groups, cur = [], []
                for c in s["conds"]:
                    if c["t"] == "OrOperator":
                        groups.append(cur)
                        cur = []
                    else:
                        cur.append(c)
                groups.append(cur)
                for g in groups:
                    ok, np4 = True, p4
                    for c in g:
                        if c["t"] == "CheckPathOption" and c.get("path") == "4":
                            eq = float(c["value"]) == p4
                            ok = ok and (eq if c.get("method") == "1" else not eq)
                    if not ok:
                        continue
                    for c in g:
                        if c["t"] == "EventSetPathOption" and c.get("path") == "4":
                            v = float(c["value"])
                            np4 = p4 if v == 0 else v   # S4=0 consumes a request; at rest the level stays
                    k = (s["too"] if "too" in s else s["to"], np4)
                    if k not in seen:
                        seen.add(k)
                        todo.append(k)
        return ecs

    def clip_ec(self, fxs, clip):
        fx = self.fx.get(fxs)
        if not fx:
            return None
        for name, ecs in fx["anims"]:
            if os.path.basename(name).lower() == clip.lower():
                return ecs
        return None


# ------------------------------------------------------------------------------------------------ C: catalogue
def step_catalogue(drv, census, out):
    rows, summary = [], collections.Counter()
    for fn in sorted(os.listdir(ARGS.scenes)):
        if not fn.endswith(".c5units"):
            continue
        name = fn[:-8]
        with open(os.path.join(ARGS.scenes, fn), encoding="utf-8") as f:
            head, units = parse_scene(f.read())
        text = scene_text(head, units)
        pts, prog0, st0 = discover(drv, "cat_" + name, text, units)
        # the variant point: each unit's first point, tried at 0 and 1
        by_value = {}
        for v in (0, 1):
            recs = collections.defaultdict(list)
            for (p, i), lst in pts.items():
                for j, pt in enumerate(lst):
                    recs[0].append("%d\t%d\tchoice\t*\t%s\t%d" % (p, i, pt, v if j == 0 else 0))
            rc, prog, st = drv.run("cat_%s_v%d" % (name, v), text, recs, [(1, 0.0, None)])
            by_value[v] = (programs(prog), statuses(st))
        # per figure: the value whose clip is the fitted one
        recs = collections.defaultdict(list)
        choice = {}
        for u in units:
            key = (u["player"], u["id"])
            lst = pts.get(key, [])
            for j, pt in enumerate(lst[1:]):
                recs[0].append("%d\t%d\tchoice\t*\t%s\t0" % (key[0], key[1], pt))
            for fg, clip, t in u["figures"]:
                v_ok = None
                for v in (0, 1):
                    n = by_value[v][0].get((0, key[0], key[1], fg))
                    if n and main_sample(n[1])[0].lower() == clip.lower():
                        v_ok = v
                        break
                choice[(key, fg)] = v_ok
            if lst:
                figs = sorted({k[3] for k in by_value[0][0] if k[1:3] == key} | {fg for fg, _, _ in u["figures"]})
                for fg in figs:
                    v = choice.get((key, fg))
                    recs[0].append("%d\t%d\tchoice\t%d\t%s\t%d" % (key[0], key[1], fg, lst[0], v or 0))
        rc, prog, st = drv.run("cat_%s_final" % name, text, recs, [(1, 0.0, None)])
        P, S = programs(prog), statuses(st)
        for u in units:
            key = (u["player"], u["id"])
            code, txt = S.get(key, ("Missing", ""))
            members = census.members.get(u["art"], [])
            for fg, clip, t in u["figures"]:
                row = {"scene": name, "player": key[0], "id": key[1], "unit_type": u["type"], "art_row": u["art"],
                       "figure": fg, "fit_clip": clip, "fit_t": "%.5f" % t, "status": code, "ctrl_clip": "",
                       "ctrl_t": "", "dt": "", "result": "", "cause": ""}
                n = P.get((0, key[0], key[1], fg))
                if code != "Ok" or not n:
                    row["result"] = "not_covered"
                    row["cause"] = "unit status %s: %s" % (code, txt[:160])
                    if "TrackMask" in txt or "mask" in txt.lower():
                        row["cause"] = "aim member (track mask bones, 4410): " + txt[:120]
                else:
                    nodes = n[1]
                    row["ctrl_clip"] = nodes[-1]["clip"] if len(nodes) == 1 else "program of %d nodes" % len(nodes)
                    if len(nodes) == 1 and nodes[0]["clip"].lower() == clip.lower():
                        d = float(nodes[0]["t"]) - t
                        row["ctrl_t"] = nodes[0]["t"]
                        row["dt"] = "%.6f" % d
                        row["result"] = "pass" if abs(d) <= T_TOL else "fail_t"
                    elif len(nodes) > 1 and main_sample(nodes)[0].lower() == clip.lower():
                        # a blended program (the aim layer of 4410's members): its main sample against the fit
                        c = main_sample(nodes)
                        d = c[2] - t
                        row["ctrl_clip"], row["ctrl_t"], row["dt"] = c[0], "%.5f" % c[2], "%.6f" % d
                        row["result"] = "pass_main" if abs(d) <= T_TOL else "fail_t"
                        row["cause"] = "a program of %d nodes: its main sample (share %.3f) compared" % (len(nodes), c[3])
                    else:
                        # which member plays this figure, and can its default state reach the fitted clip?
                        fxs, k = None, 0
                        for mem, cnt, model in members:
                            if k <= fg < k + cnt:
                                fxs = model
                            k += cnt
                        fortified = bool(u["flags"] & 4)   # kFlagFortified
                        ecs = census.reachable_ecs(fxs, fortified) if fxs else None
                        ce = census.clip_ec(fxs, clip) if fxs else None
                        if ecs is None or ce is None:
                            row["result"], row["cause"] = "not_covered", "member or clip not in 4408's parse"
                        elif set(ce) & ecs:
                            row["result"] = "not_covered"
                            row["cause"] = ("fitted clip (ec %s) reachable from the default state but not its first "
                                            "graph: needs the figure's history (fidget, transition)" %
                                            "/".join(map(str, ce)))
                        else:
                            row["result"] = "fail_unreachable"
                            row["cause"] = "fitted clip (ec %s) not reachable from the default state (flags %d)" % (
                                "/".join(map(str, ce)), u["flags"])
                rows.append(row)
                summary[row["result"]] += 1
        unit_codes = collections.Counter(c for c, _ in S.values())
        for c, n in unit_codes.items():
            summary["units_" + c] += n
    write_table(os.path.join(out, "catalogue.csv"), rows)
    ok = [abs(float(r["dt"])) for r in rows if r["result"] in ("pass", "pass_main", "fail_t")]
    s = dict(summary)
    s["figures"] = len(rows)
    if ok:
        s["t_abs_max"] = max(ok)
    causes = collections.Counter(r["cause"].split(":")[0] for r in rows if r["result"] != "pass")
    s["causes"] = dict(causes.most_common(20))
    return s


def write_table(path, rows):
    if not rows:
        open(path, "w").close()
        return
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


# ------------------------------------------------------------------------------------------------ C: sequences
SEQ_UNITS = {"warrior": ("UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR"), "warrior_v2": ("UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR"),
             "warrior_v4": ("UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR"), "horseman": ("UNIT_HORSEMAN", "ART_DEF_UNIT_HORSEMAN"),
             # 4406 r3: 4405 B's sequences
             "barbarian_euro_alpha": ("UNIT_BARBARIAN_WARRIOR", "ART_DEF_UNIT_BARBARIAN_EURO"),
             "barbarian_euro_bravo": ("UNIT_BARBARIAN_WARRIOR", "ART_DEF_UNIT_BARBARIAN_EURO"),
             "barbarian_euro_charlie": ("UNIT_BARBARIAN_WARRIOR", "ART_DEF_UNIT_BARBARIAN_EURO"),
             "archer": ("UNIT_ARCHER", "ART_DEF_UNIT_ARCHER"), "archer_v2": ("UNIT_ARCHER", "ART_DEF_UNIT_ARCHER"),
             "archer_v3": ("UNIT_ARCHER", "ART_DEF_UNIT_ARCHER")}
HEAD = ["c5units\t1", "scene\town\t10.1167316", "player\t0\tPLAYERCOLOR_POLYNESIA\tCIVILIZATION_POLYNESIA\tERA_ANCIENT"]


def unit_line(uid, utype, art, x=38, y=13, flags=0):
    return ["unit", "0", str(uid), utype, art, str(x), str(y), "-", "-", "-", "2", "20", "0", "100", "-", str(flags),
            "1", "0", "0"]


def load_sequences():
    """{seq: {"frames": [(frame, present_s)], "figs": {fig: {frame: (model, clip, ec, t, rot)}}}}"""
    out = {}
    q = read_csv(ARGS.q1)
    s = out.setdefault("Q1", {"frames": {}, "figs": collections.defaultdict(dict)})
    for r in q:
        fr = int(r["frame"])
        s["frames"][fr] = float(r["present_us"]) / 1e6
        if r["mesh"] == "HorsemanHorse":
            continue   # the horse shares its rider's clip and t (8.6); the rider stands for the figure
        s["figs"]["Q1/" + r["figure"]][fr] = (r["model"], r["clip"], int(r["ec"]), float(r["t"]),
                                              float(r["rot_max_deg"]))
    for r in read_csv(ARGS.table4407):
        if r["seq"] != "P01" or r["mesh"] == "HorsemanHorse":
            continue
        s = out.setdefault("P01", {"frames": {}, "figs": collections.defaultdict(dict)})
        fr = int(r["frame"])
        s["frames"][fr] = float(r["present_s"])
        s["figs"][r["fig"]][fr] = (r["model"], r["clip"], int(r["ec"]) if r["ec"] else -1, float(r["t"]),
                                   float(r["rot_max_deg"]))
    # 4406 r3: 4405 B's multi-frame sequences (table_4405.csv; Q1 is read from its own file above, the singles SR30,
    # SR60 and SA are one capture per process and have no frame-to-frame clock). One followed figure-mesh per key; the
    # horse mesh shares its rider's clip and t (8.6).
    if ARGS.table4405 and os.path.exists(ARGS.table4405):
        for r in read_csv(ARGS.table4405):
            if r["seq"] in ("Q1", "SR30", "SR60", "SA") or r["mesh"] == "HorsemanHorse":
                continue
            s = out.setdefault(r["seq"], {"frames": {}, "figs": collections.defaultdict(dict)})
            fr = int(r["frame"])
            s["frames"][fr] = float(r["present_us"]) / 1e6
            s["figs"]["%s/%s" % (r["seq"], r["figure"])][fr] = (r["model"], r["clip"], int(r["ec"]) if r["ec"] else -1,
                                                                float(r["t"]), float(r["rot_max_deg"]))
    for s in out.values():
        s["frames"] = sorted(s["frames"].items())
    return out


def member_figure(census, art, model):
    k = 0
    for mem, cnt, fxs in census.members.get(art, []):
        if os.path.splitext(fxs)[0] == model.lower():
            return k
        k += cnt
    return None


def seq_frames(frames):
    """Captured frames two presented frames apart: the start frame (dt 0), then per pair two frames of half the
    Present step each. Returns (frames list, index of each captured frame)."""
    fl, at = [(frames[0][0], 0.0, frames[0][1])], {frames[0][0]: 0}
    for (fa, pa), (fb, pb) in zip(frames, frames[1:]):
        step = (pb - pa) / max(1, fb - fa)
        for k in range(fa + 1, fb + 1):
            fl.append((k, step, pa + step * (k - fa)))
        at[fb] = len(fl) - 1
    return fl, at


def step_sequences(drv, census, out):
    rows, summ = [], {}
    seqs = load_sequences()
    for name, s in seqs.items():
        frames = s["frames"]
        f0 = frames[0][0]
        fl, at = seq_frames(frames)
        units, plan = [], []
        for fig, byf in sorted(s["figs"].items()):
            if f0 not in byf:
                continue
            model, clip, ec, t, rot = byf[f0]
            utype, art = SEQ_UNITS[model]
            uid = 100 + len(units)
            mf = member_figure(census, art, model)
            if ec not in (1000, 2000) or mf is None:
                for fr, _ in frames:
                    if fr in byf:
                        rows.append({"seq": name, "fig": fig, "frame": fr, "obs_clip": byf[fr][1],
                                     "obs_ec": byf[fr][2], "obs_t": "%.5f" % byf[fr][3], "ctrl_ec": "", "ctrl_t": "",
                                     "dt": "", "result": "not_covered",
                                     "cause": "starts in ec %d (not an idle loop): the scene starts the default "
                                              "state only" % ec})
                continue
            u = {"f": unit_line(uid, utype, art), "player": 0, "id": uid, "figures": [], "fig": fig, "mf": mf,
                 "ec": ec}
            n_fig = sum(c for _, c, _ in census.members[art])
            u["figures"] = [(k, clip, t) for k in range(n_fig)]
            units.append(u)
        text = scene_text(HEAD, units)
        pts, _, _ = discover(drv, "seq_%s" % name, text, units)
        # the variant value giving the recorded ec, per unit (all figures alike)
        ecv = {}
        for v in (0, 1):
            recs = {0: ["0\t%d\tchoice\t*\t%s\t%d" % (u["id"], pt, v if j == 0 else 0)
                        for u in units for j, pt in enumerate(pts.get((0, u["id"]), []))]}
            _, prog, _ = drv.run("seq_%s_v%d" % (name, v), text, recs, [(1, 0.0, None)])
            P = programs(prog)
            for u in units:
                n = P.get((0, 0, u["id"], u["mf"]))
                if n and int(n[1][0]["ec"]) == u["ec"] and u["id"] not in ecv:
                    ecv[u["id"]] = v
        recs = {0: ["0\t%d\tchoice\t*\t%s\t%d" % (u["id"], pt, ecv.get(u["id"], 0) if j == 0 else 0)
                    for u in units for j, pt in enumerate(pts.get((0, u["id"]), []))]}
        _, prog, st = drv.run("seq_%s_play" % name, text, recs, fl)
        P = programs(prog)
        skips = collections.Counter()
        for u in units:
            byf = s["figs"][u["fig"]]
            drift = 0.0
            for fr, _ in frames:
                if fr not in byf:
                    continue
                model, clip, ec, t, rot = byf[fr]
                n = P.get((at[fr], 0, u["id"], u["mf"]))
                row = {"seq": name, "fig": u["fig"], "frame": fr, "obs_clip": clip, "obs_ec": ec, "obs_t": "%.5f" % t,
                       "ctrl_ec": "", "ctrl_t": "", "dt": "", "result": "", "cause": ""}
                if not n:
                    row["result"], row["cause"] = "not_covered", "no program (unit status)"
                else:
                    c = main_sample(n[1])
                    row["ctrl_ec"], row["ctrl_t"] = c[1], "%.5f" % c[2]
                    if ec not in (1000, 2000) and ec != c[1]:
                        row["result"] = "not_covered"
                        row["cause"] = ("the figure changed state (ec %d): an action, move or random decision not in "
                                        "the timeline" % ec)
                    elif c[1] != ec:
                        row["result"], row["cause"] = "fail_clip", ""
                    else:
                        d = c[2] - t
                        dur = 6.0
                        d = (d + dur / 2) % dur - dur / 2
                        row["dt"] = "%.6f" % d
                        if abs(d - drift) <= T_TOL:
                            row["result"] = "pass" if drift == 0 else "pass_after_skip"
                        else:
                            k = round((d - drift) / (1 / 30.0))
                            if abs((d - drift) - k / 30.0) <= T_TOL and k > 0:
                                skips[u["fig"]] += k
                                drift = d
                                row["result"] = "k6_skip"
                                row["cause"] = "the figure lost %d step(s) of 1/30 s here (K6, reported, not absorbed)" % k
                            else:
                                row["result"] = "fail_t"
                rows.append(row)
        c = collections.Counter(r["result"] for r in rows if r["seq"] == name)
        summ[name] = {"figures": len({r["fig"] for r in rows if r["seq"] == name}), "rows": dict(c),
                      "k6_skips_per_figure": dict(skips),
                      "frames": [f for f, _ in frames]}
    write_table(os.path.join(out, "sequences.csv"), rows)
    summ["p01_4681"] = score_p01(drv, census, seqs.get("P01"))
    return summ


def score_p01(drv, census, s):
    """P01 frame 4681: the warrior figure that goes idle A -> fidget A. Played from 4677 with its recorded t, the
    lost step at 4679 and the fidget decided on 4680 (both as K6 data: --skip), scored with and without the former
    X1 rule."""
    if not s:
        return {"skipped": "no P01 table"}
    try:
        with open(ARGS.blends4407, encoding="utf-8") as f:
            bl = [b for b in json.load(f) if b["seq"] == "P01" and b["frame"] == 4681]
    except OSError:
        bl = []
    fig = None
    for k, byf in s["figs"].items():
        if 4677 in byf and 4683 in byf and byf[4683][2] == 1040 and byf[4677][2] == 1000:
            fig = k
    if not fig:
        return {"skipped": "no figure idle A at 4677 and fidget A at 4683"}
    model, clip, ec, t0, _ = s["figs"][fig][4677]
    utype, art = SEQ_UNITS[model]
    mf = member_figure(census, art, model)
    n_fig = sum(c for _, c, _ in census.members[art])
    u = {"f": unit_line(7, utype, art), "player": 0, "id": 7, "figures": [(k, clip, t0) for k in range(n_fig)]}
    text = scene_text(HEAD, [u])
    pts, _, _ = discover(drv, "p01_fig", text, [u])
    frames = s["frames"]
    fl, at = seq_frames([(f, p) for f, p in frames if f <= 4683])
    base = ["0\t7\tchoice\t*\t%s\t%d" % (pt, 0) for pt in pts.get((0, 7), [])]
    roll = ["0\t7\tchoice\t%d\troll:%s:3\t1" % (mf, OMNI), "0\t7\tchoice\t%d\toption:%s:3\t1" % (mf, OMNI)]
    if CLOCK[0] == "steps":
        # the lost 0.2 s on 4679 (6 steps); the fidget decided on 4680's first step and entered on its second, the
        # rest of 4680 held (4 steps), so that the fidget is at 0.2 s on 4681
        recs = {0: base, at[4679] + 1: roll}
        skip = ["--skip", "4679:0:7:%d:6" % mf, "--skip", "4680:0:7:%d:4" % mf]
        given = "4679: 6 steps, 4680: 4 steps"
    else:
        # one update per frame: the lost 0.2 s on 4678, the fidget decided on 4679 and entered on 4680 (t 0)
        recs = {0: base, at[4679]: roll}
        skip = ["--skip", "4678:0:7:%d:1" % mf]
        given = "4678: 1 update (0.2 s)"
    res = {"figure": fig, "member_figure": mf, "t_4677": t0, "k6_skips_given": given,
           "recorded": {"t2": bl[0]["t2"] if bl else None, "w": bl[0]["w"] if bl else None,
                        "t1_held": bl[0]["t1"] if bl else None}}
    # 4406 r2: the default holds the From-side ContinueOffset source at its entry time (GraphOptions, MC #2117);
    # run_from is the graphs as read (--run-from), a comparison line
    for label, extra in (("no_exe_fade", []), ("run_from", ["--run-from"]),
                         ("former_x1_rule", ["--fade", "%s:6:3:0.3" % OMNI])):
        _, prog, st = drv.run("p01_4681_" + label, text, recs, fl, extra=skip + extra)
        P = programs(prog)
        per = {}
        for fr in (4679, 4681, 4683):
            n = P.get((at[fr], 0, 7, mf))
            if not n:
                per[fr] = {"status": statuses(st).get((0, 7))}
                continue
            nodes = n[1]
            fid = [x for x in nodes if x["kind"] == "sample" and int(x["ec"]) == 1040]
            old = [x for x in nodes if x["kind"] == "sample" and int(x["ec"]) == 1000]
            per[fr] = {"state": n[0], "nodes": len(nodes), "idle_t": [float(x["t"]) for x in old],
                       "fidget_share": round(share(nodes, lambda m: m["kind"] == "sample" and int(m["ec"]) == 1040), 4),
                       "fidget_t": float(fid[-1]["t"]) if fid else None,
                       "main": main_sample(nodes)[:3]}
        w = per.get(4681, {}).get("fidget_share")
        per["w_minus_recorded"] = round(w - res["recorded"]["w"], 4) if (w is not None and res["recorded"]["w"]) else None
        per["within_0.02"] = per["w_minus_recorded"] is not None and abs(per["w_minus_recorded"]) <= W_TOL
        res[label] = per
    return res


# ------------------------------------------------------------------------------------------------ E: palettes
MECH = ("UNIT_MECH", "ART_DEF_UNIT_MECH", "mechanized_aim_state_01.fsmxml")


def _program_at(d, frame, player, uid, fig):
    """(state, nodes, masks) of a figure at frame index `frame` of a driver run's result dir, or None."""
    P = programs(read_csv(os.path.join(d, "programs.csv")))
    n = P.get((frame, player, uid, fig))
    if not n:
        return None
    mp = os.path.join(d, "masks.csv")
    ms = [m for m in read_csv(mp) if (int(m["frame"]), int(m["player"]), int(m["id"]), int(m["figure"])) ==
          (frame, player, uid, fig)] if os.path.exists(mp) else []
    return n[0], n[1], ms


def mech_on_idle_b(drv, t):
    """(scene text, frame-0 choice records) of a mech created on idle B at clip time t (None records when no idle
    variant plays idle B). The run of the chosen variant is pal_mech_v<k> (frame 0 only)."""
    utype, art, _ = MECH
    u = {"f": unit_line(7, utype, art), "player": 0, "id": 7, "figures": [(0, "Mech_IdleB.gr2", t)]}
    text = scene_text(HEAD, [u])
    pts, _, _ = discover(drv, "pal_mech_disc", text, [u])
    plist = pts.get((0, 7), [])
    for v in (0, 1):   # the idle variant (R0) whose program plays idle B
        recs = ["0\t7\tchoice\t*\t%s\t%d" % (pt, v if pt.startswith("option:") else 0) for pt in plist]
        _, prog, _ = drv.run("pal_mech_v%d" % v, text, {0: recs}, [(1, 0.0, None)])
        n = programs(prog).get((0, 0, 7, 0))
        if n and main_sample(n[1]) and main_sample(n[1])[1] == 2000:
            return text, recs
    return text, None


def mech_idle_program(drv, t, name):
    """The controller's frame-0 program of a mech created on idle B at t (its idle state: idle B under the aim layer)."""
    text, base = mech_on_idle_b(drv, t)
    if base is None:
        return None, "no idle variant plays idle B"
    drv.run(name, text, {0: base}, [(1, 0.0, None)])
    return _program_at(os.path.join(drv.out, "runs", CLOCK[0], name, "result"), 0, 0, 7, 0), None


def mech_program(drv, census, b):
    """The controller's program for a 4353 mech block fitted as idle B -> fidget B (blends_a): the mech created on
    idle B at the fit's t1 (the block's free start: with the From side held, t1 is the time the fidget was entered
    at), the fidget forced on the first update (1e-6 s frames), then the fidget played to the fit's t2."""
    utype, art, machine = MECH
    text, base = mech_on_idle_b(drv, b["t1"])
    if base is None:
        return None, "no idle variant plays idle B"
    fl = [(1, 0.0, None), (2, 1e-6, None), (3, 1e-6, None)]
    rest, k = b["t2"], 4
    while rest > 1e-9:
        step = min(0.2, rest)
        fl.append((k, step, None))
        rest -= step
        k += 1
    roll = ["0\t7\tchoice\t0\troll:%s:3\t1" % machine, "0\t7\tchoice\t0\toption:%s:3\t1" % machine]
    name = "pal_mech_%d_%d" % (b["frame"], b["id"])
    drv.run(name, text, {0: base, 1: roll}, fl)
    got = _program_at(os.path.join(drv.out, "runs", CLOCK[0], name, "result"), len(fl) - 1, 0, 7, 0)
    return got, None


def step_palettes(drv, census, out):
    """E for blended programs: P01 4681 (each P01 reading) and the 4353 mech blocks, through oracle_palettes."""
    sys.path.insert(0, HERE)
    import oracle_palettes as op
    with open(ARGS.blends4407, encoding="utf-8") as f:
        fits = json.load(f)
    blocks, notes = [], {}

    def add(key, b, prog):
        blocks.append({"key": key, "bones": b["bones"], "id": b["id"], "mesh": b["mesh"], "model": b["model"],
                       "program": prog})

    seqs = load_sequences()
    p01 = score_p01(drv, census, seqs.get("P01"))
    fitp = next((x for x in fits if x["seq"] == "P01" and x["frame"] == 4681), None)
    if "figure" in p01 and fitp:
        _, at = seq_frames([(f, p) for f, p in seqs["P01"]["frames"] if f <= 4683])
        for label in ("no_exe_fade", "run_from", "former_x1_rule"):
            got = _program_at(os.path.join(drv.out, "runs", CLOCK[0], "p01_4681_" + label, "result"), at[4681], 0, 7,
                              p01["member_figure"])
            if got:
                add("P01_4681_" + label, fitp, op.program_text(got[1], got[2]))
                notes["P01_4681_" + label] = {"state": got[0], "program": op.program_text(got[1], got[2])}
    for b in fits:
        clips = {"warrior_idlea.gr2": "Warrior_IdleA.gr2", "warrior_fidgeta.gr2": "Warrior_FidgetA.gr2",
                 "mech_idleb.gr2": "Mech_IdleB.gr2", "mech_fidgetb.gr2": "Mech_FidgetB.gr2"}
        ref = "S|%s|%r;S|%s|%r;B|0|1|%r" % (clips.get(b["a"], b["a"]), b["t1"], clips.get(b["b"], b["b"]), b["t2"],
                                            b["w"])
        key = "%s_%d_%d" % (b["seq"], b["frame"], b["id"])
        add(key + "_fit4407", b, ref)
        if b["seq"] == "U4353":
            got, why = mech_program(drv, census, b)
            if got:
                add(key + "_controller", b, op.program_text(got[1], got[2]))
                notes[key + "_controller"] = {"state": got[0], "program": op.program_text(got[1], got[2])}
            else:
                notes[key + "_controller"] = {"skipped": why or "no program"}
    # the mech blocks read as idle B under the aim layer (15.8): the controller's idle program at the start time a
    # scan of that program found (r2/mech_idleb_aim_fits.json; the block's free start, as the catalogue's figure t)
    if ARGS.mechfits and os.path.exists(ARGS.mechfits):
        with open(ARGS.mechfits, encoding="utf-8") as f:
            for b in json.load(f)["blocks"]:
                key = "%s_%d_%d_controller_idle" % (b["seq"], b["frame"], b["id"])
                got, why = mech_idle_program(drv, b["t"], "pal_mech_idle_%d_%d" % (b["frame"], b["id"]))
                if got:
                    add(key, dict(b, bones=json.load(open(ARGS.mechfits, encoding="utf-8"))["bones"]),
                        op.program_text(got[1], got[2]))
                    notes[key] = {"state": got[0], "program": op.program_text(got[1], got[2])}
                else:
                    notes[key] = {"skipped": why or "no program"}
    exe_dir = os.path.dirname(os.path.dirname(os.path.abspath(ARGS.exe)))
    unitscene = ARGS.unitscene or os.path.join(exe_dir, "unitscene", "unitscene.exe")
    try:
        res = op.compare(blocks, unitscene, ARGS.units_dir, os.path.join(out, "palettes"))
    except (RuntimeError, OSError) as e:
        raise StepFailed("palettes: %s" % e)
    for k, v in notes.items():
        res.setdefault(k, {}).update(v)
    rows = [dict({"key": k}, **{kk: vv for kk, vv in v.items() if kk != "program"}) for k, v in sorted(res.items())]
    cols = []
    for r in rows:
        cols += [c for c in r if c not in cols]
    rows = [{c: r.get(c, "") for c in cols} for r in rows]
    write_table(os.path.join(out, "palettes.csv"), rows)
    return {"tolerances": {"rot_elem": op.ROT_ELEM_TOL, "trans": op.TRANS_TOL}, "blocks": res}


# ------------------------------------------------------------------------------------------------ D: transitions
def step_transitions(drv, census, out):
    art = "ART_DEF_UNIT__WARRIOR"
    n_fig = sum(c for _, c, _ in census.members[art])
    u = {"f": unit_line(1, "UNIT_WARRIOR", art), "player": 0, "id": 1, "figures": []}
    text = scene_text(HEAD, [u])
    h = 1.0 / 30.0 + 1e-9
    kinds = {
        "idle_to_fidget": {3: ["0\t1\tchoice\t*\troll:%s:3\t1" % OMNI, "0\t1\tchoice\t*\toption:%s:3\t1" % OMNI]},
        "idle_to_run": {3: ["0\t1\tmove\t2\t2"]},
        "idle_to_attack": {3: ["0\t1\taction\tattack\t-\t*"]},
        "combat_ready_to_charge_run": {3: ["0\t1\taction\tattack\t-\t*"], 40: ["0\t1\taction\tcharge\t-\t*"]},
        # 4406 r3: run -> stop -> idle (8.10's clip-end rule and stop variant); _b forces every choice to 1
        "run_to_stop": {3: ["0\t1\tmove\t2\t2"], 30: ["0\t1\tarrive\t2"]},
        "run_to_stop_b": {3: ["0\t1\tmove\t2\t2"], 30: ["0\t1\tarrive\t2"]},
    }
    rows, summ = [], {}
    for kind, ev in kinds.items():
        frames = [(i + 1, 0.0 if i == 0 else h, None) for i in range(70)]
        pts, _, _ = discover(drv, "tr_" + kind, text, [u], records0=ev, frames=frames)
        recs = collections.defaultdict(list)
        for i, rs in ev.items():
            recs[i].extend(rs)
        for pt in pts.get((0, 1), []):
            recs[0].append("0\t1\tchoice\t*\t%s\t%d" % (pt, 1 if kind.endswith("_b") and pt.startswith("option:") else 0))
        _, prog, st = drv.run("tr_" + kind, text, recs, frames)
        P = programs(prog)
        prev = None
        first_new = None
        for fr in range(len(frames)):
            n = P.get((fr, 0, 1, 0))
            if not n:
                continue
            state, nodes = n
            c = main_sample(nodes)
            if prev is not None and state != prev and first_new is None and fr >= min(ev):
                first_new = fr
            prev = state
            if fr >= min(ev) - 1:
                rows.append({"kind": kind, "frame": fr, "state": state, "nodes": len(nodes), "main_ec": c[1],
                             "main_t": "%.5f" % c[2], "main_share": "%.4f" % c[3]})
        summ[kind] = {"status": statuses(st).get((0, 1)), "first_state_change_frame": first_new,
                      "states": [r["state"] for r in rows if r["kind"] == kind][:25]}
        if kind.startswith("run_to_stop"):   # 4406 r3: from the arrive on, (state, main ec, main t) per frame
            summ[kind]["after_arrive"] = [(r["state"], r["main_ec"], r["main_t"]) for r in rows
                                          if r["kind"] == kind and r["frame"] >= 29][:40]
    write_table(os.path.join(out, "transitions.csv"), rows)
    return summ


# ------------------------------------------------------------------------------------------------ 0395: live captures
# Ticket 0395 (research unit-controller.md 17): the captures of fortify on and off (FORT, FADE), the aim unit (AIM)
# and 4405 B's embark series (Q8) refitted to the embark model, from render/evidence/0395/table_0395.py's table, the
# live driver's run records and the FADE blend fit. Each row: what the capture shows, what the controller gives, the
# result with 0041's / section 10's tolerances (t 0.005 s, w 0.02, palettes 1e-3 / 0.01); a known open cause is named.
L95_WARRIOR = ("UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR")
L95_CANOE = ("UNIT_WARRIOR", "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE")
L95_SHIP = ("UNIT_BATTLESHIP", "ART_DEF_UNIT_BATTLESHIP")
H30 = 1.0 / 30.0


def l95_table():
    """{(seq, frame): {plot: [rows]}} of table_0395.csv (the horse mesh dropped: it shares its rider's clip and t)."""
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in read_csv(ARGS.table0395):
        if r["mesh"] != "HorsemanHorse":
            out[(r["seq"], int(r["frame"]))][r["plot"]].append(r)
    return out


def l95_mode(vals):
    c = collections.Counter(round(float(v), 4) for v in vals)
    return c.most_common(1)[0][0] if c else None


def l95_last(drv, name, text, recs, fl, uid, fig=0):
    """(state, nodes, masks) of figure `fig` of unit uid at the last frame of a free-mode run (seed 1)."""
    drv.run(name, text, recs, fl, forced=False)
    return _program_at(os.path.join(drv.out, "runs", CLOCK[0], name, "result"), len(fl) - 1, 0, uid, fig)


def l95_fortify(drv, name, tau, t0=1.0):
    """The controller's warrior given `fortify 1` with its figures on idle at t0: (ec, t, share of 1500, From t,
    state) of figure 0 `tau` s of the new clip after the event (the event's own frame advances the From side only)."""
    u = {"f": unit_line(1, *L95_WARRIOR), "player": 0, "id": 1, "figures": [(0, "Warrior_IdleA.gr2", t0)]}
    text = scene_text(HEAD, [u])
    n = max(1, int(round(tau / H30)))
    fl = [(1, 0.0, None), (2, H30, None)]
    k_ev = len(fl)
    fl.append((k_ev + 1, H30, None))
    for k in range(n):
        fl.append((len(fl) + 1, tau / n, None))
    got = l95_last(drv, name, text, {k_ev: ["0\t1\tfortify\t1"]}, fl, 1)
    if not got:
        return None
    state, nodes, _ = got
    c = main_sample(nodes)
    w = share(nodes, lambda m: int(m["ec"]) == 1500)
    frm = [float(m["t"]) for m in nodes if m["kind"] == "sample" and int(m["ec"]) != 1500]
    t_new = [float(m["t"]) for m in nodes if m["kind"] == "sample" and int(m["ec"]) == 1500]
    return {"ec": c[1], "t": t_new[0] if t_new else c[2], "w": w, "from_t": frm[0] if frm else None, "state": state,
            "from_t_at_event": t0 + 2 * H30}


def l95_row(part, unit, frame, **kw):
    r = {"part": part, "unit": unit, "frame": frame, "obs": "", "ctrl": "", "result": "", "cause": ""}
    r.update(kw)
    return r


def l95_fortify_rows(drv, tab, fade):
    """Fortify on: the ordered warrior's figures in 1500 at the captured t (FORT 690 W1, 783 W2; FADE W3)."""
    rows = []
    pts = [("FORT", 690, "42,12", "W1"), ("FORT", 783, "43,12", "W2")]
    for seq, fr, plot, name in pts:
        rs = tab.get((seq, fr), {}).get(plot, [])
        ecs = collections.Counter(r["ec"] for r in rs)
        if not rs or ecs.most_common(1)[0][0] != "1500":
            rows.append(l95_row("fortify_on", name, fr, result="not_covered", cause="no figure in 1500 at this plot"))
            continue
        tau = l95_mode(r["t"] for r in rs if r["ec"] == "1500")
        c = l95_fortify(drv, "l95_fort_%d" % fr, tau)
        ok = c and c["ec"] == 1500 and abs(c["t"] - tau) <= T_TOL and abs(c["w"] - 1.0) <= W_TOL
        rows.append(l95_row("fortify_on", name, fr, obs="12 figures: 1500 at t %.4f, single clip (w 1)" % tau,
                            ctrl=("ec %s t %.4f w %.4f state %s" % (c["ec"], c["t"], c["w"], c["state"]) if c else
                                  "no program"),
                            result="pass" if ok else "fail",
                            cause="" if ok else "controller's 1500 share or t differs"))
    for b in fade:   # FADE: the blend fit of W3's blocks (From idle, To fortify, w)
        c = l95_fortify(drv, "l95_fade_%d_%d" % (b["frame"], b["id"]), b["t2"])
        if not c:
            rows.append(l95_row("fade_w", "W3", b["frame"], result="fail", cause="no program"))
            continue
        okw = abs(c["w"] - b["w"]) <= W_TOL
        rows.append(l95_row("fade_w", "W3/%s/%d" % (b["mesh"], b["id"]), b["frame"],
                            obs="w %.4f on 1500 at t %.4f (From %s at %.4f)" % (b["w"], b["t2"], b["a"], b["t1"]),
                            ctrl="w %.4f at t %.4f" % (c["w"], c["t"]), result="pass" if okw else "fail_w",
                            cause="" if okw else "the type-3 ease at a second point (P01 4681 the first)"))
        if b.get("t_ref") is not None:
            held, run = (b["t_ref"] - b["t2"]) % 6.0, b["t_ref"] % 6.0
            d_h = (b["t1"] - held + 3.0) % 6.0 - 3.0
            d_r = (b["t1"] - run + 3.0) % 6.0 - 3.0
            res = ("pass_held" if abs(d_h) <= T_TOL else "pass_running" if abs(d_r) <= T_TOL else "fail_t1")
            rows.append(l95_row("fade_from", "W3/%s/%d" % (b["mesh"], b["id"]), b["frame"],
                                obs="From t1 %.4f; W4's idle clock %.4f" % (b["t1"], b["t_ref"]),
                                ctrl="held (hold_from_continue): t1 = %.4f; running would be %.4f" % (held, run),
                                result=res if res != "pass_running" else "fail_held",
                                cause="dt held %.4f, running %.4f" % (d_h, d_r)))
    return rows


def l95_clock_rows(tab):
    """K6 between FORT's captures, read from the figures' own clocks (no controller run): the bystander horseman's idle
    advance is the clock of every unit not given an order; a warrior whose clip ended between two captures must show
    that advance less the overflow dropped at the clip end (8.10), which lies in [0, one frame step]."""
    rows = []
    hs = {fr: l95_mode(r["t"] for r in tab[("FORT", fr)].get("44,12", [])) for fr in (690, 783, 951)}

    def adv(a, b, dur=6.0):
        return (hs[b] - hs[a]) % dur
    checks = [(690, 783, "W1", "42,12", 1.0, "1500 -> 1520"), (783, 951, "W2", "43,12", 1.0, "1500 -> 1520 (4.0 s loop)")]
    for a, b, name, plot, dur, what in checks:
        ta = l95_mode(r["t"] for r in tab[("FORT", a)].get(plot, []) if r["ec"] == "1500")
        tb = l95_mode(r["t"] for r in tab[("FORT", b)].get(plot, []) if r["ec"] == "1520")
        A = adv(a, b)
        # the clip time the warrior passed: the rest of 1500, then 1520 (a 4.0 s loop) up to tb
        passed = (dur - ta) + tb
        k = round((A - passed) / 4.0)
        over = A - passed - 4.0 * k
        ok = -T_TOL <= over <= H30 + T_TOL
        rows.append(l95_row("k6_clip_end", name, "%d->%d" % (a, b),
                            obs="bystander advance %.4f; %s: 1500 %.4f -> 1520 %.4f (+%d loops)" % (A, what, ta, tb, k),
                            ctrl="overflow dropped at the clip end %.4f (rule: 0..%.4f)" % (over, H30),
                            result="pass" if ok else "fail", cause=""))
    # W1 at 951 after the move order: the figures still in 1520 against W1's clock carried from 783
    w1_783 = l95_mode(r["t"] for r in tab[("FORT", 783)].get("42,12", []) if r["ec"] == "1520")
    exp = (w1_783 + adv(783, 951)) % 4.0
    for r in tab[("FORT", 951)].get("42,12", []):
        if r["ec"] == "1520":
            d = float(r["t"]) - exp
            rows.append(l95_row("k6_after_order", "W1/%s/%s" % (r["mesh"], r["id"]), 951,
                                obs="1520 at %s after the move order" % r["t"],
                                ctrl="the unit's clock carried from 783: %.4f" % exp,
                                result="pass" if abs(d) <= T_TOL else "skip_%.3f" % -d,
                                cause="" if abs(d) <= T_TOL else
                                "the figure lost %.3f s of clip time before the capture (open: 17)" % -d))
    return rows


def l95_move_rows(drv, tab):
    """Fortify off by a move (FORT 951, W1): the controller gives every figure run (with its 0.15 s fade) on the
    order; the EXE starts the run per figure (8.9), so figures still in 1520 are the staggered entry."""
    rows = []
    rs = tab.get(("FORT", 951), {}).get("42,12", [])
    u = {"f": unit_line(1, *L95_WARRIOR), "player": 0, "id": 1, "figures": []}
    text = scene_text(HEAD, [u])
    fl = [(i + 1, 0.0 if i == 0 else H30, None) for i in range(60)]
    got = l95_last(drv, "l95_fort_off", text, {3: ["0\t1\tfortify\t1"], 50: ["0\t1\tmove\t2\t1"]}, fl, 1)
    ctrl = "no program"
    if got:
        c = main_sample(got[1])
        ctrl = "every figure: ec %d t %.4f (state %s) %.3f s after the order" % (c[1], c[2], got[0], 9 * H30)
    for r in rs:
        if r["ec"] == "1400":
            rows.append(l95_row("fortify_off", "W1/%s/%s" % (r["mesh"], r["id"]), 951, obs="run 1400 at %s" % r["t"],
                                ctrl=ctrl, result="pass_ec", cause="t not scored: run entered at a per-figure phase (8.9)"))
        else:
            rows.append(l95_row("fortify_off", "W1/%s/%s" % (r["mesh"], r["id"]), 951,
                                obs="still %s at %s" % (r["ec"], r["t"]), ctrl=ctrl, result="not_covered",
                                cause="staggered run entry per figure (8.9; 0042's C1): the controller starts all at once"))
    return rows


def l95_embark_rows(drv, census, tab):
    """4405 B's Q8 refitted to the war canoe (the Polynesian embark override): the controller on the canoe art, move
    at frame 1 and arrive placed so that the figure seen in stop run at 820 has its t; then the recorded Present steps
    (two or more presented frames per captured pair, clamped by the clock), scored per canoe at 820-829."""
    rows = []
    q = {fr: tab.get(("Q8", fr), {}).get("33,11", []) for fr in (820, 823, 825, 829)}
    if not q[820]:
        return [l95_row("embark", "canoe", 820, result="not_covered", cause="no Q8 rows")]
    pres = {fr: float(q[fr][0]["present_us"]) / 1e6 for fr in q if q[fr]}
    stop = [r for r in q[820] if r["ec"] == "1440"]
    if not stop:
        return [l95_row("embark", "canoe", 820, result="not_covered", cause="no canoe in stop run at 820")]
    ts = float(stop[0]["t"])
    u = {"f": unit_line(1, *L95_CANOE), "player": 0, "id": 1, "figures": []}
    text = scene_text(HEAD, [u])
    fl = [(1, 0.0, None)] + [(2 + k, H30, None) for k in range(20)]
    k_arr = len(fl)
    fl.append((k_arr + 1, H30, None))
    n = max(1, int(round(ts / H30)))
    for k in range(n):
        fl.append((len(fl) + 1, ts / n, None))
    at = {820: len(fl) - 1}
    frames = sorted(pres)
    for a, b in zip(frames, frames[1:]):
        steps = b - a
        for k in range(steps):
            fl.append((len(fl) + 1, (pres[b] - pres[a]) / steps, None))
        at[b] = len(fl) - 1
    name = "l95_embark"
    rc, prog, st = drv.run(name, text, {1: ["0\t1\tmove\t2\t1"], k_arr: ["0\t1\tarrive\t2"]}, fl, forced=False)
    P = programs(prog)
    sts = statuses(st)
    for fr in frames:
        for fig, r in enumerate(sorted(q[fr], key=lambda x: int(x["id"]))):
            n_ = P.get((at[fr], 0, 1, fig))
            if not n_:
                rows.append(l95_row("embark", "canoe/%s" % r["id"], fr, obs="%s %s" % (r["ec"], r["t"]),
                                    result="not_covered", cause="no program: %s" % (sts.get((0, 1)),)))
                continue
            c = main_sample(n_[1])
            obs_ec = int(r["ec"]) if r["ec"] else -1
            d = c[2] - float(r["t"])
            if c[1] == obs_ec and abs(d) <= T_TOL:
                res, cause = "pass", ""
            elif fr == 820 and obs_ec == 1400:
                res, cause = "fail_stagger", "this canoe still runs while the other stops: a per-figure arrive (0042)"
            else:
                res, cause = "fail", "controller ec %d t %.4f" % (c[1], c[2])
            rows.append(l95_row("embark", "canoe/%s" % r["id"], fr, obs="ec %s t %s" % (r["ec"], r["t"]),
                                ctrl="figure %d: ec %d t %.4f (state %s)" % (fig, c[1], c[2], n_[0]),
                                result=res, cause=cause))
    return rows


def l95_aim_rows(drv, out):
    """AIM 856: the battleship's block against the controller's program for a steer (path 8 selector 0-2, path 7
    weight 1 and 0) under the resolved aim mask, through oracle_palettes (unitscene program, placement solved)."""
    sys.path.insert(0, HERE)
    import oracle_palettes as op
    bones = glob.glob(os.path.join(ARGS.bones0395, "AIM", "*.bones.json"))
    if not bones:
        return [l95_row("aim", "battleship", 856, result="not_covered", cause="no AIM bones")]
    fits = json.load(open(os.path.join(ARGS.fits0395, "AIM", "battleship.json"), encoding="utf-8"))
    blk = fits[0]
    u = {"f": unit_line(1, *L95_SHIP), "player": 0, "id": 1, "figures": []}
    text = scene_text(HEAD, [u])
    blocks, rows, notes = [], [], {}
    for sel in (0, 1, 2):
        for w in (1.0, 0.0):
            fl = [(i + 1, 0.0 if i == 0 else H30, None) for i in range(30)]
            name = "l95_aim_s%d_w%d" % (sel, int(w))
            got = l95_last(drv, name, text, {2: ["0\t1\tsteer\t*\t%g\t%d" % (w, sel)]}, fl, 1)
            if not got:
                notes[name] = "no program"
                continue
            blocks.append({"key": name, "bones": bones[0], "id": blk["id"], "mesh": blk["mesh"],
                           "model": "battleship.gr2", "program": op.program_text(got[1], got[2])})
            notes[name] = "state %s" % got[0]
    blocks.append({"key": "fit_aim_left", "bones": bones[0], "id": blk["id"], "mesh": blk["mesh"],
                   "model": "battleship.gr2", "program": "S|Battleship_Aim_Left.gr2|0"})
    exe_dir = os.path.dirname(os.path.dirname(os.path.abspath(ARGS.exe)))
    unitscene = ARGS.unitscene or os.path.join(exe_dir, "unitscene", "unitscene.exe")
    try:
        res = op.compare(blocks, unitscene, ARGS.units_dir, os.path.join(out, "palettes_aim"))
    except (RuntimeError, OSError) as e:
        raise StepFailed("aim palettes: %s" % e)
    for b in blocks:
        v = res.get(b["key"], {})
        within = v.get("within")
        rows.append(l95_row("aim", "battleship/%s" % b["key"], 856, obs="block %d: aim left at 0 (single fit 0.0000 deg)"
                            % blk["id"], ctrl="%s; %s" % (notes.get(b["key"], ""), b["program"][:160]),
                            result="pass" if within else "fail",
                            cause="rot elem %s trans %s" % (v.get("rot_elem_max"), v.get("trans_max"))))
    return rows


def step_live0395(drv, census, out):
    tab = l95_table()
    fade = []
    if ARGS.fade0395 and os.path.exists(ARGS.fade0395):
        with open(ARGS.fade0395, encoding="utf-8") as f:
            fade = json.load(f)
    rows = l95_fortify_rows(drv, tab, fade)
    rows += l95_clock_rows(tab)
    rows += l95_move_rows(drv, tab)
    rows += l95_embark_rows(drv, census, tab)
    rows += l95_aim_rows(drv, out)
    write_table(os.path.join(out, "live0395.csv"), rows)
    summ = collections.defaultdict(collections.Counter)
    for r in rows:
        summ[r["part"]][r["result"]] += 1
    return {k: dict(v) for k, v in summ.items()}


# ------------------------------------------------------------------------------------------------ D: random parts
def interval(n, p):
    m = n * p
    return m, Z999 * math.sqrt(n * p * (1 - p))


def step_random(drv, census, out):
    art = "ART_DEF_UNIT__WARRIOR"
    res = {}
    # R0 at creation: 1,000 warriors (12,000 figures), one frame dt 0, free mode
    units = [{"f": unit_line(i + 1, "UNIT_WARRIOR", art, 10 + i % 40, 10 + i // 40), "player": 0, "id": i + 1,
              "figures": []} for i in range(ARGS.r0_units)]
    _, prog, _ = drv.run("rnd_r0", scene_text(HEAD, units), {}, [(1, 0.0, None)], seed=7, forced=False)
    P = programs(prog)
    a = sum(1 for k, (s, n) in P.items() if int(n[0]["ec"]) == 1000)
    b = sum(1 for k, (s, n) in P.items() if int(n[0]["ec"]) == 2000)
    m, hw = interval(a + b, 0.6)
    res["R0_idle_A_share"] = {"figures": a + b, "ec1000": a, "ec2000": b, "expected_ec1000": m, "half_width_999": hw,
                              "within": abs(a - m) <= hw}
    # RandomRoll 0.0007 and R1 0.3 / 0.7: one warrior in free mode, idle
    nfr = ARGS.roll_frames
    u = [{"f": unit_line(1, "UNIT_WARRIOR", art), "player": 0, "id": 1, "figures": []}]
    frames = [(i + 1, 0.0 if i == 0 else 1.0 / 30.0 + 1e-9, None) for i in range(nfr)]
    _, prog, _ = drv.run("rnd_roll", scene_text(HEAD, u), {}, frames, seed=11, forced=False)
    st = collections.defaultdict(dict)
    for r in prog:
        if r["node"] == "0":
            st[int(r["figure"])][int(r["frame"])] = int(r["state"])
    in1 = rolls = fid = swap = 0
    for fg, byf in st.items():
        seq = [byf[k] for k in sorted(byf)]
        for x, y in zip(seq, seq[1:]):
            if x == 1:
                in1 += 1
            if x == 1 and y in (6, 3, 5):
                rolls += 1
            if x in (1, 6) and y == 3:
                fid += 1
            if x in (1, 6) and y == 5:
                swap += 1
    m, hw = interval(in1, 0.0007)
    res["RandomRoll_0.0007"] = {"updates_in_idle": in1, "fires": rolls, "expected": m, "half_width_999": hw,
                                "within": abs(rolls - m) <= hw,
                                "note": "one machine update per 1/30 s step; a frame holds one step here"}
    m2, hw2 = interval(fid + swap, 0.3)
    res["R1_fidget_share"] = {"decisions": fid + swap, "fidget": fid, "swap": swap, "expected_fidget": m2,
                              "half_width_999": hw2, "within": abs(fid - m2) <= hw2}
    return res


# ------------------------------------------------------------------------------------------------ main
def main():
    global ARGS
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", default="catalogue,sequences,transitions,random")
    ap.add_argument("--quick", action="store_true", help="small random runs (tests)")
    ap.add_argument("--clocks", default="continuous,steps", help="K6 readings to run (clock.h ClockMode): continuous scores (MC #2089), steps is a comparison line")
    for k, v in DEF.items():
        ap.add_argument("--" + k, default=v)
    ARGS = ap.parse_args()
    ARGS.r0_units = 50 if ARGS.quick else 1000
    ARGS.roll_frames = 300 if ARGS.quick else 8334
    out = os.path.abspath(ARGS.out).replace("\\", "/")
    if not any(out.lower().startswith(r) for r in WORK_ROOTS):
        print("oracle_anim: OUT %s is not under C:/c5-64bit-work or F:/c5-64bit-work" % out, file=sys.stderr)
        return 4
    steps = [s for s in ARGS.steps.split(",") if s]
    bad = [s for s in steps if s not in ("catalogue", "sequences", "transitions", "random", "palettes", "live0395")]
    bad += [c for c in ARGS.clocks.split(",") if c not in ("steps", "continuous")]
    if not [c for c in ARGS.clocks.split(",") if c]:
        bad.append("(no clock)")
    if bad:
        print("oracle_anim: unknown step %s" % ",".join(bad), file=sys.stderr)
        return 2
    need = [ARGS.exe, ARGS.game, ARGS.db, ARGS.census]
    if "catalogue" in steps:
        need.append(ARGS.scenes)
    if "sequences" in steps:
        need += [ARGS.q1, ARGS.table4407]
    if "palettes" in steps:
        need += [ARGS.table4407, ARGS.blends4407, ARGS.units_dir]
    if "live0395" in steps:
        need += [ARGS.table0395, ARGS.fits0395, ARGS.bones0395, ARGS.units_dir]
    missing = [p for p in need if not os.path.exists(p)]
    if missing:
        print("oracle_anim: input missing: %s" % ", ".join(missing), file=sys.stderr)
        return 3
    os.makedirs(out, exist_ok=True)
    census = Census(ARGS.census, ARGS.db)
    drv = Driver(ARGS.exe, out)
    summary = {"inputs": {k: getattr(ARGS, k) for k in DEF}, "tolerances": {"t_s": T_TOL, "w": W_TOL, "z": Z999}}
    t0 = time.time()
    rc = 0
    clocks = [c for c in ARGS.clocks.split(",") if c]
    for s in steps:
        modes = clocks if s != "catalogue" else clocks[:1]   # dt 0: the clock is not used
        for mode in modes:
            CLOCK[0] = mode
            sub = os.path.join(out, mode)
            os.makedirs(sub, exist_ok=True)
            try:
                summary.setdefault(s, {})[mode] = globals()["step_" + s](drv, census, sub)
            except StepFailed as e:
                summary.setdefault(s, {})[mode] = {"failed": str(e)}
                print("oracle_anim: step %s (%s clock) failed: %s" % (s, mode, e), file=sys.stderr)
                rc = 1
    summary["driver_runs"] = drv.runs
    summary["driver_seconds"] = round(drv.seconds, 1)
    summary["seconds"] = round(time.time() - t0, 1)
    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1, default=str)
    print(json.dumps({k: v for k, v in summary.items() if k != "inputs"}, indent=1, default=str)[:6000])
    return rc


ARGS = None
CLOCK = ["steps"]
if __name__ == "__main__":
    sys.exit(main())
