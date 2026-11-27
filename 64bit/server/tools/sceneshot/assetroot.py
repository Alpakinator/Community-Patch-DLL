"""Asset roots for the offline scene players (ticket 0614, offline-scene-harness.md section 34). Written 2026-11-20 for
this repository (GPL-3.0-or-later).

A live run on a pool instance rewrites that instance's mods database while its game starts ("3 activated", then "0
activated", then "3" again): a player that read the recording instance's MODS folder and database in that window drew
without the mods (".fxsxml not found", exit 8). An asset root is a frozen copy that no live run writes:

    <root>/asset-root.txt                                          the manifest (KEY=VALUE lines, UTF-8)
    <root>/Documents/My Games/Sid Meier's Civilization 5/cache/Civ5ModsDatabase.db    the instance's mods database
    <root>/Documents/My Games/Sid Meier's Civilization 5/MODS/<each activated mod's folder>

and an install that is never launched (default the setting `render_game_dir` of tools/release/settings.py, the
workstation's pure_vp copy), named by the manifest, never copied. The player (sceneshot.exe --asset-root DIR, or the
environment's C5_SCENE_ASSET_ROOT; scenerun.py --asset-root) checks the manifest before it reads anything: the
database's SHA-256 and the MODS folder's listing hash (tree_hash below, the same function as sceneshot.cpp TreeHash), a
refusal (exit 9) when either changed, when a -journal or -wal file lies beside the database, or when the install lies in
a claimed pool instance. Nothing falls back to the recording instance.

    python -I server/tools/sceneshot/assetroot.py make --from c5h-c --out C:/c5-64bit-work/asset-roots/c5h-c-20261120 \
           [--install DIR] [--name NAME]
    python -I server/tools/sceneshot/assetroot.py verify DIR [--full]
    python -I server/tools/sceneshot/assetroot.py show DIR

make refuses (exit 2, nothing left behind) when the source instance is claimed (tools/harness/pool.py holder) or a
process runs from it, when its mods database has a -journal or -wal beside it, is unreadable or has no activated mod,
when an activated mod has no folder under MODS, when the database changed while it was copied, when the install lies in
a pool instance (an instance's install is launched; name a never-launched copy) or misses CivilizationV_DX11.exe, or
when --out exists. verify: exit 0 equal to the manifest, 1 not (the line says what); --full also compares every file's
content with the content hash make recorded. Exit 2 for a usage error.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import os
import shutil
import sqlite3
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
CIV = Path("Documents") / "My Games" / "Sid Meier's Civilization 5"
DB_NAME = "Civ5ModsDatabase.db"
MANIFEST = "asset-root.txt"
FORMAT = "1"
REQUIRED = ("format", "name", "made_from", "made_at", "install", "mods_db_sha256", "mods_tree_sha256", "mods_files",
            "activated")


class AssetRootError(Exception):
    pass


def _pool():
    sys.path.insert(0, str(REPO / "tools" / "harness"))
    try:
        import pool  # type: ignore
        return pool
    finally:
        sys.path.pop(0)


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def tree_listing(mods: Path) -> List[Tuple[str, int, Path]]:
    out = []
    for dirpath, _dirs, files in os.walk(mods):
        for n in files:
            p = Path(dirpath) / n
            out.append((p.relative_to(mods).as_posix(), p.stat().st_size, p))
    return out


def tree_hash(mods: Path) -> Tuple[str, int]:
    """SHA-256 over the sorted lines "<path relative to MODS, '/' separators>\\t<size>\\n" (UTF-8, sorted by bytes) and
    the file count: sceneshot.cpp TreeHash computes the same."""
    lines = sorted(("%s\t%d\n" % (rel, size)).encode("utf-8") for rel, size, _ in tree_listing(mods))
    return hashlib.sha256(b"".join(lines)).hexdigest(), len(lines)


def content_hash(mods: Path) -> str:
    """SHA-256 over the sorted lines "<rel>\\t<SHA-256 of the file>\\n" (verify --full)."""
    lines = sorted(("%s\t%s\n" % (rel, sha256_file(p))).encode("utf-8") for rel, _size, p in tree_listing(mods))
    return hashlib.sha256(b"".join(lines)).hexdigest()


def activated_folders(db: Path, mods: Path) -> Tuple[int, List[str], List[str]]:
    """(activated, folders relative to MODS, activated mods without a folder) by the rule of srv_residency_source.cpp
    ReadActiveModFolders: Mods rows with Activated = 1; each one's folder is a ModFiles row's EvaluatedPath without its
    Path, re-rooted after "...\\Sid Meier's Civilization 5\\MODS\\"."""
    con = sqlite3.connect("file:%s?mode=ro" % db.as_posix(), uri=True)
    try:
        act = {(str(i).lower(), str(v)) for i, v, a in con.execute("select ModID, Version, Activated from Mods") if a}
        found: Dict[Tuple[str, str], str] = {}
        tail = "\\sid meier's civilization 5\\mods\\"
        for i, v, path, ev in con.execute("select ModID, Version, Path, EvaluatedPath from ModFiles"):
            k = (str(i).lower(), str(v))
            if k not in act or k in found or not path or not ev:
                continue
            path, ev = str(path).replace("/", "\\"), str(ev).replace("/", "\\")
            if len(ev) <= len(path) + 1 or ev[-len(path):].lower() != path.lower() or ev[-len(path) - 1] != "\\":
                continue
            folder = ev[:-len(path) - 1]
            cut = folder.lower().rfind(tail)
            if cut < 0 or cut + len(tail) >= len(folder):
                continue
            found[k] = folder[cut + len(tail):]
    finally:
        con.close()
    missing = sorted("%s v%s" % k for k in act if k not in found or not (mods / found[k]).is_dir())
    return len(act), sorted({f for k, f in found.items() if (mods / f).is_dir()}), missing


def read_manifest(root: Path) -> Dict[str, str]:
    p = root / MANIFEST
    if not p.is_file():
        raise AssetRootError("%s: no %s" % (root, MANIFEST))
    kv = {}
    for line in p.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise AssetRootError("%s: line %r is not KEY=VALUE" % (p, line))
        k, v = line.split("=", 1)
        kv[k] = v
    miss = [k for k in REQUIRED if not kv.get(k)]
    if miss:
        raise AssetRootError("%s: no %s" % (p, ", ".join(miss)))
    if kv["format"] != FORMAT:
        raise AssetRootError("%s: format %s (this tool writes %s)" % (p, kv["format"], FORMAT))
    return kv


def _source_instance(arg: str) -> Path:
    p = Path(arg)
    if (p / "instance.json").is_file():
        return p.resolve()
    pool = _pool()
    try:
        return Path(pool._find(arg)["dir"])
    except pool.PoolError as e:
        raise AssetRootError("--from %s: %s" % (arg, e))


def _check_source_free(inst: Path, when: str):
    pool = _pool()
    h = pool.holder(inst)
    if h["claimed"]:
        c = h["claim"] or {}
        raise AssetRootError("%s: instance %s is claimed (owner %s, ticket %s): a live run may be writing its MODS folder "
                             "and mods database; wait until it is free" % (when, inst.name, c.get("owner"), c.get("ticket")))
    run = pool.alive(inst)
    if run:
        raise AssetRootError("%s: a process runs from instance %s (pid %s): its mods database may be changing"
                             % (when, inst.name, ",".join(str(a["pid"]) for a in run)))


def make(src: str, out: Path, install: Path = None, name: str = None) -> Dict[str, str]:
    inst = _source_instance(src)
    out = Path(os.path.abspath(out))
    if out.exists():
        raise AssetRootError("--out %s exists: an asset root is made once (choose a new name)" % out)
    if install is None:
        sys.path.insert(0, str(REPO / "tools" / "release"))
        try:
            import settings  # type: ignore
        finally:
            sys.path.pop(0)
        install = settings.get("render_game_dir")
        if install is None:
            raise AssetRootError("no --install and no setting render_game_dir (tools/release/settings.py)")
    install = Path(os.path.abspath(install))
    hi = _pool().holder(install)
    if hi["instance"]:
        raise AssetRootError("--install %s lies in pool instance %s, whose game is launched: name a never-launched copy "
                             "(default: the setting render_game_dir)" % (install, hi["instance"]))
    if not (install / "CivilizationV_DX11.exe").is_file():
        raise AssetRootError("--install %s has no CivilizationV_DX11.exe" % install)
    _check_source_free(inst, "before the copy")
    civ = inst / CIV
    db, mods = civ / "cache" / DB_NAME, civ / "MODS"
    for side in ("-journal", "-wal"):
        if Path(str(db) + side).exists():
            raise AssetRootError("%s%s exists: a write of the game is in progress or was cut off" % (db, side))
    if not db.is_file():
        raise AssetRootError("%s is missing (the instance has not run with mods yet?)" % db)
    before = sha256_file(db)
    tmp = Path(tempfile.mkdtemp(prefix=".assetroot-", dir=str(out.parent) if out.parent.is_dir() else None))
    try:
        (tmp / CIV / "cache").mkdir(parents=True)
        shutil.copy2(db, tmp / CIV / "cache" / DB_NAME)
        if sha256_file(tmp / CIV / "cache" / DB_NAME) != before:
            raise AssetRootError("%s changed while it was copied" % db)
        n_act, folders, missing = activated_folders(tmp / CIV / "cache" / DB_NAME, mods)
        if n_act == 0:
            raise AssetRootError("%s lists no activated mod (a game was starting on the instance?)" % db)
        if missing:
            raise AssetRootError("activated mods without a folder under %s: %s" % (mods, "; ".join(missing)))
        for f in folders:
            shutil.copytree(mods / f, tmp / CIV / "MODS" / f)
        _check_source_free(inst, "after the copy")
        if sha256_file(db) != before:
            raise AssetRootError("%s changed while the mods' folders were copied" % db)
        th, n = tree_hash(tmp / CIV / "MODS")
        kv = {"format": FORMAT, "name": name or out.name, "made_from": inst.name, "made_from_dir": inst.as_posix(),
              "made_at": _dt.datetime.now().astimezone().isoformat(timespec="seconds"), "install": install.as_posix(),
              "install_exe_sha256": sha256_file(install / "CivilizationV_DX11.exe"), "mods_db_sha256": before,
              "mods_tree_sha256": th, "mods_files": str(n), "mods_content_sha256": content_hash(tmp / CIV / "MODS"),
              "activated": str(n_act), "activated_folders": "; ".join(folders)}
        text = ("# ticket 0614: an asset root of the offline scene players (server/tools/sceneshot/assetroot.py); do not "
                "edit, make a new one\n" + "".join("%s=%s\n" % kv_ for kv_ in kv.items()))
        (tmp / MANIFEST).write_text(text, encoding="utf-8", newline="\n")
        out.parent.mkdir(parents=True, exist_ok=True)
        os.replace(tmp, out)
        return kv
    finally:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)


def verify(root: Path, full: bool = False) -> List[str]:
    """[] when the asset root equals its manifest; else what differs."""
    kv = read_manifest(root)
    civ = root / CIV
    db, mods = civ / "cache" / DB_NAME, civ / "MODS"
    bad = []
    for side in ("-journal", "-wal"):
        if Path(str(db) + side).exists():
            bad.append("%s%s exists" % (db, side))
    if not db.is_file():
        bad.append("%s is missing" % db)
    elif sha256_file(db) != kv["mods_db_sha256"]:
        bad.append("the mods database's SHA-256 is not the manifest's")
    th, n = tree_hash(mods) if mods.is_dir() else ("", 0)
    if th != kv["mods_tree_sha256"] or str(n) != kv["mods_files"]:
        bad.append("MODS lists %d files with listing hash %s; the manifest: %s files, %s"
                   % (n, th, kv["mods_files"], kv["mods_tree_sha256"]))
    if full and not bad and kv.get("mods_content_sha256") and content_hash(mods) != kv["mods_content_sha256"]:
        bad.append("a file's content under MODS is not the manifest's (same names and sizes)")
    inst = Path(kv["install"])
    if not (inst / "CivilizationV_DX11.exe").is_file():
        bad.append("the install %s has no CivilizationV_DX11.exe" % inst)
    h = _pool().holder(inst)
    if h["claimed"]:
        bad.append("the install %s lies in pool instance %s, which is claimed" % (inst, h["instance"]))
    return bad


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    mk = sub.add_parser("make")
    mk.add_argument("--from", dest="src", required=True, help="the source instance: a pool name or its folder")
    mk.add_argument("--out", required=True)
    mk.add_argument("--install", help="a never-launched install (default: the setting render_game_dir)")
    mk.add_argument("--name")
    vf = sub.add_parser("verify")
    vf.add_argument("root")
    vf.add_argument("--full", action="store_true")
    sh = sub.add_parser("show")
    sh.add_argument("root")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "make":
            kv = make(a.src, Path(a.out), Path(a.install) if a.install else None, a.name)
            print("assetroot: made %s from %s: %s activated (%s), %s files under MODS, install %s"
                  % (a.out, kv["made_from"], kv["activated"], kv["activated_folders"], kv["mods_files"], kv["install"]))
            return 0
        if a.cmd == "verify":
            bad = verify(Path(a.root), a.full)
            for b in bad:
                print("assetroot: DIFFERS: %s" % b)
            print("assetroot: %s %s" % ("OK" if not bad else "NOT OK", a.root))
            return 0 if not bad else 1
        for k, v in read_manifest(Path(a.root)).items():
            print("%s=%s" % (k, v))
        return 0
    except AssetRootError as e:
        print("assetroot: refused: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
