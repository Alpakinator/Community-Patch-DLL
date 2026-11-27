"""Ticket 0614: asset roots (server/tools/sceneshot/assetroot.py) and the player's checks of them (sceneshot.exe
--asset-root). Offline: a fake pool instance with a real SQLite mods database under tmp_path, a temporary pool dir, no
process listed or started, no game file read.

    set C5_SCENESHOT_EXE=<the built sceneshot.exe>     (build_sceneshot.py; required: a skip counts as failed)
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest -q server/tests/render/scenebundle/test_assetroot.py

The player loads an asset root before it reads the bundle, so a missing bundle after a good asset root ends with
NoManifest (21) after the "asset root ... checked" line, and a bad asset root with Install (9) before it: neither needs
a GPU, the game or a recorded bundle."""
import json
import os
import sqlite3
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "server" / "tools" / "sceneshot"))
sys.path.insert(0, str(REPO / "tools" / "harness"))

import assetroot  # noqa: E402
import pool  # noqa: E402

CIV = Path("Documents") / "My Games" / "Sid Meier's Civilization 5"
NOWIN = 0x08000000


def make_instance(root: Path, name: str, activated=("cp", "vp"), files=None, journal=False):
    """A pool instance folder: instance.json, MODS with three mods (cp, vp, eui) and a Civ5ModsDatabase.db that names
    them as the game does (EvaluatedPath = <MODS>\\<folder>\\<Path>), `activated` of them activated."""
    d = root / name
    (d / "install").mkdir(parents=True)
    (d / "instance.json").write_text(json.dumps({"name": name, "state": "ready"}))
    mods = d / CIV / "MODS"
    folders = {"cp": "(1) Community Patch", "vp": "(2) Vox Populi", "eui": "(3a) VP - EUI Compatibility Files"}
    files = files or {"cp": {"CvGameCore_Expansion2.dll": b"dll", "Art/a.fxsxml": b"<a/>"},
                      "vp": {"Art/Units/ironcladbb.fxsxml": b"<ironclad/>", "Art/Units/x.gr2": b"\x00" * 300},
                      "eui": {"UI/e.lua": b"-- eui"}}
    for k, fs in files.items():
        for rel, data in fs.items():
            p = mods / folders[k] / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
    (d / CIV / "cache").mkdir(parents=True)
    db = d / CIV / "cache" / "Civ5ModsDatabase.db"
    con = sqlite3.connect(db)
    con.execute("create table Mods(ModID text, Version integer, Activated integer, Enabled integer)")
    con.execute("create table ModFiles(ModID text, Version integer, Path text, EvaluatedPath text)")
    for i, k in enumerate(("cp", "vp", "eui")):
        mid = "id-%s" % k
        con.execute("insert into Mods values(?,?,?,1)", (mid, 1 + i, 1 if k in activated else 0))
        for rel in files.get(k, {}):
            r = rel.replace("/", "\\")
            con.execute("insert into ModFiles values(?,?,?,?)",
                        (mid, 1 + i, r, "%s\\%s\\%s" % (str(mods).replace("/", "\\"), folders[k], r)))
    con.commit()
    con.close()
    if journal:
        Path(str(db) + "-journal").write_bytes(b"j")
    return d


def make_install(p: Path) -> Path:
    p.mkdir(parents=True)
    (p / "CivilizationV_DX11.exe").write_bytes(b"MZ-0614")
    return p


def claim_file(name: str, owner="w9-0614"):
    pool.claims_dir().mkdir(parents=True, exist_ok=True)
    (pool.claims_dir() / ("%s.json" % name.lower())).write_text(json.dumps(
        {"name": name, "owner": owner, "ticket": "0614", "expires_at": "2100-01-01T00:00:00-05:00"}), encoding="utf-8")


@pytest.fixture
def env(tmp_path, monkeypatch):
    roots = tmp_path / "instances"
    roots.mkdir()
    monkeypatch.setenv("C5_INSTANCES_ROOTS", str(roots))
    monkeypatch.setenv("C5_POOL_DIR", str(tmp_path / "pool"))
    monkeypatch.delenv("C5_SCENE_ASSET_ROOT", raising=False)
    monkeypatch.setattr(pool, "PROCESS_LISTER", lambda: [])
    return {"roots": roots, "install": make_install(tmp_path / "pure_vp"), "work": tmp_path}


def no_leftovers(parent: Path):
    assert not [p for p in parent.iterdir() if p.name.startswith(".assetroot-")]


# ---- make, verify ---------------------------------------------------------------------------------------------------------

def test_make_copies_the_activated_mods_only(env):
    inst = make_instance(env["roots"], "c5h-a")
    out = env["work"] / "roots" / "r1"
    (env["work"] / "roots").mkdir()
    kv = assetroot.make(str(inst), out, env["install"])
    assert kv["made_from"] == "c5h-a" and kv["activated"] == "2"
    assert kv["activated_folders"] == "(1) Community Patch; (2) Vox Populi"
    mods = out / CIV / "MODS"
    assert sorted(p.name for p in mods.iterdir()) == ["(1) Community Patch", "(2) Vox Populi"]
    assert (mods / "(2) Vox Populi" / "Art" / "Units" / "ironcladbb.fxsxml").read_bytes() == b"<ironclad/>"
    assert kv["mods_files"] == "4"
    lines = sorted(b"%s\t%d\n" % (r.encode(), n) for r, n in [
        ("(1) Community Patch/CvGameCore_Expansion2.dll", 3), ("(1) Community Patch/Art/a.fxsxml", 4),
        ("(2) Vox Populi/Art/Units/ironcladbb.fxsxml", 11), ("(2) Vox Populi/Art/Units/x.gr2", 300)])
    import hashlib
    assert kv["mods_tree_sha256"] == hashlib.sha256(b"".join(lines)).hexdigest()
    assert assetroot.verify(out) == [] and assetroot.verify(out, full=True) == []
    assert assetroot.read_manifest(out)["install"] == env["install"].as_posix()
    no_leftovers(env["work"] / "roots")


def test_cli_make_verify_show(env):
    inst = make_instance(env["roots"], "c5h-b")
    out = env["work"] / "cli-root"
    run = lambda *a: subprocess.run([sys.executable, "-I", str(REPO / "server/tools/sceneshot/assetroot.py"), *a],
                                    capture_output=True, text=True, timeout=120, env=dict(os.environ))
    r = run("make", "--from", str(inst), "--out", str(out), "--install", str(env["install"]))
    assert r.returncode == 0 and "assetroot: made" in r.stdout, r.stderr
    r = run("verify", str(out), "--full")
    assert r.returncode == 0 and r.stdout.strip().endswith("assetroot: OK %s" % out)
    r = run("show", str(out))
    assert r.returncode == 0 and "made_from=c5h-b" in r.stdout
    r = run("make", "--from", str(inst), "--out", str(out), "--install", str(env["install"]))
    assert r.returncode == 2 and "exists" in r.stderr


@pytest.mark.parametrize("case", ["claimed", "journal", "none-activated", "install-in-instance", "no-exe",
                                  "folder-missing"])
def test_make_refusals(env, case):
    inst = make_instance(env["roots"], "c5h-c", activated=() if case == "none-activated" else ("cp", "vp"),
                         journal=case == "journal")
    install = env["install"]
    if case == "claimed":
        claim_file("c5h-c")
    elif case == "install-in-instance":
        install = make_instance(env["roots"], "c5h-d") / "install"
        (install / "CivilizationV_DX11.exe").write_bytes(b"MZ")
    elif case == "no-exe":
        install = env["work"] / "empty-install"
        install.mkdir()
    elif case == "folder-missing":
        import shutil
        shutil.rmtree(inst / CIV / "MODS" / "(2) Vox Populi")
    out = env["work"] / ("r-" + case)
    with pytest.raises(assetroot.AssetRootError) as e:
        assetroot.make(str(inst), out, install)
    want = {"claimed": "is claimed (owner w9-0614", "journal": "-journal exists", "none-activated": "no activated mod",
            "install-in-instance": "lies in pool instance c5h-d", "no-exe": "has no CivilizationV_DX11.exe",
            "folder-missing": "without a folder"}[case]
    assert want in str(e.value)
    assert not out.exists()
    no_leftovers(env["work"])


@pytest.mark.parametrize("change", ["db", "added", "size", "content"])
def test_verify_sees_a_change(env, change):
    inst = make_instance(env["roots"], "c5h-e")
    out = env["work"] / "r-v"
    assetroot.make(str(inst), out, env["install"])
    mods = out / CIV / "MODS"
    if change == "db":
        with open(out / CIV / "cache" / "Civ5ModsDatabase.db", "ab") as f:
            f.write(b"\0")
    elif change == "added":
        (mods / "(2) Vox Populi" / "new.xml").write_bytes(b"x")
    elif change == "size":
        (mods / "(2) Vox Populi" / "Art" / "Units" / "x.gr2").write_bytes(b"\0" * 301)
    else:
        (mods / "(2) Vox Populi" / "Art" / "Units" / "x.gr2").write_bytes(b"\1" * 300)
    if change == "content":
        assert assetroot.verify(out) == []          # names and sizes equal: only --full sees it
        assert any("content" in b for b in assetroot.verify(out, full=True))
    else:
        assert assetroot.verify(out) != []


# ---- the player (sceneshot.exe) -------------------------------------------------------------------------------------------

def player():
    exe = os.environ.get("C5_SCENESHOT_EXE")
    assert exe and Path(exe).is_file(), "C5_SCENESHOT_EXE must name the built sceneshot.exe (server/tools/sceneshot/build_sceneshot.py)"
    return exe


def play(env, *args, extra_env=None):
    e = dict(os.environ)
    e.update(extra_env or {})
    cp = subprocess.run([player(), "--bundle", str(env["work"] / "no_such_bundle_0614"), "--out", str(env["work"] / "out"),
                         "--dumped", *args], capture_output=True, text=True, errors="replace", timeout=300,
                        creationflags=NOWIN, env=e)
    lines = [ln for ln in cp.stdout.splitlines() if ln.strip()]
    return cp.returncode, lines


def made_root(env, name="c5h-p"):
    inst = make_instance(env["roots"], name)
    out = env["work"] / ("root-" + name)
    assetroot.make(str(inst), out, env["install"])
    return out


def test_player_reads_a_python_made_root(env):
    """The player's TreeHash and SHA-256 equal assetroot.py's: the root is accepted, then the missing bundle is NoManifest."""
    root = made_root(env)
    rc, lines = play(env, "--asset-root", str(root))
    assert rc == 21, lines
    assert any(ln.startswith("sceneshot: asset root ") and "(--asset-root)" in ln and "made from c5h-p" in ln and
               "4 files under MODS, 2 activated mods" in ln for ln in lines), lines
    assert lines[-1].startswith("sceneshot: FAILED 21 NoManifest"), lines
    rc, lines = play(env, extra_env={"C5_SCENE_ASSET_ROOT": str(root)})
    assert rc == 21 and any("(C5_SCENE_ASSET_ROOT)" in ln for ln in lines), lines


@pytest.mark.parametrize("bad", ["db", "added", "journal", "no-manifest", "missing", "format"])
def test_player_refuses_a_changed_root(env, bad):
    root = made_root(env)
    mf = root / "asset-root.txt"
    if bad == "db":
        with open(root / CIV / "cache" / "Civ5ModsDatabase.db", "ab") as f:
            f.write(b"\0")
    elif bad == "added":
        (root / CIV / "MODS" / "(1) Community Patch" / "extra.lua").write_bytes(b"--")
    elif bad == "journal":
        (root / CIV / "cache" / "Civ5ModsDatabase.db-journal").write_bytes(b"j")
    elif bad == "no-manifest":
        mf.unlink()
    elif bad == "missing":
        root = env["work"] / "no_root_0614"
    else:
        mf.write_text(mf.read_text(encoding="utf-8").replace("format=1", "format=7"), encoding="utf-8")
    rc, lines = play(env, "--asset-root", str(root))
    want = {"db": "changed since it was made", "added": "changed since it was made", "journal": "-journal or -wal",
            "no-manifest": "no asset-root.txt", "missing": "the asset root is missing", "format": "format 7"}[bad]
    assert rc == 9 and lines[-1].startswith("sceneshot: FAILED 9 Install") and want in lines[-1], lines
    assert not (env["work"] / "out").exists()


def test_player_refuses_an_install_in_a_claimed_instance(env):
    """An asset root whose manifest names a claimed instance's install (hand-edited: make refuses it) is refused."""
    root = made_root(env)
    other = make_instance(env["roots"], "c5h-q") / "install"
    (other / "CivilizationV_DX11.exe").write_bytes(b"MZ")
    mf = root / "asset-root.txt"
    mf.write_text(mf.read_text(encoding="utf-8").replace("install=" + env["install"].as_posix(), "install=" + other.as_posix()),
                  encoding="utf-8")
    rc, lines = play(env, "--asset-root", str(root))
    assert rc == 21, lines                       # unclaimed: read (the bundle is missing)
    claim_file("c5h-q")
    rc, lines = play(env, "--asset-root", str(root))
    assert rc == 9 and "lies in pool instance c5h-q, which the pool shows claimed" in lines[-1] and "owner w9-0614" in lines[-1], lines


@pytest.mark.parametrize("args", [["--install", "X"], ["--mods", "X"]])
def test_player_usage_conflicts(env, args):
    root = made_root(env)
    rc, lines = play(env, "--asset-root", str(root), *args)
    assert rc == 2 and "names the install and the mods' folders" in lines[-1], lines
    rc, lines = play(env, *args, extra_env={"C5_SCENE_ASSET_ROOT": str(root)})
    assert rc == 2 and "unset C5_SCENE_ASSET_ROOT" in lines[-1], lines
