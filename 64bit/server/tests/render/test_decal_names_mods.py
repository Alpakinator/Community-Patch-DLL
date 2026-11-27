"""Issue 0348 (from 0166's 4105): the decal names and textures are read from the ACTIVATED mods' folders only, as the game's
own record of them gives (<Documents>\\My Games\\Sid Meier's Civilization 5\\cache\\Civ5ModsDatabase.db, table Mods,
Activated = 1; each mod's folder from ModFiles' EvaluatedPath without its Path), not every folder under MODS.
srvrender::ModFolders / ReadActiveModFolders in server/src/render/srv_residency_source.cpp.

The rig of test_decal_wiring.py (render_host_test.exe on a hidden desktop, render_tiles=bake with the stand-in baker, the
fake decal tap of this process). Every test makes its own Documents folder (C5_DOCUMENTS) with a fake MODS tree and a fake
mods database written here with Python's sqlite3 in the game's schema and page size (1,024 bytes): never the owner's real MODS
or database. Every negative case checks the log, the fatal hook's count and the exit code.
"""
import re
import sqlite3
import time
from pathlib import Path

import pytest

import renderfx as fx
from test_decal_wiring import (DEC_A, FARM_XML, Cameras, FakeDecalTap, _install, decal_loud, decal_totals, fatal_calls,
                               fatal_or_set, image, start, traced_bakes)

CIV = Path("My Games") / "Sid Meier's Civilization 5"

# the game's own CREATE TABLE statements (a copy of a Civ5ModsDatabase.db, schema version 14, read 2026-11-16)
SCHEMA = [
    "CREATE TABLE Mods('RowID' INTEGER PRIMARY KEY  AUTOINCREMENT  NOT NULL , 'ModID' TEXT NOT NULL , 'Version' TEXT, "
    "'Type' INTEGER DEFAULT 1, 'Installed' BOOL DEFAULT 0, 'Enabled' BOOL DEFAULT 0, 'Activated' BOOL DEFAULT 0, "
    "'LastRetrieved' DATETIME)",
    "CREATE TABLE ModFiles('ModID' TEXT NOT NULL , 'Version' INTEGER NOT NULL, 'Path' TEXT NOT NULL , 'md5' TEXT, "
    "'EvaluatedPath' TEXT, Import INTEGER DEFAULT 0, PRIMARY KEY ('ModID', 'Version', 'Path'), "
    "FOREIGN KEY (ModID, Version) REFERENCES Mods(ModID, Version))",
    "CREATE TABLE ModProperties('ModID' TEXT NOT NULL , 'Version' INTEGER NOT NULL, 'Name' TEXT NOT NULL , 'Value' TEXT, "
    "PRIMARY KEY ('ModID', 'Version', 'Name'), FOREIGN KEY (ModID, Version) REFERENCES Mods(ModID, Version))",
    "CREATE TABLE ModSystemSettings('Name' TEXT PRIMARY KEY NOT NULL, 'Value' TEXT)",
]

INACTIVE_FARM = FARM_XML.replace(b"crops_maize_d.dds", b"inactive_only.dds")
ACTIVE, INACTIVE = "(2) Active Mod", "(1) Inactive Mod"   # the inactive one sorts first: under 4105's rule it would win


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request, monkeypatch):
    docs = tmp_path / "docs"
    (docs / CIV / "MODS").mkdir(parents=True)
    (docs / CIV / "cache").mkdir(parents=True)
    monkeypatch.setenv("C5_DOCUMENTS", str(docs))
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    r.docs = docs
    r.mods = docs / CIV / "MODS"
    r.db = docs / CIV / "cache" / "Civ5ModsDatabase.db"
    yield r
    r.close()


@pytest.fixture
def tap():
    t = FakeDecalTap()
    yield t
    t.close()


def mod_folder(rig, name, files):
    """A mod's folder under the fake MODS with its files (relative path -> bytes); returns its relative paths."""
    d = rig.mods / name
    for rel, data in files.items():
        f = d / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(data)
    return list(files)


def mods_db(path, mods, docs_for_paths, filler=0):
    """The mods database: mods = [(mod id, version, activated, folder name under MODS or an absolute folder, [paths])].
    EvaluatedPath = <folder>\\<path> as the game writes it (backslashes up to the folder, the path's own slashes after).
    docs_for_paths: the Documents folder the EvaluatedPaths name (another one: a copied instance's database).
    filler: that many extra ModFiles rows of an inactive mod (the table then spans interior pages)."""
    if path.exists():
        path.unlink()
    c = sqlite3.connect(str(path))
    c.execute("PRAGMA page_size = 1024")
    c.execute("PRAGMA journal_mode = DELETE")
    for s in SCHEMA:
        c.execute(s)
    root = str(Path(docs_for_paths) / CIV / "MODS")
    for mid, ver, act, folder, paths in mods:
        c.execute("INSERT INTO Mods(ModID, Version, Installed, Enabled, Activated) VALUES (?, ?, 1, 1, ?)", (mid, str(ver), act))
        c.execute("INSERT INTO ModProperties VALUES (?, ?, 'Name', ?)", (mid, ver, str(folder)))
        base = folder if Path(folder).is_absolute() else root + "\\" + folder
        for p in paths:
            c.execute("INSERT INTO ModFiles VALUES (?, ?, ?, '0', ?, 1)", (mid, ver, p, base + "\\" + p))
    for i in range(filler):
        c.execute("INSERT INTO ModFiles VALUES ('f1f1f1f1-0000-0000-0000-000000000000', 1, ?, '0', ?, 1)",
                  ("Filler/file_%05d.xml" % i, root + "\\(9) Filler\\Filler/file_%05d.xml" % i))
    c.execute("INSERT INTO Mods(ModID, Version, Activated) VALUES ('f1f1f1f1-0000-0000-0000-000000000000', '1', 0)")
    c.execute("INSERT INTO ModSystemSettings VALUES ('Version', '14')")
    c.commit()
    c.close()
    assert not Path(str(path) + "-journal").exists()


def two_mods(rig):
    """(1) Inactive Mod (its own farm_types.xml naming inactive_only.dds, and that texture) and (2) Active Mod (the
    farm_types.xml naming crops_maize_d.dds, and the textures)."""
    ia = mod_folder(rig, INACTIVE, {"Core/farm_types.xml": INACTIVE_FARM, "Art/inactive_only.dds": b"x",
                                    "(1) Inactive Mod (v 1).modinfo": b"<Mod/>"})
    ac = mod_folder(rig, ACTIVE, {"Core/farm_types.xml": FARM_XML, "Art/crops_maize_d.dds": b"x",
                                  "Art/Crops_Generic_02_H.dds": b"x", "(2) Active Mod (v 3).modinfo": b"<Mod/>"})
    return ia, ac


def stock_install(rig):
    return _install(rig.tmp, {"Assets/Decals/RoadsandRails_new_D.dds": b"x", "Assets/Decals/RoadsandRails_new_H.dds": b"x",
                              "Assets/Gameplay/farm_types.xml": FARM_XML.replace(b"crops_maize_d.dds", b"stock_only.dds")})


def mods_line(text):
    m = re.search(r"decals: mods: (.*)", text)
    assert m, text[-3000:]
    return m.group(1)


def run_set(rig, tap, names, wait=None):
    """Starts the rig, publishes one set with `names` on tile (1, 1), waits for `wait` (a log pattern) or for the set to be
    in use or refused (fatal_or_set; ticket 0496: was a 1.5 s sleep in a fixed 5 s run); returns (exit code, stdout,
    render.log)."""
    p = start(rig, render_bake_install=str(stock_install(rig)))
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        dec = (DEC_A[0], 0.5, 0, (1, 0xFFFFFFFF, 0xFFFFFFFF), 2)
        tap.publish(1, image(1, [dec], names, [dec[0]]))
        assert tap.wait_ack(1) == 0
        if wait:
            rig.wait_log(wait, p=p)
        else:
            fatal_or_set(rig, p)
    finally:
        cams.close()
    code = rig.stop(p)
    return code, rig.stdout(), rig.text()


def test_the_active_mods_farm_types_and_textures_are_read(rig, tap):
    """The activated mod's farm_types.xml resolves the reference (not the inactive mod's, which sorts first and would win
    under 4105's rule), its texture is found; the log line names 1 activated mod and its folder only; no fatal."""
    ia, ac = two_mods(rig)
    mods_db(rig.db, [("11111111-1111-1111-1111-111111111111", 1, 0, INACTIVE, ia),
                     ("22222222-2222-2222-2222-222222222222", 3, 1, ACTIVE, ac)], rig.docs)
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/1/albedo"],
                           wait=r"decals: bake \d+ tile \(1, 1\)")
    assert code == 0, out[-2000:]
    # the behaviour first: under 4105's rule the inactive mod's farm_types.xml (its folder sorts first) was read
    assert re.search(r"decals: farm_types.xml from .*\(2\) Active Mod.*: \d+ attributes", t), t[-3000:]
    line = mods_line(t)
    assert re.match(r"1 activated in '.*Civ5ModsDatabase\.db' \(read in [\d.]+ ms\), 0 without a folder under MODS; "
                    r"1 folders above the install: \(2\) Active Mod$", line), line
    assert re.search(r"decals: farm_types.xml from .*\(2\) Active Mod.*: \d+ attributes", t), t[-3000:]
    assert re.search(r"decals: set generation 1 \(load 1\) in use: 1 decals, 2 names \(1 farm references resolved\)", t), t[-3000:]
    assert fatal_calls(out) == 0 and decal_loud(t) == [], decal_loud(t)


def test_an_inactive_mods_texture_is_not_found(rig, tap):
    """A texture only the inactive mod's folder has: not in the view, so the set is refused (an install failure: LOUD,
    the fatal hook once, no bake carries it); under 4105's rule it was found."""
    ia, ac = two_mods(rig)
    mods_db(rig.db, [("11111111-1111-1111-1111-111111111111", 1, 0, INACTIVE, ia),
                     ("22222222-2222-2222-2222-222222222222", 3, 1, ACTIVE, ac)], rig.docs)
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "inactive_only.dds"])
    assert code == 0, out[-2000:]
    assert fatal_calls(out) == 1, out[-2000:]          # the behaviour first: under 4105's rule the texture was found
    assert mods_line(t).startswith("1 activated in "), mods_line(t)
    why = re.search(r"result: fatal hook code 5: (.*)", out).group(1)
    assert "texture 'inactive_only.dds' is not in the install or the mods' folders" in why, why
    assert traced_bakes(t) == [] and decal_totals(t)["sets refused by names"] == 1


def test_no_activated_mod_means_the_install_only(rig, tap):
    """Both mods inactive: no folder above the install; the reference resolves through the install's farm_types.xml (to
    stock_only.dds, which the install lacks: refused, fatal once), the active mod's texture is not found."""
    ia, ac = two_mods(rig)
    mods_db(rig.db, [("11111111-1111-1111-1111-111111111111", 1, 0, INACTIVE, ia),
                     ("22222222-2222-2222-2222-222222222222", 3, 0, ACTIVE, ac)], rig.docs)
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/1/albedo"])
    assert code == 0, out[-2000:]
    assert re.match(r"0 activated in .*; 0 folders above the install: \(none\)$", mods_line(t)), mods_line(t)
    assert fatal_calls(out) == 1, out[-2000:]
    why = re.search(r"result: fatal hook code 5: (.*)", out).group(1)
    assert "texture 'stock_only.dds' (from '@farm_types.xml/Crops_Africa/1/albedo') is not in the install" in why, why


def test_copied_database_paths_overflow_pages_and_missing_folders(rig, tap):
    """The reader on a database the size of the game's (1,024-byte pages, ModFiles over 500 rows: interior pages) whose
    EvaluatedPaths name another Documents folder (a copied instance: re-rooted under this MODS); the active mod's only row
    has a 3,000-character path (an overflow chain: its folder is found only if the chain is read right); a non-ASCII
    folder name; an activated mod whose folder is gone and one outside MODS (a DLC scenario) count as without a folder."""
    ia, ac = two_mods(rig)
    long_rel = "Deep/" + "x" * 3000 + ".txt"   # a database row only (no such file: too long for a file name)
    uni = "(3) Mod \u00e9t\u00e9"
    mod_folder(rig, uni, {"Art/uni_only.dds": b"x"})
    other_docs = Path("Q:/elsewhere/Documents")
    mods_db(rig.db, [("11111111-1111-1111-1111-111111111111", 1, 0, INACTIVE, ia),
                     ("22222222-2222-2222-2222-222222222222", 3, 1, ACTIVE, [long_rel]),
                     ("33333333-3333-3333-3333-333333333333", 2, 1, uni, ["Art/uni_only.dds"]),
                     ("44444444-4444-4444-4444-444444444444", 1, 1, "(4) Gone", ["gone.modinfo"]),
                     ("55555555-5555-5555-5555-555555555555", 1, 1, r"C:\Game\Assets\DLC\DLC_01\Scenarios\Mongol",
                      ["Mongol Scenario.modinfo"])], other_docs, filler=600)
    assert rig.db.stat().st_size > 40 * 1024
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/1/albedo", "uni_only.dds"],
                           wait=r"decals: bake \d+ tile \(1, 1\)")
    assert code == 0, out[-2000:]
    line = mods_line(t)
    assert re.match(r"4 activated in .*, 2 without a folder under MODS; 2 folders above the install: "
                    r"\(2\) Active Mod; \(3\) Mod \u00e9t\u00e9$", line), line
    assert fatal_calls(out) == 0 and decal_loud(t) == [], decal_loud(t)


@pytest.mark.parametrize("case", ["missing", "not_sqlite", "truncated", "journal", "no_mods_table"])
def test_an_unreadable_database_falls_back_to_every_folder(rig, tap, case):
    """No database, bytes that are not SQLite, a database cut short (its pages past the end), a -journal beside it (a
    write of the game in progress), a database without the Mods table: the log says why and every folder of MODS counts
    (4105's rule), so the inactive mod's texture is found; no crash, no fatal, exit 0."""
    ia, ac = two_mods(rig)
    mods = [("11111111-1111-1111-1111-111111111111", 1, 0, INACTIVE, ia),
            ("22222222-2222-2222-2222-222222222222", 3, 1, ACTIVE, ac)]
    want = {"missing": "cannot be opened (error 2)", "not_sqlite": "not an SQLite 3 file",
            "truncated": "outside the file", "journal": "a -journal or -wal file beside it",
            "no_mods_table": "no Mods or ModFiles table"}[case]
    if case == "not_sqlite":
        rig.db.write_bytes(b"this is not a database" * 100)
    elif case in ("truncated", "journal"):
        mods_db(rig.db, mods, rig.docs, filler=600)
        if case == "truncated":
            b = rig.db.read_bytes()
            rig.db.write_bytes(b[:4096])
        else:
            Path(str(rig.db) + "-journal").write_bytes(b"\xd9\xd5\x05\xf9\x20\xa1\x63\xd7" + b"\0" * 504)
    elif case == "no_mods_table":
        c = sqlite3.connect(str(rig.db))
        c.execute("CREATE TABLE Other(a, b)")
        c.commit()
        c.close()
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "inactive_only.dds"],
                           wait=r"decals: bake \d+ tile \(1, 1\)")
    assert code == 0, out[-2000:]
    line = mods_line(t)
    assert line.startswith("the activated mods are not known (") and want in line, line
    assert line.endswith("every folder of MODS counts, 2 folders (the rule before issue 0348)"), line
    assert fatal_calls(out) == 0 and decal_loud(t) == [], decal_loud(t)


def test_no_mods_folder(rig, tap):
    """No MODS folder at all: one line says so, the install's own files only (a mod's texture name is refused)."""
    rig.mods.rmdir()
    code, out, t = run_set(rig, tap, ["RoadsandRails_new_D.dds", "crops_maize_d.dds"])
    assert code == 0, out[-2000:]
    assert mods_line(t).startswith("no MODS folder at '"), mods_line(t)
    assert fatal_calls(out) == 1, out[-2000:]
