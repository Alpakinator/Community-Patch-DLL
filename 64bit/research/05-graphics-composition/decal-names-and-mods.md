# Decal names and textures: only the activated mods' folders (issue 0348)

Issue 0348 (from epic 0166's 4105, lead-0166's proposal #1517), 2026-11-16, branch `task-0348-decal-names-active-mods`.
Code: `server/src/render/srv_residency_source.{h,cpp}` (`ModFolders`, `ReadActiveModFolders`). Test:
`server/tests/render/test_decal_names_mods.py`. This file closes the open point of `terrain-decals.md` 12.2 ("[I] open:
every mod folder counts, active or not"): since 0348 only the activated mods' folders count (that 12.2 line is history;
the MC adds the one-line trace there at the merge, the file is outside this ticket's `touches`).

## 1. The rule

The decal pass's file view (`ResidencySourceConfig::decalLooseRoots`, terrain-decals.md 12.2: the install with mod
folders as extra loose roots above it, for `farm_types.xml` and the decal textures) takes **the folders of the mods the
game has activated**, not every folder under `MODS`.

- **Source of the activated set [C]:** the game's own record, `<Documents>\My Games\Sid Meier's Civilization 5\cache\
  Civ5ModsDatabase.db` (the folder of config.ini; Documents from `C5_DOCUMENTS` for a harness instance, else
  `SHGetFolderPathW(CSIDL_PERSONAL)`, as before). The same file the proxy's error report reads (`proxy_report_mods.cpp`,
  proxy-design.md 22.2). Table `Mods`, rows with `Activated = 1`. The proxy does not pass the list to the server; the
  server reads the file itself.
- **A mod's folder [C, from the schema and a copy of the workstation's database]:** `Mods` has no path column. Each mod's
  `ModFiles` rows (key `ModID`, `Version`) carry `Path` (relative to the mod's folder, forward slashes) and
  `EvaluatedPath` = `<mod folder>\<Path>` (absolute; backslashes up to the folder, the path's own slashes after it). The
  folder is `EvaluatedPath` without `\<Path>`; the `.modinfo` is itself a `ModFiles` row, so this is the folder of the
  mod's `.modinfo` (for a nested mod such as Continental Drift, `MODS\Continental Drift (v 1)\Continental Drift (v 1)`, the
  inner folder). `Mods.Version` is TEXT ('151'), `ModFiles.Version` INTEGER (151): compared as decimal text.
- **Re-rooting:** the folder's part after `...\Sid Meier's Civilization 5\MODS\` (or after this session's MODS root) is
  joined to this session's MODS root. A harness instance's database names the instance's own Documents
  (`%USERPROFILE%\c5-instances\c5h-a\Documents\...`, checked 2026-11-16 on a copy) [C], but a copied instance or a moved
  Documents folder could carry the source's paths; re-rooting makes both work. A mod whose folder is outside any MODS
  folder (the install's DLC scenarios, `Assets\DLC\...\Scenarios`, are `Mods` rows too) or no longer exists is counted
  as "without a folder" and adds nothing (their files are the install's, already in the view).
- **Order:** the activated folders sorted by name, each once (as 4105 sorted every folder). Earlier roots win
  (`gamefs.h`), so two activated mods with the same file name resolve by folder name, not by the game's activation order
  `[I]`: unchanged from 4105, not compared with the game.
- **Not modelled:** `ModFiles.Import`. The game's file system takes a mod's files with `import="1"` (614 of 2,437 rows on
  c5h-a, 718 of 3,249 on the workstation have `Import = 0`, 2026-11-16) [C]; the view takes every file of an activated
  mod's folder. VP's `farm_types.xml` and its crop textures are imported (`Import = 1`) [C], so the decals see the same
  files; a non-imported file of an activated mod with a decal texture's name would still be found `[I]`.

## 2. The reader

The render units of server.exe have no SQLite (no DLL include path, no database library: `server/build_server.py`
render_compile_args). `srv_residency_source.cpp` carries a small read-only reader of the SQLite 3 file format, written
from sqlite.org's format description (fileformat2 sections 1.3, 1.6, 2.1, 2.3): the 100-byte header (page size, reserved
bytes, UTF-8 only), table b-trees (interior 0x05, leaf 0x0D, in order), cell payloads with overflow chains (local size
by the format's X/M/K rule), the record format (serial types 0-9, text, blob), and the column names of a `CREATE TABLE`
(quoted names, table constraints skipped). Refused: another encoding, a WAL-mode header, an index page in a table tree,
WITHOUT ROWID, any offset past a page or the file, a loop or a depth above 40, a payload above 64 MB.

The file is read whole with `CreateFileW(GENERIC_READ, share read|write|delete)`, never written. A `-journal` or `-wal`
file of non-zero size beside it means a write of the game is in progress or was cut off: the file is not used then.
The workstation's database (copied, 2026-11-16): 1,545,216 bytes, page size 1,024, journal mode DELETE, schema version 14
(`ModSystemSettings.Version`), 38 `Mods` rows, 8 activated (CP, VP, EUI compatibility, Squads, Quick Turns, two of
Vern's, Wake All) [C].

## 3. Fallbacks and the log

One `render.log` / `server.log` line per call of `ModFolders` (the scene's residency init, and the render host's recorder
configuration when it records), always starting `decals: mods:`:

| case | line | folders |
|---|---|---|
| database read | `decals: mods: N activated in '<db>' (read in X ms), K without a folder under MODS; M folders above the install: <names>` | the activated ones |
| database missing, unreadable, not SQLite, cut short, no Mods/ModFiles table, a -journal/-wal beside it | `decals: mods: the activated mods are not known ('<db>': <why>): every folder of MODS counts, M folders (the rule before issue 0348)` | every folder (4105) |
| no MODS folder | `decals: mods: no MODS folder at '<root>': the install's own files only` | none |
| no Documents | `decals: mods: no Documents folder: the install's own files only` | none |

The fallback keeps 4105's behaviour (the game cannot run with mods without the database, so in the product it means a
damaged or busy file); it is a plain line, not LOUD and not an install failure. Activated mods = 0 gives an empty list:
the install's own view (`m_decalFsOpen` false), as with no MODS folder.

## 4. Tests [L offline, 2026-11-16]

`server/tests/render/test_decal_names_mods.py` (the rig of `test_decal_wiring.py`; fake MODS trees and fake databases
written with Python's sqlite3 in the game's schema at page size 1,024; never the owner's MODS or database):

- the active mod's `farm_types.xml` resolves the reference while an inactive mod that sorts first has its own (4105 would
  have read the inactive one); the line names 1 activated, 1 folder;
- an inactive mod's texture is not found: the set is refused (install failure, fatal hook once);
- no activated mod: the install's view only;
- a 600-row `ModFiles` (interior pages), a 3,000-character `Path` (overflow chain: the folder is found only when the
  chain is read right), EvaluatedPaths naming another Documents (re-rooted), a non-ASCII folder name (UTF-8), an activated
  mod whose folder is gone and one outside MODS (counted, 2 without a folder);
- fallbacks with the reason in the line, exit 0, no fatal: no database, not SQLite, cut at 4 KB, a `-journal` beside it,
  no Mods table; no MODS folder.

Results: section 5.

## 5. Results [L offline, 2026-11-16, workstation]

Build: `python -I server/tests/render/build_render_tests.py --out C:/c5-64bit-work/0348/rt` (clang-cl, /W3 /WX) at this
branch; run: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 C5_RENDER_TEST_BUILD=C:/c5-64bit-work/0348/rt/bin python -I -m pytest -q
server/tests/render/test_decal_names_mods.py server/tests/render/test_decal_wiring.py` through `quietlock.py run`.

- `test_decal_names_mods.py` **10/10** (04:30 and 04:36 EST). `test_decal_wiring.py` 16/17 at 04:36, the one failure
  `test_unresolvable_name_is_an_install_failure[no_attribute]` with "LOUD: the render thread did not stop within 5 s", so no
  `decal totals` line was written (the set had been refused and the fatal hook called once, as the test wants); the
  three cases of that test **3/3** on a rerun at 04:38. A stop-time flake on the loaded workstation (13 heavy jobs of other
  owners listed by `quietlock.py check` at 04:14), not a mods question `[I]`.
- **Mutation check:** the same test file against a build of the base commit 17674f2c (4105's rule, every folder):
  **10/10 fail**; the two behavioural ones fail on behaviour, not only on the new log line: the farm reference is resolved
  through the inactive mod's `farm_types.xml` (its folder sorts first), and the inactive mod's texture is found (fatal
  hook 0, the test wants 1).
- **Game-written databases** (a scratch test, not committed: copies in a fake Documents with an empty folder per activated
  mod): the workstation's copy (1,545,216 B) **8 activated, 0 without a folder, 8 folders, read in 11.1 ms**; c5h-a's copy
  (1,206,272 B) **2 activated (CP, VP), 0 without, read in 4.6 ms**. The reader takes both the files the game wrote and
  those Python's sqlite3 (the tests' writer) writes.
- **ASan** (COMMON 9): `build_render_tests.py --out C:/c5-64bit-work/0348/rt --asan` (MSVC `/fsanitize=address`), the
  same test file on `rt/bin-asan` at 04:42: **10/10, no ASan report** (the conftest's `no_asan_reports` fixture; the log
  folder `rt/asan-logs` empty).

## 6. Not verified

- A live game: no game was run (the ticket's `needs: none`). The real `Documents` path of a player and a database the EXE
  holds open while the server reads it (the read shares read, write and delete; the game writes the file at the mods menu,
  not in play `[I]`).
- Which of two activated mods wins for the same file name in the game (section 1, "Order").
- `ModFiles.Import` (section 1, "Not modelled").
