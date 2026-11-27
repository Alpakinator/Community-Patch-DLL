# The clean build of an export (ticket 4706, package E5 of the export plan, 2026-11-25)

Plan 4.6 of `research/07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md`: every export is accepted
by a clean build, an environment that never held this repository building the release binaries from the export alone
by one documented command. This file holds the command, the first trial and what it found. The export script and the
private-content check are in `upstream-export.md` (sections 1-3); this file does not repeat them. Marks: `[C]` read or
measured in files, `[L]` a live build, `[I]` inferred.

## 1. Result

- **One command builds the whole release set from an export plus the DLL source beside it** [L, 2026-11-25
  17:46-18:09 EST, 23 min]: `python -I tools/export/cleanbuild.py ...` in `64bit/` of a fresh folder that never held
  this repository (a plain copy of the DLL pin 1b87dd185 with the export of d321c2f29 inside), no settings file, no
  download. The 12 files of the release layout came out; the file set equals the product build's.
- **It needed inputs the source cannot carry, allow-list entries and one product fix**: both EXE builds and
  the local hand answers of build 26577625 for the address tables, a gameplay database for the DLL's smoke run
  (section 2); seven paths the allow-list keeps out (section 3, three TRIAL ONLY commits for the MC to drop or make
  permanent); and **one product bug**: `server/build_server.py` refused every build in the upstream layout (fixed here,
  section 4).
- **By function the clean build equals the product build**: one headless tiny game per arm, D10 PASS on both, and
  `statscmp.py` IDENTICAL over 15 tables and 30,569 rows; in the export tree the options suite and the units suite
  (7/7) pass, the render suite builds all 109 tests and passed the 17 it ran before I stopped it on an F:-bound test
  (section 5). **By bytes**: Lua equal; the other libraries, civ5save and the helper equal up to the 20-byte
  `/Brepro` stamp; with `SOURCE_DATE_EPOCH` matched, the DLL's code is byte-identical to the shipped pin DLL (28
  bytes differ: the stamp and one RTTI name) (section 5).
- **Private paths**: none in the shipped binaries but one: `shell_composite.dll` names its PDB by absolute path, a
  new finding (9076 had not searched it); the two PDB leaks of upstream-export.md 1.4 remain (section 6).
- Not run on the laptop (no compiler, too little commit for the LTO link; section 7). `deps/local-settings.ini` stays
  committed, with a new `.example` (section 8). The builders' hard-coded toolchain folders are listed (section 9).

## 2. The one command (BUILDING.md, `tools/export/cleanbuild.py`) [C: code, tests; L: the trial of section 4]

`BUILDING.md` (repository root, product group) lists the prerequisites with versions and folders, the inputs taken
from the builder's own game, the folder layout and the command. `tools/export/cleanbuild.py` (product group; the rest
of `tools/export/` stays private) is the command, run in `64bit/`:

```
python -I tools/export/cleanbuild.py --out <folder> --exe <DX11 EXE of 4390913> --exe <DX11 EXE of 26577625>
       --db <Civ5DebugDatabase.db> [--hand-review 26577625=<hand-review.tsv>] [--third-party <folder>] [--offline]
       [--src <DLL source, default ..>] [--repo-commit SHA] [--unlocked] [--no-composite] [--check-only]
```

Steps: (1) the toolchain files at the folders the builders open (13 files, LLVM version 19.1.7, Python 3.11+): every
missing one named, then stop; (2) the EXE tables into `<out>/exe-tables` (`gen_table.py --identity` on the anchor
EXE, then one carry per other known build; the hand answers copied in first); (3) `server/build_server.py --config
release` (the DLL and its three libraries by `build_all.py`, server.exe, civ5save.exe, the proxy and the helper);
(4) `shell/build.cmd` for `shell_composite.dll`; (5) the layout `<out>/release` with `SHA256.txt` in the release
driver's format. Children get `C5_EXE_TABLES`, `CV64_DEPS_CACHE` (the third-party folder), `C5_PURITY_TEST_CACHE`,
`VP_PROXY_GEN_WORK` and `C5_RELEASE_TEST_DB` under `--out` or from the arguments, `C5_SETTINGS_FILE` naming an absent
file, and none of `VP_DLL_SRC`, `VP_DLL_FORK`, `VP_PROXY_GEN_*`, `C5_SHELL_BUILD`: no machine setting, no shared cache.
Tests: `tools/export/tests/test_cleanbuild.py`, 13 tests, hermetic (no compiler).

**Inputs a builder needs that the source cannot carry** (each found by the trial, each in BUILDING.md):

- **Both EXE builds.** `emit_header.py` refuses unless every build of `VPEXE_KNOWN_BUILDS` has a table, and the
  carry to 26577625 is made from the anchor 4390913 (`gen_table.py --old` must be the anchor). A player has only one
  install, today 26577625. [C: emit_header.py, gen_table.py; L: the trial]
- **The hand answers for build 26577625.** Without `hand-review.tsv` the carry refuses (exit 1): 15 BAD patterns
  whose addresses the matcher cannot carry (16 BAD lines in the log; `bake_ts_site` first) [L 2026-11-25 17:05, the
  golden copies]. The workstation's file has 31 answers; D202 keeps it local (it holds 26577625 VAs), so an upstream
  builder cannot build for 26577625 today. With it, both tables are complete and equal to the workstation's tables in
  every field but the `generated` time stamp [L 2026-11-25 17:12, field-by-field comparison by name, no value printed].
- **The smoke database.** `build_all.py`'s smoke run reads a Civ5DebugDatabase.db (default: the release descriptor's
  `test_db`, `tools/release/releases/` is `[unclear]` and absent from the export): `--db`, passed as
  `C5_RELEASE_TEST_DB`. The trial used the descriptor's pinned copy (`civ5-headless pinned/vp-modpack-5.4.8-cdrift`,
  SHA-256 `bd9b8aa1...`), the same file the product build's smoke reads.

## 3. What the export lacked: the TRIAL ONLY allow-list entries, each a decision for the owner and the MC [C]

**Current state (2026-11-25 19:50 EST, ticket 0776; this replaces "for the MC to drop" below):** the entries are
permanent on `tools/export/allowlist.txt` `[product]` by the owner's D263, all but one: `exebuild/include/`,
`tools/exebuild/` (`evidence/` private), `proxy/model/`, the five purity files, and under a comment naming D263 as a D83
exception for interoperability data `exebuild/symbols/*.tsv` and `exebuild/patterns.txt` (the sources it names are all
in `shell/common/`, `shell/composite/`, `proxy/src/`, product already); by lead-0763's call (listed in
`tools/export/drafts/owner-read.md` point 6) `render/tools/`, `shell/standin/`, `shell/tools/memcap.py`. The rest of
`proxy/tools/` and `shell/tools/` is private by omission (no longer `[unclear]`). **`deps/dll.lock` stays `[unclear]`**
until ticket 0773 (after 0765 and 0774 rewrite it without user paths and 0775 adds the check's exception for the fork's
address; lead-0763 #15788), so the export still lacks the one file the release build reads at four places. Product
file set of the branch = the trial's (d321c2f29) minus `deps/dll.lock` [C: `check.py --list --groups product`, 1,443
against 1,444 files]. Findings measured **with** environment terms (the table below counted without): 24 new in 9 files
(13 `deps/dll.lock`, 10 `render/tools/` in 7 files, 1 `shell/standin/`), the 11 outside dll.lock cleaned at the source
(origin comment as "the project's fork"; default paths from the user folder, `USERPROFILE` or `~`; `oracle.py` calls
the alias `exact_t.ArtFiles`, one line in `models/tools/formations/exact_t.py`). Gate `check.py --rev HEAD --since
origin/main --require-terms`: **PASS, 0 new** [L 2026-11-25]; the full product check finds none in these entries
(its 72 in 38 files are the base's, mostly the owner's first name for 0773). **Build 26577625 in `exebuild/`** [C]:
of the 796 values in the local 26577625 tables that are not also 4390913 values, none is in `exebuild/` as an address
(4 equal numbers are 4390913 offsets or function heads in notes); `exebuild/include/vp_exe_build.h` carries its build
identity (stamp, SizeOfImage), not an address (lead-0763: fine). No TRIAL ONLY line remains in the allow-list.
**Dry run** [L 2026-11-25 20:05-20:10 EST, branch impl-0776 fd50ff445]: `tools/export/export.py --target <scratch> --rev
HEAD --groups product --dry-run --allow-findings` wrote 1,443 files (41.4 MB; the marker because the full check with
environment terms has the base's findings); against the trial export's set: every file of it but `deps/dll.lock`, no
extra file, no `TRIAL ONLY` text in any exported file. In that `64bit/`, `tools/export/cleanbuild.py --check-only`
(the two golden EXEs, the local hand answers, `--src` the read-only pin copy of section 4, `--offline`, under
`quietlock.py run`): **`OK (check only)` in 57 s**, 13 toolchain files, both tables `complete` (4390913 1 s, 26577625
56 s). Not run: the build steps 3-5 (the full build without `deps/dll.lock` is 0773's to try once the lock goes).

A dry export of `main` 83ac9a13b (2026-11-25 16:45, `--groups product`, 1,340 files) holds every product source but
not every file the four builders open. Found by reading the builders' path joins (a scratch scan of the export copy
against the checkout) and confirmed by the trial of section 4. lead-0763 allowed them for the trial (#15356, Q2) in one
commit whose message starts `TRIAL ONLY`, for the MC to drop:

| Entry | Who needs it | What it carries (the decision) |
|---|---|---|
| `exebuild/` (`include/*.h`, `symbols/*.tsv`, `patterns.txt`) | every product unit includes `exebuild/include`; `emit_header.py` and `gen_table.py` read the catalogue | the symbol catalogue: 531 rows with the **anchor VAs of EXE build 4390913**, the build identities (stamp, SizeOfImage), the list of source files that hold **EXE byte patterns** (the patterns themselves are already in allow-listed `shell/composite/*.cpp` and `shell/common/exe_camera.h`). **Decided by the owner on 2026-11-25 (D263, relayed by MC-B #15622): the export carries the 4390913 address tables and byte patterns as interoperability data**; the allow-list entry itself is still the MC's to make permanent |
| `tools/exebuild/` (generator, emitter, table reader, tests; `evidence/` stays private) | `emit_header.py` is step 0 of every builder; `gen_table.py` makes the tables from the builder's EXE | code only; prints no address |
| `deps/dll.lock` (was `[unclear]`) | `tools/x64-dll/build.py resolve_src`, `proxy/gen/toolchain.py resolve_source`, `proxy/gen/build_info.py` | the DLL pin (commit, describe, source fingerprint), **the fork's address** (`repository =`), and 10 export-check findings: 9 user-folder paths in the `release*` and `profile-file` lines, 1 the fork owner's handle in the address |
| `proxy/model/` (17 MB; was `[unclear]`) | `build_proxy.py` step 0 (`gen_pure_list.py --check`) and step 7 (the purity test) | **generated model JSON** of the DLL's interfaces (`proxy/gen/extract_model.py` from the DLL headers, plan 4.3), the audited purity lists |
| `proxy/tools/{derive_purity,gen_pure_list,test_purity,shell_manifest}.py`, `proxy/tools/rt_tests/purity_unit.cpp` (`proxy/tools/` was `[unclear]`) | steps 0 and 7 of `build_proxy.py`; `shell_manifest.py` writes the compositor's `.sha256` in the layout | code; the rest of `proxy/tools/` stays out (private by omission while the trial commit stands) |
| `shell/tools/memcap.py` (`shell/tools/` was `[unclear]`) | `test_purity.py` runs its test programs under it | code |
| `render/tools/` (16 tools; was `[unclear]`; second TRIAL ONLY commit) | `render/CMakeLists.txt` adds every `tools/<tool>/`, and `tests/bake`, `tests/bakelayers` depend on the targets `bakecompare`, `bakelayers_check` (CMake configure fails without them) | code; 8 export-check findings (user-folder paths in 7 files: `quadtest.cpp` 2, `terrainreplay/README.md`, `terrainshot/offline_all.py`, `unitanim/oracle_anim.py`, `unitanim/test_oracle_anim.py`, `unitscene/oracle.py`, `unitscene/oracle_draws.py`) |
| `shell/standin/` (was `[unclear]`; third TRIAL ONLY commit) | `shell/build.cmd` compiles `standin/test_host.cpp` into its test host after the compositor and fails the build without it | code; 1 finding (the owner's handle in an origin comment, line 1, the form 0769 handles) |

With all three TRIAL ONLY commits the merge gate on the branch reports **19 new findings in 9 files**, all in these
entries; without them (the allow-list of the permanent commits only) it **PASSes with 0 new findings** [L 2026-11-25
18:45, `check.py --rev HEAD --since origin/main --require-terms --no-env-terms`, with and without `--allowlist` of a
copy lacking the trial lines]. So a real export with these entries needs those 19 cleaned at the source first.

Optional (not added): `perf/quietlock/` (gate.py, linkplace.py). Without it the builders print a warning and run
without the quiet-machine gate and **without a whole-program link place**; on the workstation the trial therefore
held one link place itself for the whole build (section 4). Found but outside the release set: the product file
`tools/release/stages/build.py` names `tools/d10/evidence/*/ia-*.patch` (private) and `proxy/tools/shell_manifest.py`,
so the exported release driver cannot run a take as it stands (not needed by the one command).

**`deps/dll.lock` in the upstream layout (lead-0763's question).** Plan 4.5 says upstream needs no pin, since the DLL
source sits next to the folder. The lock is read at four places in the release build: `build.py resolve_src`
(commit, describe, fingerprint), `toolchain.resolve_source` (the same for the proxy's model chain),
`build_info._lock_commit` (the DLL pin field of the build id) and `build_proxy.py` step 7 (skipped off the pin). Making
it optional is small in code [I: about 40 lines in the three files plus 4-6 tests, 1-2 h]: with no lock file, accept
the source as "upstream layout" (describe from the DLL repository's git, else `unknown`) and record no pin. What it
gives up is the one check that `proxy/model/` was generated from these DLL headers: the model is generated from the
pinned source and committed, so upstream would need either the lock (as a record of the model's source) or the
model regenerated at build time (`proxy/gen/regen_model.py`, minutes). Until then the build of a DLL source at another
commit needs `--unlocked`, and the proxy's purity test does not run for it.

## 4. The trial on the workstation (plan 4.6's weaker form) [L, 2026-11-25]

Where: lead-0763 (#15356) moved the trial from the laptop (section 7) to a fresh folder on the workstation, outside
every repository and worktree; C: by MC-B's ruling #15474 (F: wrote at 1.8 MB/s that hour under the 5.5 autoplays).
The DLL source: lead-0763 Q3 (ii), the pin of `main` (`deps/dll.lock` x64-0.1b `1b87dd185`, Release-5.4.8) as a plain
copy, not 0762's `export-e5-dll` (58a8ce863, on Release-5.5: `main`'s lock, proxy model and purity lists are 5.4.8's
until 0765 lands, so like with like needs the pin; the 5.5 re-run on C5 is 4707's).

**The folder.** `C:/c5-64bit-work/4706/clean-1748/`: `cp -r` of the shared read-only pin copy
`C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185` (a `git archive` of 1b87dd185, 3,069 files, 396 MB; no `.git`),
then `tools/export/export.py --target <it> --rev d321c2f29 --groups product --dry-run --allow-findings
--no-env-terms` into its `64bit/` (1,444 files, 41.4 MB; the full check FAILs with 23 findings in 12 files: the 4 known on
`main` in 3 files (0761's notes in upstream-export.md section 14) and the 19 of the trial entries (section 3), so the
copy carries the not-an-export marker). d321c2f29 is
`main` 83ac9a13b plus this branch (cleanbuild.py, BUILDING.md, the guard fix, the three TRIAL ONLY commits). No
settings file, no `deps/dll-source.ini` (the DLL source is `..` by the upstream default); `perf/` absent.

**The command**, in `64bit/`, under `quietlock.py run` and one whole-program link place held by a wrapper for the
whole build (the export has no `perf/quietlock`, so the builders would link without a place, D121):

```
python -I tools/export/cleanbuild.py --out C:/c5-64bit-work/4706/clean-1748-out --exe <golden 4390913 DX11 EXE>
  --exe <golden 26577625 DX11 EXE> --db <the 5.4.8 descriptor's pinned Civ5DebugDatabase.db>
  --hand-review 26577625=<exe_tables>/26577625/hand-review.tsv --third-party C:/c5-64bit-work/4706/third-party
  --offline --repo-commit d321c2f291d2da93ffa08c4c20f5b04899fc3308
```

The EXEs are the read-only golden copies `F:/c5-64bit-work/golden/civ5-steam-build{4390913,26577625}-ws`; the
third-party folder held the three archives of `deps/third-party.lock`, hashes as locked.

**Result: `cleanbuild: OK` in 1,385 s (17:46:04-18:09:05 EST), the whole release set from the one command** [L]:

| Step | Time | Note |
|---|---|---|
| prerequisites | 0 s | 13 toolchain files, LLVM 19.1.7, Python 3.11.5 (Anaconda's, the workstation's `python`) |
| tables 4390913 / 26577625 | 1 s / 52 s | both complete; equal to the workstation's tables but the time stamp (section 2) |
| server (`build_server.py --config release`) | 1,189 s | build_all: stubs 24 s, database 36 s, localization 15 s, DLL 607 s (compile 84 s, LTO link 482 s, link peak 13,361 MB private), smoke host 45 s, smoke 14 s; server.exe and civ5save.exe; proxy 207 s with the full purity test 66/66 (114 s) |
| composite (`shell/build.cmd`) | 137 s | `BUILD OK` |
| layout | 0 s | 12 `.dll`/`.exe`/notice files in SHA256.txt, 242 MB with PDBs |

Nothing was downloaded (`--offline`; the archives from the named folder). Writes outside `--out`: the `__pycache__`
folders of the build scripts in `64bit/` (the export's next run removed 17 such files) [L]; `%TEMP%` not watched [not
verified]. The output folder holds 1.5 GB.

**What it took to get there** (the first run, `clean-1711`, 17:11-17:25, and two pre-checks on its finished DLL):

1. The carry to 26577625 refused without the hand answers (section 2): `--hand-review` added.
2. build_all, the DLL and the libraries passed first time (13 min); the server step stopped in `render/build.py`'s CMake
   configure: `tests/bake` and `tests/bakelayers` depend on the targets `bakecompare` and `bakelayers_check` of
   `render/tools/`, which the export lacked (`[unclear]`): the second TRIAL ONLY commit.
3. `build_server.py` then refused with "internal: the render units would see the DLL fork source" for every build in
   the upstream layout: its guard was `any(str(src) in arg for arg in render_compile_args)`, and with this folder as
   `64bit/` inside the DLL source every render include path contains the source folder. **A product bug**: the export
   could never build `server.exe` where upstream puts it. Fixed on this branch (lead-0763 widened the touches, #15522):
   `render_args_see_dll()` looks for the DLL's include folders (`x64build.INCLUDE_DIRS` under the source, case and
   slash insensitive), so the guard keeps its purpose in both layouts; `server/tests/test_render_args_upstream.py`
   (4 tests: both layouts pass, a DLL header folder is caught in both, in another case and with forward slashes;
   the upstream test asserts that the old substring test would have refused it).
4. `shell/build.cmd` built `shell_composite.dll` but then failed on `shell/standin/test_host.cpp` (`[unclear]`): the
   third TRIAL ONLY commit. The script builds its test programs with the product and has no product-only mode.
5. The final run above, in a new folder, from d321c2f29, passed every step in one go.

`main`'s builders also print `[build id] LOUD: ... has no .git and no --repo-commit` for a copy without git: in the
upstream layout git sees the DLL repository (the parent), so the build id there names the DLL repository's commit
[I]; the trial passed `--repo-commit`, as the release driver does for its archives.

## 5. Comparison with the workstation's product build [L]

**The product build** (17:57-18:08 EST, the normal case of runbook 1.2 and 1.5, from this branch's worktree at the
same commit d321c2f29, `deps/local-settings.ini` and git present): `server/build_server.py --src <keep pin source>
--deps C:/c5-64bit-work/keep/pins-0.1b/deps-x64-release --config release` (282 s; its DLL and libraries are the
shared pin build that 0616 made on 2026-11-20 with the tools of that day) and `shell/build.cmd`, laid out by the same
`cleanbuild.layout()`. Its build id says `repo_dirty=1` (the research README was being edited) [L].

**File set: equal.** The same 12 files in `SHA256.txt` and the same 24 files with PDBs, `build.json`,
`build-proxy.json` [L, `evidence/4706/sets-clean-vs-product.txt`].

**Bytes** [L, `evidence/4706/bytediff.py`, per PE section]:

| File | Equal? | Where they differ, and why |
|---|---|---|
| `lua51_x64.dll` (+ PDB) | **byte-equal** | built only from the locked Lua tarball |
| `CvGameDatabase_x64.dll`, `civ5save.exe`, `vp64helper.exe` | equal but **20 bytes**: the PE time stamp field and the debug directory's GUID and age | under `/Brepro` these are a hash of the output; the code and data sections are equal. The inputs that feed the hash and differ: the sources' line ends (the export writes git blobs with LF, the workstation checkout has CRLF under `core.autocrlf`, so the PDB's source checksums differ) [I: the mechanism; C: the 20 bytes] |
| `CvLocalization_x64.dll` | 268 bytes in 39 runs | the debug directory, and 7-byte runs in `.data`: the anonymous namespace's hash in RTTI names (`?A0x80256A3C@Localization` against `?A0x8BE27901@Localization`), which clang derives from the main file's name as given; the shared pin build is from the tools of 2026-11-20 [I: that the given name differs] |
| x64 DLL | 9,216 bytes in 2,607 runs, most of them single bytes in `.text` | `__DATE__`/`__TIME__`: `SOURCE_DATE_EPOCH` is the commit time of the tool repository's HEAD (`build.py source_date_epoch`), which a copy without `.git` lacks, so the export's DLL says `Jan  1 1970 00:00:00` and the pin build `Nov 20 2026 22:48:42` [C: the strings]; with the epoch matched, the `.text` is equal (below) |
| server.exe, the proxy DLL, `shell_composite.dll` | differ | they embed the build id (repository commit, dirty flag: `d321c2f29... (archive)` against `d321c2f29`, `repo_dirty=1`), and the compositor its PDB path (section 6) |
| `shell_composite.ini`, `MinHook-LICENSE.txt` | differ in size (19,950 / 20,197 B; 4,365 / 4,446 B) | LF from the export against CRLF from the checkout; the text is the same |

**The epoch-matched DLL: the export rebuilds the shipped pin DLL's code byte for byte** [L, 18:19-18:29 EST]. In the
same export folder, `tools/x64-dll/build_all.py --config release --offline` with `SOURCE_DATE_EPOCH=1795214922` (the
pin DLL's `__DATE__`/`__TIME__`, Nov 20 2026 22:48:42 UTC), one held link place (588 s; DLL 507 s): the DLL
(`d9c8a8c4...`) against the shared pin build (`b84aa3e9...`) differs in **28 bytes only**: the 20 bytes of the
`/Brepro` stamp and debug GUID, and 8 bytes of one RTTI type name in `.data` (`.?AVWbMemFile@?A0x96BAE5C@@` against
`.?AVWbMemFile@?A0x94EEB0B7@@`, the anonymous namespace hash again). `.text` and `.rdata` are equal. `lua51_x64.dll`
byte-equal, `CvGameDatabase_x64.dll` 20 bytes (stamp and GUID), `CvLocalization_x64.dll` the same 268 bytes as above.
So the remaining differences are two reproducibility details, both cheap to name: the epoch of a copy without git,
and the anonymous-namespace hash of a few translation units [I: which given file name differs; not traced].

The upstream layout has git (the DLL repository), so there `SOURCE_DATE_EPOCH` and the build id come from the DLL
repository's commit, not from this repository's: an upstream build of the same content is never byte-equal to ours
unless the epoch is passed (`SOURCE_DATE_EPOCH` in the environment is honoured) [I].

**By function: identical.** One headless short game per arm, one after the other on the workstation, 18:12-18:17 EST:
`tools/d10/run_d10.py --games tiny --allow-subset --srv-bin <arm>/release/bin --jobs 4` (Continents Tiny fixed seed,
to turn 100, the x86 arm the i0 pin `f33fc46a4`): both arms PASS all three D10 parts (writer identity, behaviour
identity, cross-load; 139 s each); and `tools/d10/statscmp.py` between the two arms' x64 direct plays: **15 tables,
30,569 rows: IDENTICAL** (like with like, D164: both headless, one machine, one input) [L].

Test suites in the export tree (`64bit/` of the trial, with the trial's EXE tables and no settings file; 18:17-18:42
EST) [L]:

| Suite | Command | Result |
|---|---|---|
| options | `python -I options/test_asan.py --out <dir>` | `test_asan: OK` (canary caught, every test passes; 67 MB peak) |
| units | `server/tests/units/build_units_tests.py --out <dir>`, then `test_units_runner.py` with `C5_UNITS_TEST_EXE`, `C5R_GAME_DIR`, `C5R_EXE` (the `render_game_dir`/`render_exe` copies, build 4390913) | build OK; **7 passed** in 170 s |
| render | `python render/build.py --test --build-dir <dir> --game-dir <copy> --exe <copy>` | configure and build of all 109 test targets OK; **17 of 17 run passed**, then **stopped by me** at test 18, `c5r_decal_test`, which reads its reference tiles from F: (`C5R_DECAL_REF` default under `F:/c5-64bit-work/0166/`) and had used 7 s of CPU in 11 minutes while the 5.5 autoplays held F:; the other 91 not run |

So the export tree builds every render test and the ones run pass; a full render suite needs the F: references (and
the D: tap the runbook names for `test_decal_wiring.py`), which an upstream checkout does not have either: those
tests are environment-bound, not export-bound [I].

## 6. Private paths in the produced binaries and PDBs [L]

upstream-export.md section 1's check: `research/06-tooling-and-workflows/evidence/9076/pdbcheck.py` (counts of the
user folder, machine name, work and repository folders, the placeholder names; also in UTF-16) over both release
sets, 2026-11-25 about 18:10 EST (`tools/export/evidence/4706/pdbcheck-*.txt`). The trial's folders are under `C:\c5-64bit-work`, so a build path that leaks shows as
`work-folder`; a builder working under the user folder would leak the user folder at the same places (the product
build, made from a worktree under the user folder, shows it).

| File | Clean build (export, `C:\c5-64bit-work\4706`) | Product build (worktree under the user folder) |
|---|---|---|
| x64 DLL, the three libraries, server.exe, civ5save.exe, proxy DLL, vp64helper.exe | **none** (placeholders `VPSRC` only) | none |
| **`shell_composite.dll` (shipped)** | **work-folder 1**: the absolute PDB path of its output folder in the CodeView record (`RSDS ... <out>\shell\bin\shell_composite.pdb`) | work-folder 1, the same |
| `server.pdb` | work-folder 15 | **user-folder 15**, repository folder 45 |
| `vp64helper.pdb` | work-folder 2 | work-folder 2 |
| `shell_composite.pdb` | work-folder 230 | **user-folder 140**, work-folder 90, repository folder 420 |
| other PDBs | none | none |

- **New: the shipped compositor carries its build folder** [C: bytes at file 0x1CC083]. `shell/build.cmd` links
  `shell_composite.dll` without `/PDBALTPATH:%_PDB%`, so the debug directory names the whole PDB path. 9076 did not
  search the compositor (upstream-export.md 1.6, "not searched"). The release take builds it the same way, so a
  released compositor names the take's work folder [I: the take's own DLL not opened here]. Fix (not in this ticket's
  files): `/PDBALTPATH:%_PDB%` on that link line, as `REPRO_LINK` does for the other builds; the composite's PDB also
  needs the compile-side mapping (`/pathmap` for cl.exe, which builds it) to lose the 140 user-folder hits.
- **The two PDB leaks of upstream-export.md 1.4 are still there, listed, not fixed** (their files are outside this
  ticket's touches; neither is cheap to prove without a rebuild of the server): `server.pdb`'s 15 are the module
  names of `c5r_models.lib` (CMake object folders named after the absolute path of `models/src`, the folder
  `C_\c5-64bit-work\4706\clean-1748\64bit\models\src` in the trial's build log), fix in `render/src/units/CMakeLists.txt`;
  `vp64helper.pdb`'s 2 are the helper's absolute `/Fo`, fix in `proxy/build_proxy.py` `build_helper`.
- No user folder, machine name or repository folder in any shipped binary of either build except the compositor's PDB
  path, whose value is the builder's output folder.

## 7. The laptop

**Not run there** (D258 named the laptop; lead-0763 #15356 moved the trial to the workstation): the laptop has no
compiler, and every prerequisite of BUILDING.md section 1 is an install with administrator rights at a fixed folder,
which AGENTS.md section 4 puts to the owner. lead-0763 asks the MC for the owner's word; it is not needed for beta 5.

What the laptop already has, from the records (no new probe; the laptop was not granted to this ticket) [L 2026-11-14,
`quiet-testbed.md` 21.2 and 21.4, `tools/testbed/hosts/laptop.json`]: Windows 10 Pro 22H2, i7-7700HQ (4 cores,
8 threads), **15.8 GB of memory with an 18.2 GB commit limit (8.8 GB free)**, C: 349.6 GB free; no compiler ("no
compilers; builds stay on the workstation"); the testbed's Python is the embeddable 3.12.10 (isolated, under the
testbed root, not on `PATH`); Debugging Tools 10.0.26100.1 and RenderDoc 1.47 as copied folders under the testbed root
(installed nothing); Steam with the owner's account and the game (build 4390913 when surveyed; its EXE is the owner's
Steam install and is never used, D200); Git not recorded. Plan 4.6 calls a clean folder on a test machine weaker than
a fresh runner because of what is installed; on the laptop the gap is the other way round: almost nothing is.

What a laptop run would need besides the installs [I]: **the DLL's whole-program link takes up to 13.3 GB of memory**
(runbook 1.8), above the laptop's free commit of 8.8 GB, so the link would fail or the page file must grow (a system
setting, the owner's call); and the two game EXEs, the hand answers and the smoke database of section 2 shipped as
files (the laptop's own Steam EXE may not be used).

## 8. `deps/local-settings.ini`: ignored file with a committed example, or not [C]

Answer (upstream-export.md section 4 left it to E5): **keep it committed here, and add a committed example**.

- The release set needs none of it: `cleanbuild.py` sets `C5_SETTINGS_FILE` to an absent file and passes what it
  needs (`C5_EXE_TABLES`, the smoke database) itself, and the trial built with no settings file at all (section 4).
- It is private already: `deps/` is not exported (only `deps/third-party.lock` and now the example are), so making it
  ignored adds nothing to the export's privacy.
- Making it ignored would break this repository's own workflow: every agent worktree is made from git and gets the
  file from there (22 tools and tests name it; `exe_tables`, `pins_dir`, `core_db` are read from it), so each new
  worktree would start without settings and every build would fail until someone copied the file in.
- The user-folder paths in it are base findings of the gate's repository pass (upstream-export.md 13), not new ones.

New: `deps/local-settings.example.ini` (product, on the allow-list): every key, its environment variable and an
example value, for an exported checkout's other tools (render tests, the release driver).

## 9. Hard-coded toolchain folders (for a later "toolchain folders from settings" ticket) [C]

lead-0763 (#15356): not changed here; listed for a later ticket. On `main` 83ac9a13b (2026-11-25, grep of the builders):

| File:line | Folder |
|---|---|
| `tools/x64-dll/build.py:33`, `:36` | `C:\Program Files\LLVM\bin`; VS 2022 Community `vcvars64.bat` |
| `tools/x64-dll/closedlib-stubs/build_stubs.py:56`, `:59` | the same two |
| `tools/x64-dll/cvgamedatabase/build_db.py:42`, `:46`, `:47` | LLVM; VS 2022 Community `vcvars64.bat` and `vcvars32.bat` |
| `tools/x64-dll/smoke-host/run_smoke.py:33` | LLVM `llvm-symbolizer.exe` |
| `tools/x64-dll/pgo/check_profile.py:31`, `pgo/train.py:31` | LLVM (release-pgo only) |
| `proxy/gen/toolchain.py:127`, `:128`, `:130`, `:131` | LLVM clang-cl and lld-link; VC++ 2008 `Common7\Tools\vsvars32.bat`; VS 2022 Community `vcvars64.bat` |
| `proxy/gen/extract_lua.py:53-56`, `proxy/gen/verify_slots.py:35-36` | the same (model chain, not the release build) |
| `proxy/build_proxy.py:82` | `C:\Program Files\Microsoft SDKs\Windows\v6.0A\bin\mt.exe` |
| `render/build.py:48-53`, `models/build.py:32-37`, `ipc/build.py:29-32` | LLVM (`VP_LLVM_BIN` overrides), VS 2022 Community, its CMake and Ninja, `C:\Program Files\CMake\bin`; ipc also VC9 (`VS90COMNTOOLS` overrides) |
| `render/cmake/clang-cl-x64.cmake:5` | LLVM |
| `shell/build.cmd:44`, `:46`; `shell/frame/build.cmd:19` | VS 2022 Community (`VSDIR` overrides), the SDK's `fxc.exe` (`SDKVER` overrides) |
| `tools/release/stages/build.py:63` | LLVM |

Only `render`, `models` and `ipc` (`VP_LLVM_BIN`) and the shell scripts (`VSDIR`) take an override, and not
consistently; the DLL, stubs, database and proxy builds take none. A Build Tools install
(`...\2022\BuildTools\...`), another drive, or an LLVM installed elsewhere is not found.

## 10. Not verified

- No run on the laptop, nor on any machine without the toolchain already installed: the prerequisite list is the
  builders' own folders and the workstation's versions, not an install tested from nothing. Whether Python packages
  beyond the standard library are needed: the trial ran with Anaconda's Python 3.11.5 (many packages installed); the
  builders import none that I found, but a plain python.org install was not tried.
- Visual C++ 2008 Express SP1's availability from Microsoft today was not checked on the day (from memory: removed
  from public downloads).
- No run on 0762's 5.5 DLL source (`export-e5-dll`): that is 4707's on C5, once `main` carries the 5.5 pin.
  *Done 2026-11-26 by 0773, section 11.*
- `%TEMP%` and the registry were not watched during the build; only `--out` and the source tree were checked.
- The render suite ran 17 of its 109 tests (stopped on an F:-bound test, section 5); the suites ran only in the
  export tree, not in the product checkout for the same commit (their results on `main` come from the merge checks of
  their own tickets).
- The release take's compositor PDB path (section 6) was inferred from the build script, not read from a released DLL.
- The cause of the anonymous-namespace hash differences (which given file name differs) was not traced.

## 11. The clean build of the first public export (ticket 0773 phase 2, 2026-11-26) [L]

- Package: `tools/export/package.py --rev 37d7ccf51 --dll-branch export-e5-dll-one --fetch` (19:5x EST, 44 s): commit 1
  1698af8c3 (Release-5.5 b0bce8c2f + export-e5-dll-one 1cea15013's tree), commit 2 306db2d0a (1,675 files under
  `64bit/`: product 1,455 with `deps/dll.lock`, notes 220); the export check with the private terms and the
  environment's names PASS, 0 findings.
- A plain copy of commit 2 (`git archive`) is refused by `server/build_server.py` at once: `tools/x64-dll/build.py
  resolve_src` takes `--unlocked` only for a git source, and a plain copy must match `deps/dll.lock`'s fingerprint (the
  lock pins x64-vp55 a2627c8ac; the pull request's DLL tree is another commit) [C: the message and `resolve_src`]. A
  builder of the upstream repository has a git checkout, so the build ran in a local clone of the package's fork clone
  checked out at commit 2. Upstream therefore builds with `--unlocked` until the lock is optional (0777).
- `cleanbuild.py` (command in runbook 6.9) in that checkout's `64bit/`, one LTO link place held for the whole build:
  **`cleanbuild: OK` in 609 s** (19:49-19:59 EST): tables 1 s / 49 s (both complete), `build_server.py --config release`
  483 s, `shell/build.cmd` 77 s, layout 12 files. SHA-256: DLL `A531273F...B702`, `server.exe` `4C14D408...81B5`, proxy
  DLL `D1512579...8DF`, `shell_composite.dll` `E605A48A...1445`, `vp64helper.exe` `6B40F032...B3A9`; `lua51_x64.dll`
  (`30724D6D`) and `civ5save.exe` (`409422EF`) are byte-equal to the 0.1 release pin's (upstream-export.md 1.2).
- `evidence/9076/pdbcheck.py` on the layout: no user, machine, work or repository folder in the seven shipped
  binaries (the proxy DLL has one `VPSRC` placeholder); the PDBs, not shipped, carry work-folder paths (`server.pdb`
  15, `shell_composite.pdb` 230, `vp64helper.pdb` 2), as section 6 and upstream-export.md 1.4 describe.
- Not done: no comparison with the product build of the same commit (section 5's work), no suites in the export tree.

Sources: this ticket's runs on the workstation, 2026-11-25; logs kept outside the repository during the session
(`C:/c5-64bit-work/impl-4706/logs-1711/`, `logs-1748/`, deleted with the scratch at the end); the masked summaries in
`tools/export/evidence/4706/` (both runs' `cleanbuild.log`, the first run's failures, the set and DLL byte
comparisons, `statscmp`, the D10 summaries, the PDB counts, the epoch build's line) and its three comparison scripts
(`set_hashes.py`, `bytediff.py`, `tablediff.py`: names and offsets only, no value of the EXE).
