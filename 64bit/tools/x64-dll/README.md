# tools/x64-dll: the x64 build of the DLL

## One command (issue 1405)

```
python tools/x64-dll/build_all.py [--src <fork source>] --out <dir> [--config debug|release] [--db <gameplay db>]
```

Steps, in order, each logged to `<dir>\logs\<step>.log`: closed-library stubs and `lua51_x64.dll`
(`closedlib-stubs/build_stubs.py`) -> CvGameDatabase library (`cvgamedatabase/build_db.py`, writes
`CvGameDatabase_x64.dll/.lib` into the stubs dir; issue 1402) -> CvLocalization library
(`cvlocalization/build_loc.py --db-dir <stubs>`, writes `CvLocalization_x64.dll/.lib` into the stubs dir; issues 1701,
1703) -> the DLL (`build.py --stubs`, with the narrowing gate) -> smoke host (`smoke-host/build_host.py`) -> smoke run
(`smoke-host/run_smoke.py --db`, which opens a fresh copy of the gameplay database through `CvGameDatabase_x64.dll`
and checks the DLL's info counts against it; issue 1404; the smoke host does not initialise the localization
dictionary, so the DLL's text lookups during `CacheGameDatabaseData` return the key, as before 1703). Exit 0 only if every step succeeded; otherwise the failing step's exit code and a last
line `build_all: FAILED at step <name> (exit N): <reason>`. `<dir>\build_all.json` has each step's exit code and
seconds plus the DLL's size, unresolved count and gate result. Debug: about 65 s (115 s on 2026-11-10 with the
localization step, 8 s, on a loaded machine); release: about 11 min (the
full-LTO link alone is 8.5-11 min, single-threaded). Evidence of every check (release link, gate pass and fail,
offline failure, plain-copy build, build_all run, `/WX` check): `evidence/2026-11-10-1405/`.

**What `--src` must be** (checked against `deps/dll.lock` by every script here): the DLL fork's source at the pinned
commit (`deps/dll.lock` key `commit`: x64-0.1b `1b87dd185` on 2026-11-21), either
*(Corrected 2026-11-21, one-off 9080: the lock is the source of truth; replaced "(`e9d0c4837`, branch `x64`)", an
older pin. Write the lock's commit where the commands below say `<commit>`.)*

- a git checkout of exactly that commit with no tracked changes (`git -C <fork clone> worktree add --detach <dir>
  <commit>`), or
- a plain copy of that commit without `.git` (`git -C <fork clone> archive -o <tmp>\src.tar <commit>`, then
  `tar -x -f <tmp>\src.tar -C <dir>`). A copy has no history, so it is checked by a source fingerprint (issue
  1508): SHA-256 over every `*.h/.hpp/.inl/.c/.cpp/.def` of the build-input directories plus `build_vp_clang.py`,
  CRLF folded to LF (473 files at e9d0c4837), compared with the `fingerprint` line of `deps/dll.lock`; a copy of
  another commit, or with edited, extra or missing sources there, is refused before anything compiles. About 1 s.
  `src_fingerprint.py --git <fork clone>` prints the lock line after a re-pin; `--dir <tree>` checks a tree. The
  DLL's version string says it was built from a copy (`Release-5.4.8-6-ge9d0c4837 Copy`).

Any other commit or tracked changes are refused with the reason; `--unlocked` builds them on purpose (fork
development, git checkouts only) and records it. Every script here that takes `--src` (`build.py`, `build_all.py`,
`build_stubs.py`, `cvgamedatabase/build_db.py`, `cvgamedatabase/ab/run_ab.py`, `smoke-host/build_host.py`,
`smoke-host/gen_ifaces.py`, `closedlib-stubs/_test/*.py`) runs this same check (`build.py` `resolve_src`). Nothing is ever written into `--src`. `--out` must be outside the repository and
outside `--src`.

**Without `--src`: the one setting (ticket 4701).** `dll_source.py` gives the DLL source location, in this order:
the environment variable `VP_DLL_SRC`; `src = <path>` in `deps/dll-source.ini` (this repository's value, a path of
this machine; a relative value is taken from the repository root); else the upstream layout, `..` seen from the folder
that holds `tools/` (these tools under `64bit/` in the DLL repository, where `../CvGameCoreDLL_Expansion2` is).
`build.py` takes its `DEFAULT_SRC` from it and every script above uses that, `build_all.py` included (its `--src` is
no longer required). The default goes through the same `resolve_src` check, and a refusal names where it came from
("(the default, from deps/dll-source.ini)"). `python tools/x64-dll/dll_source.py` prints the location and its origin.
The list of every tool of the repository that names the DLL source: `research/03-dll-engine-boundary/x64-dll-port.md`
section 13.

Third-party sources come from `deps/third-party.lock` through `thirdparty.py`, offline first: `--lua-tarball PATH`
(and `--sqlite-zip PATH` for the database library), else the cache root `$CV64_DEPS_CACHE` (default
`%LOCALAPPDATA%\Temp\claude\cv64-deps`, outside the tree), else a download from the lock's URL unless
`--offline` / `CV64_OFFLINE=1`. The SHA-256 from the lock is checked every time; a missing or wrong file fails with
one line naming the URL, the hash and where to put the file.

The gameplay database (`--db`) defaults to `<cache root>\testdb\vp548-cdrift-Civ5DebugDatabase.db`: a read-only copy
of civ5-headless `pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db` (VP 5.4.8, SHA-256 `bd9b8aa1...addfadd6f`),
which matches the fork's 5.4.8 base. It is not downloadable: copy it there, or pass any VP 5.4.8 `Civ5DebugDatabase.db`
copy (never the file in `Documents\My Games` itself). Without it `build_all.py` stops before building.

Other options: `--no-gate` (no `-Wshorten-64-to-32`, no gate), `--jobs N`, `--timeout S` (smoke run),
`--major-civs N` / `--unvalidated-major-civs` (issue 4252, below: the dll step only; the smoke step then also requires
the DLL's start line `Supporting N major civilizations` and the `+mN` version suffix, recorded as `smoke_major_civs` in
`build_all.json`).

Folders: `closedlib-stubs/` (stub and replacement libraries, `lua51_x64.dll`), `cvgamedatabase/` (the real
CvGameDatabase over SQLite 3.7.17 for x64 and x86, its x86 A/B against the closed library; own README),
`cvlocalization/` (the real CvLocalization for x64 and x86, its x86 A/B; own README; replaced the stub group
`closedlib-stubs/CvLocalization/`, deleted in issue 1703), `smoke-host/`,
`lua51/`, `thirdparty.py` (locked third-party archives), `narrowing-baseline.tsv`, `evidence/`.

## The steps by hand

`build_stubs.py`, `build_db.py` and `build_loc.py` must run before `build.py --stubs` (they produce the libraries
`--stubs` links, `lua51_x64.dll`, `CvGameDatabase_x64.dll` and `CvLocalization_x64.dll`); the commands `build_all.py`
runs (issue 1508 added the second and the `--db` of the last, issue 1703 the third):

```
python tools/x64-dll/closedlib-stubs/build_stubs.py --src <fork source> --out <stubs> --config debug
python tools/x64-dll/cvgamedatabase/build_db.py --src <fork source> --out <stubs> --arch x64 --config debug
python tools/x64-dll/cvlocalization/build_loc.py --src <fork source> --out <stubs> --arch x64 --config debug --db-dir <stubs>
python tools/x64-dll/build.py --src <fork source> --config debug --out <dll> --stubs <stubs> [--shorten]
python tools/x64-dll/smoke-host/build_host.py --src <fork source> --out <host> --stubs <stubs>
python tools/x64-dll/smoke-host/run_smoke.py --host <host> --dll-dir <dll> --dep-dir <stubs> --run <run> --db <gameplay db>
```

Census builds (no stubs, `/FORCE:UNRESOLVED`) and stub iteration:

```
python tools/x64-dll/build.py --src <fork source> --config debug   --census --clean --out <dir>
python tools/x64-dll/build.py --src <fork source> --config release --census --clean --out <dir>
python tools/x64-dll/build.py --src <fork source> --config debug   --stubs <stub-lib-dir> --link-only --out <dir>
```

`build.py` compiles and links `CvGameCore_Expansion2.dll` (Vox Populi 5.4.8) as a 64-bit binary. Issue 1001, epic
1000. Probe it was built from: `evidence/2026-11-10-probe/`. Run it from any directory with Python 3.11 (no packages
needed). It calls `vcvars64.bat` itself; no developer prompt is needed. `--out` takes every output, so several builds
can run at once in different directories. The source tree is never written (issue 1405): `commit_id.inc`
(`CURRENT_GAMECORE_VERSION`, untracked in the fork and written by its `update_commit_id.bat` in the x86 build) is
generated as `<out>\gen\commit_id.inc` (`git describe --tags --long` plus `Clean`/`Dirty` for a git tree, the lock's
`describe` plus `Copy` for a plain copy) and mapped over `<src>\commit_id.inc` with a clang VFS overlay
(`-vfsoverlay <out>\gen\vfsoverlay.yaml`), which wins over a stale file in the source tree too.

## Options

| Option | Meaning |
|---|---|
| `--src DIR` | DLL fork source at the commit of `deps/dll.lock`: a git checkout of it without tracked changes, or a plain copy without `.git` (see "What `--src` must be"). Default: the setting of `dll_source.py` (above; here `deps/dll-source.ini`), accepted only while it is at the locked commit. The source list is read from its `build_vp_clang.py` (`CPP = [...]`), so it follows the fork. |
| `--unlocked` | Accept a git `--src` at another commit or with tracked changes (fork development); `build.log` records it. |
| `--config debug\|release\|fast\|pgo-instr` (build.py; build_all.py takes debug and release only) | debug: `/Od /Oy-`, `VPDEBUG`. release: `/Ox /Ob2 /Zo -flto`, `STRONG_ASSUMPTIONS NDEBUG VPRELEASE_ERRORMSG`, link `/LTCG /OPT:REF /OPT:ICF` (as the x86 `build_vp_clang.py`); its link waits for the whole-program link place (below). fast (ticket 0159): release's flags and defines without `-flto`, link without `/LTCG`, plus a marker section; for functional work only, never timing (section "The fast configuration"). pgo-instr (used by 4801): the instrumented build for PGO training (`pgo/train.py`). *Row corrected 2026-11-21 (one-off 9080): pgo-instr and the build_all.py limit added.* |
| `--link-place-wait MIN` | Release: give up after MIN minutes waiting for the whole-program link place (default `C5_LTO_LINK_WAIT_MIN`, else 60); exit 4. |
| `--out DIR` | Required. Objects in `obj\` (mirrors the source tree), PCH, DLL, PDB, import lib, map, logs. |
| `--census` | Link with `/FORCE:UNRESOLVED` and write `unresolved.txt`, `unresolved-grouped.txt`, `unresolved.tsv` (below). |
| `--stubs DIR` | Link every `DIR\*.lib` (the closed-library stubs of issues 1005/1006). Works with and without `--census`; with it, the census lists what the stubs leave open. |
| `--link-only` | Reuse the objects in `--out`; only link. For stub work: about 3 s per debug link. Appends to `build.log` instead of replacing it; refuses objects built with another `--config`. |
| `--shorten` | Add `-Wshorten-64-to-32` (implicit 64->32 narrowing, issue 1003) and run the **narrowing gate** (issue 1405, below): exit 3 before linking if any site is not in the baseline. |
| `--narrowing-baseline FILE` | Baseline of the gate; default `tools/x64-dll/narrowing-baseline.tsv`. |
| `--write-narrowing-baseline` | With `--shorten`: replace this config's rows of the baseline with the current sites (after a reviewed, intended change), then gate against it. |
| `--jobs N` | Parallel compiles; default all logical cores (32 here). |
| `--clean` | Delete this script's outputs in `--out` first (`obj\`, `mangled\`, `CvGameCore_Expansion2.*`, PCH, logs, lists, the vcvars cache). Never deletes anything else. |
| `--no-commit-id` | Use the source tree's own `commit_id.inc` instead of generating one (fails if it has none, e.g. a plain copy). `update_commit_id.bat` is never run (issue 1405). |
| `--major-civs N` | Issue 4252 (epic 0334, MC decisions G2/G3): the major-civilisation count. Default 22 = `MOD_GLOBAL_MAX_MAJOR_CIVS` absent, the command line exactly as before. Another value adds `/DMOD_GLOBAL_MAX_MAJOR_CIVS=N` after the configuration's defines (never to `PREDEFS`, which the server and the proxy read: they take no define, G1), writes the version string `<describe>+mN <Clean\|Dirty\|Copy>` and records `major_civs`, `major_civs_define`, `major_civs_header` in `build-summary.json`. Validated: 22 and 43. The source's own `CustomModsGlobal.h` line is read first: a live line must equal N (then no `/D` is added), else exit 1 before anything compiles (upstream tags 3.8.1, 3.10.11, 4.2.5 have it live). `--link-only` refuses objects of another count; `--no-commit-id` refuses N other than 22. |
| `--unvalidated-major-civs` | Allow a `--major-civs` value other than 22 and 43 (2..62; 63 and above are always refused: no city-state slot, no compile). |
| `--mangled auto\|on\|off` | Census: a second link with `/DEMANGLE:NO` fills the `mangled` column of `unresolved.tsv`. `auto` (default) = on for debug (3-5 s), off for release (an LTO link takes about 11 min). |

Exit code 0 when every source compiled, the narrowing gate passed (with `--shorten`) and lld-link returned 0; 3
when the gate failed; 4 when a release link gave up waiting for the whole-program link place (the objects stay:
`--link-only` links them later); 1 otherwise, always with a last line `FAILED: <reason>`. In normal mode (no `--census`)
unresolved symbols are link errors (`/ERRORLIMIT:0`, so all are listed) and the census lists are still written.

## The fast configuration and the whole-program link place (ticket 0159, decision D107)

The owner's order of 2026-11-13 (`research/07-architecture-decisions/architect-2026-11-13-linker-memory.md`): the release
link is full LTO, one thread, 6-13 GB of private memory and 8-11 minutes, and several at once filled the machine.

**`--config fast`** compiles exactly as release (`/Ox /Ob2 /Zo`, the same defines, so the same code paths, asserts and
logging) but without `-flto`, and links without `/LTCG` (`/OPT:REF /OPT:ICF` kept). Use it for functional work:
graphics, parity runs while iterating, database, installer, proxy and server behaviour (COMMON rule 18). Never for
timing, and never as a pin or a shipped build. It must never pass for release, so:

- the DLL carries a section `.vpcfg` holding `VP-BUILD-CONFIG=fast; not for timing` (a generated C file,
  `<out>\gen\vpconfig.c`, kept by `/INCLUDE:vp_build_config`; data only, travels with the file when it is copied or
  renamed). Release and debug DLLs have no such section; their bytes are unchanged by 0159;
- `build-summary.json` and `build.log` record `"config": "fast"` (and `"for_timing": false`);
- a layout (the `bin/` folder a server or instance runs from) holding it is named `<layout>-fast`, for example
  `x64-04b1211b-pa803776-fast`;
- `timing_refusal(path)` in `build.py` reads all three (marker section, a `build-summary.json` or `build.json` beside the
  DLL whose config is fast, a folder name of the DLL's two parent levels ending in `-fast`) and returns the reason;
  `perf/turnbench/turnbench.py` refuses such an arm before it takes the lock or starts anything. The frame-rate tools
  adopt the same check after ticket 0157 merges (through `tools/harness/framecap.py`).

`--shorten` gates a fast build against the baseline's release rows (the compiler's diagnostics are the same; `-flto` only
changes the object format). `build_all.py` and the instance provisioning do not offer fast yet (callers adopt it later):
build the libraries with `build_all.py --config release` (or reuse a release build's `deps\stubs`) and the DLL with
`build.py --config fast --stubs <that stubs dir>`.

**One whole-program link at a time.** Before a release link (only configurations in `WHOLE_PROGRAM`, today release),
`build.py` takes a place of `perf/quietlock/linkplace.py`: 1 place on the workstation (`C5_LTO_LINK_PLACES`), kept at 1
after the memory upgrade of 2026-11-14 until a measurement shows a need for 2. A link that finds the place taken
waits in place and prints a progress line naming the holder (process id, owner, what, output folder, since when) when
the holder changes and every 5 minutes; it gives up after 60 minutes (`--link-place-wait`, `C5_LTO_LINK_WAIT_MIN`) with
exit 4. The holder's record names `build.py`'s process and, once started, the linker's; it is stale, and freed by the
next look, when neither runs. The memory floor of COMMON rule 16 (8 GB free commit) applies when the place is taken: a
free place with too little memory also waits, naming the three largest processes. Debug and fast links take no place.
`python perf/quietlock/linkplace.py status` and `quietlock.py check` list the holders.

`build.py` records the linker's peak private memory and peak working set (`link_peak_private_mb`,
`link_peak_working_set_mb` in `build-summary.json`, and the `lld-link rc=` line of `build.log`) for every link.
Measured on the pin 04b1211b8, 2026-11-13/14 on C: (`research/06-tooling-and-workflows/x64-dll-build-configurations.md`
section 2): release link 470.7 s, 13,340 MB peak private; fast 1.9 s, 466 MB; debug 1.9 s, 531 MB. The release build
at this tool commit is byte-equal to the pin's DLL (74588135...).

## Narrowing gate (issue 1405)

`--shorten` collects the unique `-Wshorten-64-to-32` sites and compares them with `narrowing-baseline.tsv`. A site's
key is file (relative to `--src`), column, warning text (it names both types) and the source line's text with
whitespace collapsed; the line number is informative only, so edits that shift lines do not count as new sites.
Keys are a multiset (the same statement twice counts twice). Any key whose count grew fails the build with exit 3;
`<out>\narrowing-new.txt` and `build.log` name each new site as `file:line:col: message | code`. Sites that disappeared
are reported as `GONE` and do not fail; tighten the baseline with `--write-narrowing-baseline`. The baseline has rows
per config (`debug`, `release`; both 798 sites at e9d0c4837, 2026-11-10) and the gate uses the rows of `--config`.
`build_all.py` always builds the DLL with the gate unless `--no-gate`. Warnings in the fork's sources are not errors
(no `/WX` there); the gate is what stops the narrowing census from growing.

`#line` (ticket 4810, 2026-11-17): the speed packages of epic 0504 put `#line N` after each speed block so `__LINE__`
does not move, and clang then names the remapped (logical) line and, after `#line N "file"`, the logical file. With
`--shorten` the compile therefore also passes `-Xclang -fno-diagnostics-use-presumed-location`
(`PHYSICAL_LOC_ARGS`): every diagnostic names the physical file and line, so the key's code text is the right line in
any branch. A diagnostics option only: code is unchanged and `-flto` objects are byte-identical with it (probe,
`perf/evidence/0504/4810/`). Before the gate the build compiles a small probe (`check_physical_lines`, in `<out>\gen`)
and stops if this clang still names a remapped line; a site whose line cannot be read (file missing, line past the end)
stops the build too (`NarrowingSiteError`; before 4810 it was keyed with an empty code text). Tests:
`tests/test_narrowing_line.py` (real clang-cl on small files: no directive, a speed block with `#line` define off and
on, `#line N "other file"`, `#line` inside `#if 0`).

## Warnings as errors for the project's own code (issue 1405)

`/WX` (with clang-cl's `/W3`, i.e. `-Wall`) for: every stub group's `*.cpp`, `closedlib-stubs/common/*.cpp` and the
generated `_imp_pointers.c` (`build_stubs.py`), and the smoke host's `src/*.cpp` (`build_host.py`). Not for the DLL
fork's sources, not for Lua (stock 5.1.4) and not for SQLite. The fork's include directories are passed as external
headers (`/external:I`, `/external:W0`) in those two scripts, so a warning inside a fork header (today:
`-Wreorder-ctor` in `FFastVector.h:212`) does not fail the project's code. Check: `closedlib-stubs/_test/wx_check.py
--out <dir>` (an own-code warning fails, the fork-header warning does not, and the same probe with plain `/I` fails).
Turning `/WX` on exposed no warning in the project's own sources (2026-11-10).

## Outputs in `--out`

| File | Content |
|---|---|
| `build.log` | Command, tool versions (clang-cl, lld-link, MSVC `VCToolsVersion`, SDK, Python, source HEAD/describe/branch/tracked changes), `commit_id.inc`, full clang-cl arguments, per-phase timings, slowest TUs, warning counts per category, unresolved counts per group |
| `build-summary.json` | The same numbers for scripts |
| `compile.log` | Every TU's compiler output, in source-list order, with return code and seconds |
| `warnings.txt` | Unique warning sites per `-W` flag, with occurrence counts (headers count once per TU) |
| `link.rsp`, `link.log` | The lld-link response file and its full output |
| `CvGameCore_Expansion2.dll/.pdb/.lib/.map` | The DLL, its PDB, import library and `/MAP` link map |
| `unresolved.txt` | Sorted unique unresolved symbols, demangled exactly as lld prints them (the probe's `undef.txt` format) |
| `unresolved-grouped.txt` | The same, grouped by closed library with the probe's name rules (`probe-group-unresolved.py`); a group `other` means a rule is missing |
| `unresolved.tsv` | group, reference count, mangled name, demangled name, first referencing object and function and source line |
| `link-mangled.rsp/.log`, `mangled\` | The `/DEMANGLE:NO` census link (its DLL is a throwaway) |
| `vcvars-env.json` | Cached `vcvars64.bat` environment (the batch takes 6-60 s here) |

## Notes

- **Link checks (one-off 9023, 2026-11-11, pattern P10).** `linkcheck.py` is shared by `build.py`, `build_all.py`,
  `server/build_server.py` and `proxy/build_proxy.py`. lld-link prints nothing on success, so a link counts as
  successful only with exit 0, no error line in its log (`lld-link: error`, `error LNKnnnn`, `LLVM ERROR`, out of
  memory, `bad_alloc`; not read for census links, which force unresolved symbols) and an output written after the
  link started. `build.py` prints `OK: <dll> sha256 <hex> size <n> mtime <time>` and records `dll_sha256`/`dll_mtime`
  in `build-summary.json`. `build_all.py` fails the dll step, even on exit 0, without that `OK:` line, with a
  summary or a DLL older than the step, and fails a library step whose `.dll` is older than the step (an unchanged
  `.lib` may keep its time: lld-link rewrites an import library only when it changed); it prints
  `build_all: dll: <path> sha256 ... size ... mtime ...` before its OK line. Tests:
  `python -m pytest tools/x64-dll/tests/test_linkcheck.py -q` (26, under a second, fakes only).

- **Repeatability check (issue 0131, 2026-11-13).** Every clang-cl compile takes `/Brepro` (through `repro_cl_args`), so
  no native object carries its compile time in the COFF header; the closed-library stubs did, and with stubs of other
  time stamps the DLL's LTO link gave other code for one function (`research/03-dll-engine-boundary/upstream-fix-batch-1.md`
  8c). After every link `build.py` prints `repeatability check: OK: N link inputs of this build (M objects and library
  members), none carries a time stamp; DLL sha256 ...` or `repeatability check: NOT REPEATABLE: k of M ... carry a COFF
  time stamp (e.g. ...)`, and records `repeat_check` in `build-summary.json`. The line does not fail the build. For an
  existing build: `python -I tools/x64-dll/repeatcheck.py <out>/deps/dll/link.rsp` (exit 0 OK, 1 not repeatable). Tests:
  `python -m pytest tools/x64-dll/tests/test_repeatcheck.py -q` (7, synthetic archives).
  The check shows that the link *inputs* repeat. The release link itself does not (ticket 0358, 2026-11-15): lld-link
  picks one of two code variants of `CvPlayer::doInstantGreatPersonProgress` by chance per run, so two builds with an OK
  check may still differ in that function
  (`research/06-tooling-and-workflows/x64-lto-link-repeatability.md`).

- **Link environment (ticket 0358, 2026-11-15).** Every DLL link runs `lld-link @link.rsp` in the output folder with
  `link_env(env)` only: `LIB`, `SYSTEMROOT`, `TEMP` and `TMP` from the vcvars environment, and `PATH` = the LLVM folder +
  system32. No caller variable reaches the linker: not a session id, not `PWD`, not the gate's job id, and never `LINK`
  or `_LINK_`. The caller's environment changed the odds of the second code variant. The log line `link environment
  (issue 0358): ...` and `build-summary.json` `link_env` (names, size, SHA-256) record it. Tests:
  `python -I -m pytest tools/x64-dll/tests/test_link_env.py -q -p no:cacheprovider` (9 with 0373's).

- **DSE setting on the release link (ticket 0373, 2026-11-15).** The release (whole-program) link line carries
  `LTO_DETERMINISM_LINK` = `/mllvm:-dse-memoryssa-scanlimit=1000` (in `link.rsp`, before the objects); debug and fast do
  not. LLVM 19.1.7's dead store elimination decided one store in `CvPlayer::doInstantGreatPersonProgress` at the edge
  of its default scan limit (150), in an order that varied between runs, which made the release link pick one of two
  code variants by chance (ticket 0358). With the limit at 1000 the walk completes in any order and the decision is the
  same every run. It changes the DLL's code (more dead stores removed in general), so a DLL built with it is a new
  candidate with its own hash. Evidence and run counts: `research/06-tooling-and-workflows/x64-lto-link-repeatability.md`
  section 5. Tests: `test_link_env.py` (the setting on the release line and in `link.rsp`, not on the others) and
  `test_config.py` (the release link line).

- The default `--src` (since ticket 4701 the setting of `dll_source.py`; here the coordinator's checkout) moves as `x64` gets commits; since issue 1405 it is used only
  while it is at the commit of `deps/dll.lock` (otherwise the build stops with the reason). `build.log` records the
  source mode (`git`, `git-unlocked`, `copy`), the HEAD at the start and warns if it moved before the end.
- (Before issue 1405, `update_commit_id.bat` was run in the checkout by default and failed on a plain copy with
  "Could not determine HEAD commit"; it is no longer run.) Three sources include `../commit_id.inc`.
- `vcvars64.bat` prints `'vswhere.exe' is not recognized` on this machine; harmless, the environment is complete.

## Toolchain (recorded by the build in `build.log`, 2026-11-10)

clang-cl 19.1.7 `-m64` and lld-link 19.1.7 (`C:\Program Files\LLVM\bin`), MSVC 14.43.34808 headers and libraries
(VS 2022 Community 17.x, `vcvars64.bat`), Windows SDK 10.0.26100.0, Python 3.11.5. Decision (team-1000
coordinator, Minor, 2026-11-10): MSVC 14.x x64 ABI, `/MD` (dynamic UCRT, vcruntime140, msvcp140), `/std:c++14`.

Compiler flags: `-m64 -msse3 /c /MD /GS /EHsc /fp:precise /Zc:wchar_t /Zi /FS /std:c++14 -ferror-limit=0`, the
`build_vp_clang.py` defines plus `_HAS_AUTO_PTR_ETC=1`, `_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS`,
`VP_OUT_OF_PROCESS` (issue 1002 gives it meaning), its include dirs plus `CvGameCoreDLL_Expansion2`, its
warning suppressions plus `-Wno-c++11-narrowing` (x86 parity). `-msse3` is kept for parity with the x86 build.
Linker flags: `/MACHINE:X64 /DLL /DEBUG /LTCG /DYNAMICBASE /NXCOMPAT /SUBSYSTEM:WINDOWS /MANIFEST:EMBED
/ERRORLIMIT:0 /DEF:CvGameCoreDLL.def /MAP`, the x86 default import libraries; `--census` adds `/FORCE:UNRESOLVED
/FORCE:MULTIPLE`. (Corrected 2026-11-10, issue 1009: the normal link no longer passes `/FORCE:MULTIPLE`; with the
1005/1006 stubs and `lua51_x64.lib` it links with 0 undefined and 0 duplicate symbols without it.)

Lua (issue 1009): `closedlib-stubs/build_stubs.py` builds Lua 5.1.4 as the shared `lua51_x64.dll` plus its import
library `lua51_x64.lib` in the stubs directory, so `--stubs` links the import library like any other `*.lib` and the
DLL imports `lua51_x64.dll` (as the x86 DLL imports `lua51_Win32.dll`). Whatever loads the DLL needs
`lua51_x64.dll` beside it or on the loader path.

Differences from the x86 `build_vp_clang.py`, on purpose: no VS 2008 environment (VC9 has no x64 libraries
here), `clang.cpp` is not built (it holds the 32-bit `_ftol3`/`_ltod3` helpers that only VC9-converted
libraries need), no closed `*Win32.lib`/`.obj` is linked (they are 32-bit; `--stubs` replaces them).

## Measured on this machine (2026-11-10)

32 logical cores, `--census --clean`, other workers building at the same time (so the timings are upper bounds and
vary: `vcvars64.bat` alone took 6 s, 28 s and 62 s in three runs, hence the cache). Seconds, wall clock [L]:

| Run (source) | vcvars | commit_id | PCH | 172 TUs (sum of TU times) | link | mangled link | total |
|---|---|---|---|---|---|---|---|
| debug, b25fd45c0, ~02:25 EST | 6.2 | 0.8 | 13.0 | 24.8 (420) | 2.6 | 2.9 | 50.6 |
| release, b25fd45c0, 02:30 EST | 21.8 | 1.3 | 15.0 | 73.3 (665) | **664.4** | 735.8 | 1512.0 |
| release `--stubs --shorten`, e9d0c4837 plain copy, 04:11 EST (issue 1405): 0 unresolved, no `/FORCE`, DLL 23,614,464 bytes, smoke 16/16 | 7.9 | | 16.6 | 83.8 (656) | **506.8** | | 615.8 |
| debug `--stubs --shorten`, e9d0c4837 checkout, 04:15 EST (issue 1405): 0 unresolved | 9.9 | | 21.0 | 37.9 (574) | 6.6 | | 77.0 |
| debug, 8e26515e8, 02:44 EST, during the release LTO link | 28.0 | 5.4 | 34.8 | 58.7 (798) | 5.4 | 4.8 | 140.2 |
| debug `--link-only` | | | | | 2.3-12.0 | 3.4-6.6 | 4.5-25 |

Slowest TUs (debug): `CvCity.cpp` 18.6 s, `CvPlayer.cpp` 18.5 s, `CvUnit.cpp` 11.8 s; (release) `CvPlayer.cpp` 65.6 s,
`CvCity.cpp` 48.1 s, `CvUnit.cpp` 37.0 s. The release link is full LTO (`-flto`, as x86), single-threaded: 11 min.
`-flto=thin` would parallelise it but changes code generation against the x86 build; not done.

Outputs: debug DLL 27,156,480 bytes, map 46,964,863 bytes, PDB 105 MB; release DLL 23,554,560 bytes. Both are
`IMAGE_FILE_MACHINE_AMD64` and export `DllGetGameContext` (`llvm-readobj --coff-exports`) [C].

Warnings (both configs, b25fd45c0; occurrences over all TUs including the PCH / unique sites) [C]:
`-Wdeprecated-declarations` 221 / 85, `-Wpointer-to-int-cast` 36 / 5, `-Wvoid-pointer-to-int-cast` 2 / 2. Without
the PCH TU the occurrences are 186 / 28 / 2, exactly the probe's `all.log`; the 7 unique pointer sites are the
probe's 7. At 8e26515e8 (issue 1002: crash handler, pointer casts, `VP_OUT_OF_PROCESS`) the pointer warnings are
**0**; only `-Wdeprecated-declarations` 221 / 85 remains [C]. With `--shorten` at f3d2f5669 (03:00 EST, 87 s total):
`-Wshorten-64-to-32` 953 occurrences / 799 unique sites (the probe counted 794 at b25fd45c0) [C]; evidence
`evidence/2026-11-10-build-1001/debug-shorten/`. (Explained 2026-11-10, issue 1405: the +5 are header sites that
only the PCH TU `_precompile.cpp` reports, `CvAStarNode.h:237`, `CvCultureClasses.h:67`, `CvTacticalAI.h:1295`,
`FLuaCommon.h:213`, `FLuaTypes.h:114`; the probe and issue 1003 counted without the PCH TU, `build.py` counts it [C,
`compile.log` of the e9d0c4837 debug build]. At e9d0c4837 (deps/dll.lock): 952 / **798** = 794 - 1 (47d7664e9 fixed
`CvDatabaseUtility.cpp`) + 5, debug and release alike; this is the narrowing gate's baseline.)

## Census result (2026-11-10)

| Build | Compiled | Unresolved | Compared with the probe |
|---|---|---|---|
| debug, b25fd45c0 | 173/173 | **336** | `unresolved.txt` identical to the probe's `undef.txt`; `unresolved-grouped.txt` identical to `evidence/2026-11-10-probe/unresolved-336-grouped.txt` (DLLUtil 123, FireWorks 68, CvGameDatabase 46, lua51 46, CvWorldBuilderMap 32, CvLocalization 16, FLua 5, other 0) [C] |
| debug, 8e26515e8 | 173/173 | **336** | identical to b25fd45c0 [C] |
| release, b25fd45c0 | 173/173 | **326** | a strict subset of the debug 336 [C] |

"173" counts the PCH TU (`_precompile.cpp`, `/Yc`) plus the 172 of `build_vp_clang.py`'s `CPP` list. The probe
compiled `_precompile.cpp` a second time with `/Yu` and linked both objects (`/FORCE:MULTIPLE`); that changes
nothing in the census.

Why release has 10 fewer: with `-flto` lld reports only the undefined symbols still referenced after LTO has
dropped dead code, and `NDEBUG`/`STRONG_ASSUMPTIONS` remove assertion-only paths [I: mechanism; C: the list]. The
10: `Database::Results::Bind(int, float)`, `operator<<` and `operator>>` of `ButtonPopupTypes`, `VoteSourceTypes`,
`VoteTypes` (6), `lua_getstack`, `lua_typename`, `FStringA::SetAt(int, int, char const *, int)`. **Stubs must cover
the debug list (336)**; the release list is not a complete inventory.

Normal mode was checked with a one-symbol test stub library (`lua_pushnil`): census 335, and without `--census`
lld-link fails with exactly 335 `undefined symbol` errors (all listed) [C].

Evidence (copied from the `--out` directories): `evidence/2026-11-10-build-1001/` with `debug/`, `release/`,
`debug-tip/`, `debug-shorten/` (each: `build.log`, `build-summary.json`, `warnings.txt`, `unresolved.txt`, `unresolved-grouped.txt`,
`unresolved.tsv`) and `release-vs-debug-unresolved.diff`. `debug/build.log` holds only later `--link-only` runs
(the clean compile record was overwritten before `--link-only` learned to append); its compile figures are in the
table above, and `debug-tip/build.log` is a complete clean debug record.
