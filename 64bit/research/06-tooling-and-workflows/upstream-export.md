# Upstream export: checks on what leaves the repository

The export plan of D174 (`research/07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md`) names checks
that the exported source and the built binaries must pass. This file holds their measured results, one section per
check; section 3 is the export script itself, section 8 the import of changes that come back. Private strings are named here by kind (user folder, machine name, work folder) and file offset, never quoted.

## 1. Private build paths in today's release binaries (ticket 9076, 2026-11-17) [C]

Plan section 4.6 point 4: "the binaries contain no private path". Ticket 9076 searched the binaries that ship today.

### 1.1 Result

**The shipped binaries carry no user folder, no machine name and no workstation path [C].** Not in the strings, not in
the PE debug directory, not in the version resources (UTF-16 strings were searched too). One shipped binary carries a
**personal name**: the 32-bit proxy DLL holds a log message that names the project owner by first name together with
an internal question number (section 1.3). The PDBs, which do not ship, carry work-folder paths in two cases, and a
user-folder path in the newest server build's PDB (section 1.4).

### 1.2 What was searched and how

Binaries (every `.dll` and `.exe`; SHA-256 and size in `evidence/9076/scan-output.txt`):

| Set | Folder | Files |
|---|---|---|
| Release pin | `deps/dll.lock` `release`: layout `x64-04b1211b-pfdee656` (the shipped bytes) | x64 DLL `29F58E17...7B1D`, `server.exe` `5B254430...40E5`, proxy DLL `75C40CEC...54EC`, `vp64helper.exe` `B8AC9A64...8E0F`, `CvGameDatabase_x64.dll` `9C37B39E...466E`, `CvLocalization_x64.dll` `35863EDE...7D8A`, `lua51_x64.dll` `30724D6D...0F1A`, `civ5save.exe` `409422EF...5B6C` (not shipped) |
| First playable build | `F:/c5-64bit-work/0324/stage/VP64-first-playable-VP5.4.8-x64-04b1211b-pfdee656/` (`installer-notes.md` 11.1) | the same seven shipped binaries, byte-equal to the pin (same SHA-256); also its seven text files |
| Newest build | `C:/c5-64bit-work/0463/run/layout/x64-4f4daac7-p63a79cf/` (ticket 0463's release-driver run, built 2026-11-17 00:16-00:26 by main's tools; **not a release**, read only) | x64 DLL `1768D70C...F23F`, `server.exe` `7813E7B0...0E6` (7,167,488 B, with the server renderer and the models library), proxy DLL `23F197DF...643F`, `vp64helper.exe` `2AB9AE71...7814`; the three libraries and `civ5save.exe` equal to the pin's |

Method (scripts in `evidence/9076/`, run 2026-11-17 on the workstation, outputs scrubbed by the scripts themselves:
the user name, machine name and git identity are replaced by tokens and the script refuses to write if one survives):

- `scan.py <out> <label>=<folder> ...`: parses each PE file, prints the CodeView (RSDS) PDB path of the debug
  directory, and classifies every printable ASCII string and UTF-16LE string of 5+ characters: user folder
  (`\Users\<user name>\`), the user name as a word (exact case, not inside an identifier, so game words such as
  `GREAT_WORK_ART` do not count), machine name, git name and mail, work folders (`c5-64bit-work`, `c5-i0`), repository
  folders (`GitHub`, `.claude\worktrees`), profile folders (`AppData`, `Documents`), any drive-letter path, UNC paths.
  `.reloc` is skipped (relocation blocks decode as noise such as `:L:\:l`).
- `pdbcheck.py <folder> ...`: counts the same private kinds, and the build's placeholder names (`VPSRC`, `VPREPO`,
  `VPOUT`, `VPGEN`, `VPBUILD`, `C:\vpbuild`), in the binaries and in the PDBs beside them (counts only).
- `ctx.py <file>`: the scrubbed context of each private hit in one file (used on the two PDBs of 1.4; not committed
  output, the contexts are summarised below).
- `textcheck.sh`: the first playable build's text files (`BUILD.txt`, `NOTICE.txt`, `SHA256SUMS.txt`,
  `INSTALL-FIRST-PLAYABLE.md`, `vpproxy.ini`, `manifest.json`, `purity.tsv`): 0 user folder, 0 machine name, 0 work or
  repository folder. The 8 lines with a drive-like `X:/` are URLs and one registry key (`HKLM:\...`).

### 1.3 Findings in the shipped binaries

| Binary (pin SHA-256) | Kind | Where | Note |
|---|---|---|---|
| all eight | PE debug directory | CodeView record, e.g. x64 DLL file 0x138F350, `server.exe` 0x2C86FC | the PDB **file name only** (`server.pdb`, ...): `/PDBALTPATH:%_PDB%` works; the second entry is type 16, `IMAGE_DEBUG_TYPE_REPRO` from `/Brepro` [C] |
| x64 DLL `29F58E17` | `__FILE__` strings | `.rdata` from file 0x131ACC6, 12 strings | `VPSRC\CvGameCoreDLL_Expansion2\CvPlot.h` and the like: the source folder is mapped to `VPSRC` [C] |
| proxy DLL `75C40CEC` | `__FILE__` string | `.rdata`, 1 string | mapped to `VPSRC` [C] |
| x64 DLL `29F58E17` | fixed path in upstream source | `.rdata` file 0x12FAB07 | `c:\temp\UnitKillCount_Player%d.csv`, a debug output name in upstream's own code; not private |
| proxy DLL `75C40CEC` (also `23F197DF` of the newest build, file 0xD897A) | **personal name and internal id** | `.rdata` file 0xE80C5 | a log line of the white-mask option that names the project owner by first name and an internal question number; source `proxy/src/proxy_shell.cpp` line 1687 (the `ProxyRtInt::Log` call of the `WhiteFogMask = 1` branch). Not a path; it does ship. Under D95 (nothing sent upstream carries internal ids or names) the text needs rewording before an export [C]. **Fixed in the source by ticket 0471 (2026-11-17)**: the line now ends `on (issue 0172)%s`; the pin's and the newest build's bytes still carry it until the next proxy build (1.7) |
| `vp64helper.exe`, `NOTICE.txt`, `INSTALL-FIRST-PLAYABLE.md` | public project URL | helper `.rdata` file 0x2C15A (UTF-16) | the report link `github.com/<owner>/c5-64bit-patch/issues/new`: intended and public, but it names the repository that the export plan keeps private (staging); the exported copy will need the link the plan decides |
| server, proxy, helper | profile folder (word only) | `.rdata` | `C5_DOCUMENTS`, "the Documents folder could not be found": messages, no path |

No hit of the user folder, the machine name, the git identity or a work or repository folder in any of the eight
binaries of the pin, the seven of the first playable build or the eight of the newest build [C, `scan-output.txt`,
`pdbcheck-output.txt`].

### 1.4 Findings in the PDBs (not shipped) [C]

PDBs are not in the first playable build (`installer-notes.md` 11.1: "Not shipped: PDBs"). They matter only if they are
ever published (symbols for crash reports) or if the clean build of plan 4.6 compares them.

| PDB | Kind | Count | Where it comes from |
|---|---|---|---|
| `vp64helper.pdb` (pin and newest build) | work folder (`C:\c5-64bit-work\<ticket>\...\proxy\obj\helper\*.obj`) | 2 | the helper's two object files are compiled with an absolute `/Fo` path (`proxy/build_proxy.py` `build_helper`, `'/Fo' + str(o)`), and the object name goes into the CodeView object record unmapped |
| `server.pdb` of the newest build only | **user folder**, repository folder, agent worktree name | 14 (one per source file) | the module names of `c5r_models.lib`: `CMakeFiles/c5r_models.dir/C_/Users/<user>/Documents/GitHub/c5-64bit-patch/.claude/worktrees/agent-<id>/models/src/<file>.cpp.obj`. `render/src/units/CMakeLists.txt` adds the `models/src` files from outside render's source tree, and CMake then names each object folder after the source's absolute path. The pin's `server.pdb` (before epic 0375 linked the models library) has none |
| all other PDBs | none private | | `VPSRC`/`VPREPO`/`VPOUT`/`VPGEN`/`VPBUILD` placeholders and `Program Files` (the toolchain's and Windows SDK's headers) |

### 1.5 The switches, and which builds use them

| Switch | What it removes | Our builds |
|---|---|---|
| `/PDBALTPATH:%_PDB%` (lld-link, link.exe) | the absolute PDB path in the debug directory | in `REPRO_LINK` (`tools/x64-dll/build.py`), used by the x64 DLL, `server/build_server.py`, `proxy/build_proxy.py` (DLL and helper), the database and localization DLLs [C: debug directories hold file names only]; since ticket 0778 (2026-11-25) also `shell_composite.dll` in `shell/build.cmd`, the flag alone (1.8) |
| `-ffile-prefix-map=<dir>=<name>` (clang; `/clang:` from clang-cl) | absolute folders in `__FILE__`, assert texts and debug info. The MSVC compiler's equivalents are `/pathmap:<dir>=<name>` (documented) and the undocumented `/d1trimfile:<dir>`; we compile with clang-cl, so they do not apply | `repro_cl_args` in `build.py` (maps to `VPSRC`, `VPREPO`, `VPOUT`, `VPGEN`), used by the DLL, server, proxy and helper [C: `VPSRC\...` strings] |
| `-fdebug-compilation-dir=VPBUILD`, `-gno-codeview-command-line` | the compile folder and the command line in CodeView build-info records | the same callers [C: `VPBUILD` in every PDB] |
| `/PDBSOURCEPATH:C:\vpbuild` | the linker's working folder in the PDB | `REPRO_LINK` [C] |
| `/Brepro` | time stamps (not a path; listed because plan 4.6 point 3 needs it) | the same callers (issues 0115, 0131) |
| a relative `/Fo` with the compile run in the object folder | the object path in CodeView object records | the proxy DLL, server and x64 DLL [C: their PDBs hold no work folder]; **not** the helper (1.4) [C: its `/Fo` is absolute and its PDB has the path; I: that a relative `/Fo` removes it, not rebuilt here] |
| sources inside the CMake source tree (or `add_subdirectory` of `models/` with its own binary folder) | CMake's absolute-path object folder names, which become `.lib` member names and so PDB module names | **not** `render/src/units/CMakeLists.txt` (`c5r_models`) [I: fix not tried]; `render/` passes none of the switches above either, and `server.exe` is clean today only because the render and models sources use no `__FILE__` in release and the objects carry no path into the EXE [C for "no `__FILE__`" by search of `render/`, `models/`; I for "stays clean"] |

### 1.6 What this means for the export

- The shipped release binaries already pass plan 4.6 point 4 for paths [C]. The one shipped private item is the
  personal name with an internal question id in the proxy's log text (1.3): a one-line source change, done by ticket
  0471 on 2026-11-17 (1.7; was: "for E1 or a bug ticket").
- For E5 (the clean build and its private-path check): the check should run on the binaries **and** the PDBs if PDBs
  are ever published; the two PDB leaks of 1.4 are the work for that (an object-relative `/Fo` for the helper; models
  sources inside a CMake source tree, plus `repro_cl_args`-style mapping in `render/` so a later `__FILE__` or debug
  record cannot carry the worktree path into `server.exe`).
- `scan.py` is a ready base for that check: it reads only the PE file and the environment's user and machine names.

Not verified: no rebuild was made with any changed switch (no build changes in this ticket); the newest build was read
while ticket 0463's run was still in progress, so its bytes may not be that run's final ones; the 32-bit fallback DLL
that the release takes from the upstream tag (`x86_dlls` in `manifest.json`) is upstream's binary and was not searched;
`shell_composite.dll` (only for `ServerRenderer = 1`, not in the first playable build) was not searched (searched by
ticket 0778 on 2026-11-25: it carried the work folder in its debug directory; section 1.8).

### 1.7 Personal names in the string literals of the shipped sources (ticket 0471, 2026-11-17) [C]

- 2026-11-17 01:13, workstation, branch `task-0471-proxy-log-line-owner-name` (main f1d1e872d + the fix):
  `python -I research/06-tooling-and-workflows/evidence/0471/scan_literals.py "<pattern>" proxy/src proxy/include
  proxy/helper server/src render/src render/include shell/common shell/composite shell/frame shell/server shell/standin
  ipc/src ipc/include options/src options/include` (314 files; comments stripped, only `"..."`, `'...'` and raw literals
  searched), `<pattern>` = case-insensitive alternation of the owner's first name as a word or possessive, full first
  name, surname and GitHub handle. Before the fix: 2 owner hits. After: 1, the helper's report link
  (`proxy/helper/vp64helper.cpp` line 71, `kDefaultHandin`, the public repository's `issues/new` URL with the handle;
  1.3 rates it intended and public, its exported form is the plan's to decide); no personal name. The other 37 hits
  are the word "art" for unit and terrain art assets (`art.h`, art rows, art tables).
- Fix: `proxy/src/proxy_shell.cpp` line 1687 (the `WhiteFogMask = 1` log line) keeps its leading text, which
  `proxy/tools/rt_tests/shell_unit.cpp` checks (lines 623, 690), and drops the name and the question id.
  `shell_unit` 313/313 passed; its `shell_unit.exe` (proxy toolchain, `proxy_shell.cpp` compiled in) holds the new
  text and 0 copies of the old one [C].
- Not covered: the generated proxy sources (`proxy/gen` output, from the DLL's headers), the DLL fork's own sources,
  and comments (ticket 4702). No full proxy build was made: the released proxy bytes change with the next build.

### 1.8 `shell_composite.dll` named its PDB by absolute path; `/PDBALTPATH:%_PDB%` added (ticket 0778, 2026-11-25) [C]

- Finding (impl-4706, 2026-11-25 18:53): `shell/build.cmd` linked `shell_composite.dll` with `/DEBUG /OPT:REF
  /SAFESEH` only, so the CodeView (RSDS) entry of its debug directory held the absolute PDB path of the build folder.
  The installer ships this DLL (`installer-notes.md`), so every beta so far shipped the build's work folder. It is a
  work folder, not a user folder: the shell build folder is `C5_SHELL_BUILD`, by rule under `C:\c5-64bit-work\` or F:.
- Fix: `/PDBALTPATH:%%_PDB%%` on the composite's link line in `:build86` (the `%%` because `build.cmd` is a batch file;
  the linker receives `%_PDB%` and writes only the PDB's file name). Only the flag, not the rest of `REPRO_LINK`:
  `/Brepro` changes time stamps and `/PDBSOURCEPATH` only the PDB, which is not shipped; neither was asked for. The
  test programs that `build.cmd` also links (test_host, the tap hosts, farm guard test and peer, lk_late, lockstep
  server, shell_server) are not shipped and keep their absolute names.
- Before (DLL from `C:\c5-64bit-work\0603\shell-build`, built 2026-11-24 20:37, SHA-256 `20972F1D...57284`):
  `evidence/9076/pdbcheck.py` gives `work-folder=1`; `evidence/0778/pdbname.py` (reads the debug directory) gives
  `C:\c5-64bit-work\0603\shell-build\bin\shell_composite.pdb`.
- After (branch `impl-0778` on main 8ee426878, built 2026-11-25 20:02-20:06 into `F:\c5-64bit-work\0778\shell-build`,
  plain variant, 1,953,280 bytes, SHA-256 `4E4BA3F5...CC8DD7`): pdbcheck `none` (no user folder, machine name, work or
  repository folder); pdbname `shell_composite.pdb`. Build `BUILD OK`, no LNK4 warning;
  `shell/tests/test_farm_guard.py` 11 passed (49.7 s) on that build.
- Commands (Git Bash, through `quietlock.py run`, runbook 1.5 and 2.2): `MSYS_NO_PATHCONV=1
  C5_SHELL_BUILD='F:\c5-64bit-work\0778\shell-build' python -I perf/quietlock/quietlock.py run --who <t> --what ... --
  cmd /c "shell\build.cmd"`; then `C5_SHELL_BUILD=... PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p
  no:cacheprovider shell/tests/test_farm_guard.py`; `python -I research/06-tooling-and-workflows/evidence/9076/pdbcheck.py
  <folder with the DLL>`; `python -I research/06-tooling-and-workflows/evidence/0778/pdbname.py <dll>`.
- Not verified: the asan variant (same link line, so the same flag; not built); a product take or installer built with
  the change (the shipped bytes change at the next take); that a debugger still finds the PDB beside the DLL by name
  (the usual behaviour of `/PDBALTPATH:%_PDB%`, as for the other builds; not tried here).

## 2. The allow-list and the private-content check (ticket 4700, package E0, 2026-11-17)

Plan sections 4.2 and 4.3: one allow-list file, a check that can only pass or fail, and that check as a gate at every
merge into `main`. Files: `tools/export/allowlist.txt`, `tools/export/check.py` (standard library only),
`tools/tests/export/test_check.py` (39 tests). Cleaning the findings is E1 (product) and E2 (notes), not this ticket.

### 2.1 The allow-list

Sections, matched against the path from the repository root (`folder/`, `**/folder/`, or a glob where `*` stays in one
path segment and `**` crosses segments):

| Section | Meaning |
|---|---|
| `[product]` | exported: source, build files, unit tests, the build and release tools (`tools/x64-dll/`, `tools/release/` without `releases/` and `series/`), the offline scene harness (`server/tools/scenerun/`, `server/tools/sceneshot/`), `LICENSE`, `README.md`, `CMakeLists.txt`, `docs/OPTIONS.md`, `deps/third-party.lock`, `shell/third_party/` |
| `[notes]` | exported after E2: `research/README.md`, `research/0*/*.md`, `research/03-dll-engine-boundary/upstream-drafts/`, the ownership diagram (SVG) |
| `[unclear]` | not exported and not gated, listed for the MC (2.4); wins over product and notes |
| `[private]` | wins over everything: `**/evidence/`, `ipc/results/`, `research/sources/`, the decision log, the Architect's proposals (`architect-*.md`: they quote the owner verbatim, plan 4.4 point 3) |
| `[verbatim]` | not a group: third-party files kept byte for byte (`shell/third_party/`), where authors' e-mail addresses in licence notices are not findings; every other rule still applies |

Everything else is private by omission: `ops/`, `tracker/`, `docs/` except `OPTIONS.md`, `AGENTS.md`, `STATUS.md`,
`.claude/`, `evidence/`, `perf/` (except the tools listed as unclear), the test-farm tools (`tools/harness`, `testbed`,
`instances`, `queue`, `diskwatch`, `hi`, `humanplay`, `i0`, `i0b`, `psprobe`, `stressmap`, `validation`, `wait`),
`tools/export/` and `tools/tests/` themselves, and the other `deps/*.lock` pins. A file matched by both product and
notes is an error of the allow-list (exit 2). On `main` 460589c5 the allow-list selects 862 product files, 129 notes
files and 386 unclear files [C, `--list` and `--summary`].

### 2.2 The rules

| Rule | Matches | Not a finding |
|---|---|---|
| `user-path` | `X:\Users\<name>`, `X:/Users/<name>`, `X:\\Users\\<name>` (any run of separators: doubled, quadrupled), `/c/Users/<name>`, `/Users/<name>`, `/home/<name>`; since 0596 (2026-11-20) also JSON's `\/`, `\u005c`, URL `%5C`/`%2F`, and the POSIX forms with escaped slashes (section 13); since 0596 also searched in every unexported file by the gate's repository pass | placeholders (`<user>`, `%USERPROFILE%`, `$env:...`, `{...}`, one-letter names, `Public`, `Default`, `nobody`, `you`, `runneradmin` and similar) |
| `user-name`, `machine-name`, `personal-name`, `private-term` | the terms of the private terms file (2.3), the environment's `USERNAME`/`USER` (case-sensitive) and `COMPUTERNAME`/`HOSTNAME`; whole words only (a letter, digit or `_` next to the term means no match, so identifiers such as `GREAT_WORK_ART` do not count); also Windows default machine names (`DESKTOP-`/`LAPTOP-`/`WIN-` plus 7-11 capitals and digits, at least one digit) | |
| `e-mail` | any address | `example.com/.org/.net`, `.example`, `.invalid`, `.test`, `.localhost`, the commit attribution address, `git@github.com`, files under `[verbatim]` |
| `lan-address` | IPv4 10/8, 172.16/12, 192.168/16, 169.254/16 with valid octets, not inside a longer dotted number (`10.0.26100.1` is not one); IPv6 `fe80::`, `fc00::/7` | public and loopback addresses |
| `file-picture`, `file-save`, `file-log`, `file-dump`, `file-capture` | extensions `.png .jpg .jpeg .gif .bmp .tga .dds .webp .tif .tiff .ico .psd .exr .hdr .heic`; `.civ5save .civ5replay`; `.log`; `.dmp .mdmp`; `.rdc`; reported at line 0 | `.svg` (our own diagrams go, plan 4.4 point 2) |

A binary file (NUL bytes) is searched as Latin-1 and as UTF-16LE, findings at line 0; UTF-16 text with a byte-order
mark is decoded. A line carrying `export-check: allow=e-mail` or `export-check: allow=lan-address` silences that rule
on that line only; the term, user-path and file-type rules cannot be silenced. Output is one line per finding,
`path:line: rule: <match>`, with letters and digits masked after the first of each run (`C:\U****\x**`), so the output
can be pasted into a note; `--show` prints the raw match. Exit codes: 0 pass, 1 fail, 2 usage or configuration error
(bad option, bad allow-list or terms file, `--require-terms` with no terms file, a revision git does not know, not a
git checkout).

### 2.3 The private terms list (never committed)

The real user names and machine aliases are never written in the repository. The check reads them from, in this order:
`--terms FILE` (repeatable); the files in `C5_EXPORT_TERMS` (a path-separator list); `tools/export/private-terms.local.txt`
(git-ignored by `tools/export/.gitignore`); `~/c5-ops/export-private-terms.txt` (the MC's ops folder outside the
repository). Format, one term per line, `#` comments:

```
user <name>         # a user name, case-insensitive whole word
user/cs <name>      # case-sensitive (for a name that is also an English word)
machine <alias>
name <personal name or handle>
other <anything else private>
```

Terms that are common words or code identifiers cannot be checked this way: one test-machine alias is the English word
for the machine type, and another is a two-character token that is also a variable name in the renderer and the shell
(a first run with it gave 86 more findings, 41 of them in product code, all variable names) [C, 2026-11-17]. Those two
aliases are left out of the list used for 2.5; their use in files is caught only where it comes with a user path or a
LAN address.

### 2.4 Left off the allow-list for the MC (`[unclear]`)

Product side: `.gitignore`; `docs/INSTALL-FIRST-PLAYABLE.md` (the install guide of a build; names the repository's own
report link, 9076 section 1.3); `deps/dll.lock` (the pin; plan 4.5 says upstream needs none); `ipc/bench/`, `ipc/tools/`,
`models/tools/` (Granny readers and dumps: D34 and D83 to be read first), `proxy/model/` (15 MB of extracted JSON, plan
4.3), `proxy/proof/`, `proxy/tools/`, `render/tools/`, `server/specs/`, the loose scripts, `probes/` and `unitfeed/` of
`server/tools/`, `server/unserved-allowlist.txt` (only perf evidence reads it), `shell/standin/`, `shell/tools/`,
`tools/release/releases/` and `series/` (release records), `tools/compare/`, `tools/d10/` except `statscmp.py`,
`savecmp.py` and their two tests (in `[product]` since ticket 0815, section 17; the two test saves and the D10 runner
stay; was: "`tools/d10/` (holds two test saves, and `statscmp.py`, which the clean build of plan 4.6 point 3 uses)",
corrected 2026-11-26), `tools/drift/`, `tools/scenario/`, `tools/visualcheck/`,
and the perf tools `perf/cputime/`, `memsampler/`, `profilekit/`, `sampler/`, `turnbench/`.
Notes side, about this project's agent operations and test farm rather than the EXE, the DLL or the design:
`research/06-tooling-and-workflows/` `capacity-series.md`, `d-drive-load.md`, `desktop-app-stability.md`,
`epic-task-queue.md`, `implementation-effectiveness.md`, `quiet-testbed.md`, `remote-test-machines.md`,
`session-log-hooks.md`, `tracker-server.md`, `worker-account.md`; `research/07-architecture-decisions/`
`queue-reexamination-2026-11-13.md` and `wmc-*.md`.

### 2.5 Today's failures per group (`main` 460589c5, 2026-11-17 about 01:30 EST; the same counts on 36f1134df) [C]

Command: `python -I tools/export/check.py --rev origin/main --allowlist tools/export/allowlist.txt --terms <private
list> --groups product,notes,unclear --summary`. Private list: 7 terms (the owner's user names on three machines, one
of them case-sensitive; the family name; the GitHub handle; the workstation's machine name; one test-machine alias)
plus the 2 environment terms. Run time about 5 s for the working tree, 11 s for the gate (two commits).
*Note 2026-11-21 (one-off 9080): the gate `--rev <branch> --since main --require-terms --no-env-terms` now takes
about 45 s, since it also searches every unexported file (repository group, 16,663 files).*

| Group | Files | With findings | Findings | user-name | machine-name | user-path | personal-name | file-save |
|---|---|---|---|---|---|---|---|---|
| product | 862 | 83 | 200 | 107 (34 files) | 17 (7) | 63 (43) | 13 (12) | 0 |
| notes | 129 | 105 | 1,097 | 494 (81) | 335 (43) | 252 (69) | 16 (12) | 0 |
| unclear | 386 | 68 | 650 | 284 (24) | 232 (13) | 115 (40) | 17 (16) | 2 (2) |

No e-mail address, LAN address, picture, log or dump in any of the three groups [C]. With the generic rules only (no
terms: `--no-terms --no-env-terms`) product has 63 findings in 43 files and notes 252 in 69, all `user-path`.

What the findings are [C, from the masked output, and the `--show` output read on the workstation, not committed]:

- `user-name`: nearly all are the owner's first name, which is also the workstation's Windows user name: decision
  attributions in the notes ("<name>, 2026-11-xx, decision Dnn") and the release tools' messages and docstrings
  (`tools/release` has 62 of the product's 107). Whether the first name may stay in exported notes is a call for the
  MC or the owner (open point of this ticket's hand-back); the check counts it because the same string is the user name.
- `user-path`: the workstation's user folder (the repository, the game install, the fork checkout). Product:
  `tools/release` 17, `tools/x64-dll` 16, `server/tests` 12, `proxy/gen` 5 (`toolchain.py` `FORK`, `BUILD_ROOT`),
  `shell/tests` 4, `render/build.py` 2, `models/tests` 2, one each in `server/src`, `server/README.md`,
  `proxy/README.md`, `deps/third-party.lock` and `shell/third_party/minhook/NOTICE` (our provenance line, not
  upstream's text).
- `machine-name`: the quiet-testbed alias (all 352 in product and notes; `server/tools/scenerun/` 9, `tools/release` 5
  in product); the workstation's machine name does not occur.
- `personal-name`: the GitHub handle in the D22 origin comments (`// Origin: repository <handle>/Community-Patch-DLL
  ...` in `shell/`, `proxy/src`) and in repository links (`README.md`); the family name once.
- `file-save`: the two test saves under `tools/d10/tests/data/` (unclear group).

Sample (the files with most findings): product `tools/release/stages/hi_journey.py` 24, `tools/release/take_release.py`
20, `tools/release/tests/test_hi_journey.py` 15, `server/tools/scenerun/README.md` 7,
`server/tests/render/test_decal_names_mods.py` 6; notes `release-driver.md` 104, `headless-and-parallel-testing.md` 84,
`run-and-drive-the-game.md` 54, `research/07-architecture-decisions/README.md` 52,
`research/06-tooling-and-workflows/README.md` 33.

### 2.6 How the MC runs it at a merge

One time: put the private terms file at `~/c5-ops/export-private-terms.txt` (format 2.3: the user names, the family
name, the handle if it should count, the machine names), or point `C5_EXPORT_TERMS` at it.

At every merge into `main`, from the main checkout, before the merge commit, beside the whole-history privacy grep and
`test_safety_static.py`:

```
python -I tools/export/check.py --rev <branch> --since main --require-terms
```

- It reads the branch's committed files through git (nothing is checked out or written) with the branch's own
  allow-list, and fails only on findings that the branch adds over the merge base of `main` and the branch: a finding
  counts as new when its (file, rule, matched text) occurs more often on the branch than at the base. Files that git
  sees as renamed keep their base findings. A branch that moves a folder into `[product]` or `[notes]` has that
  folder's existing findings counted as new.
- Correction (ticket 0596, 2026-11-20): until 0596 the gate searched the exported groups only, so a home path in a
  `[private]` file (`**/evidence/`), an `[unclear]` file or an unlisted one (`tracker/`, `docs/`) passed although it is
  committed and pushed. Since 0596 the gate also runs the repository pass (section 13): every unexported file is
  searched for `user-path` alone and reported as group `repository`; a finding's key also records whether its file is
  exported, so a private file moved into an exported group still counts as new. `--no-repo-paths` gives the old
  behaviour. (Replaced: "the gate checks product and notes only".)
- Exit 0: merge. Exit 1: the branch adds private content to an exported path; the lines name file, line and rule, and
  the branch goes back with them (fixed at the source, plan 4.2; for an e-mail or LAN false positive, the line
  pragma). Exit 2: the setup is wrong (no terms file, unknown branch); fix it and run again, never merge on 2.
- It is a command the merge helper calls and reads the exit code of; there is no hook, and it edits nothing.
- The 1,297 findings of 2.5 in product and notes are the base the gate measures against; they do not block merges.
  E1 and E2 bring them to zero; after that `python -I tools/export/check.py --rev main --require-terms` must pass on
  its own, and the export script (E3) runs it before copying.

Other uses: `--summary` for counts per group and rule; `--list` for the files of each group; `--groups unclear` to
see what an unclear folder would bring; `--tree DIR --allowlist FILE` for a plain folder such as an export staging
copy.

### 2.7 Tests and what is not verified

`python -I -X utf8 -m unittest discover -s tools/tests/export -p "test_*.py"`: 39 tests, about 25-30 s, in temporary
folders with invented terms (the planted strings are built from pieces at run time, so the test file carries none): a
planted user path (four spellings), machine alias, Windows default machine name, user name, e-mail, LAN address (two
ranges), picture, save, log, a binary and a UTF-16 file each caught at the right line; a clean tree with placeholders,
public and loopback addresses, a version number and an example e-mail passing; unclear, private and unlisted files not
checked; the pragmas; masking; every usage error exiting 2 with no PASS line; the gate in a temporary git repository
(an old finding not counted, a new one and a second copy counted, a rename passing, a folder moved into the allow-list
failing, untracked files ignored); the repository's own allow-list listing with no overlap and no evidence path.

Not verified: the check has not run inside the MC's merge helper (only by hand, on this branch against `main`: exit 0,
0 new findings over d748f4db); the first-cut folder choices of 2.1 and 2.4 are the implementer's reading of the plan's
table, not yet checked by the MC; the counts of 2.5 are pattern matches, and which of them are real leaks (the first
name in decision attributions, the handle in origin comments) is a decision, not a measurement; the check does not read
git history, commit messages or file names, and it does not do the D83 audit.

### 2.7 The MC's gate as used (MC, 2026-11-17 01:49) [C]

- Terms file: `~/c5-ops/export-private-terms.txt` exists (6 terms: the family name, the GitHub handle, machine names and the other machines' user names). The owner's bare first name is left out, and the MC runs the gate with `--no-env-terms` (the environment's `USERNAME` is that first name), until the owner decides whether the first name may appear in exported notes (decision attributions such as "<name>, date, decision Dnn"); the user-path rule still catches the user folder.
- At every merge into `main`, beside `test_safety_static.py` and the whole-history privacy grep: `python -I tools/export/check.py --rev <branch> --since main --require-terms --no-env-terms`. First run, on 4700's own branch: PASS, 0 new findings over 1,297 at the base.
- *2026-11-26, ticket 0773 (section 15):* the owner decided (D262): the first name is out of every exported file, and
  the full check WITH the environment's names passes on `main` from 0773's merge. The merge gate's form above is
  unchanged (`--no-env-terms`); export.py and package.py refuse on any environment-term finding.
- Correction (MC, 2026-11-17 02:05): the machine aliases (the quiet testbed's and the laptop's) are the project's safe stand-ins for the real machine names (standing rule: aliases only), so they are not terms; the terms file holds 5 terms (the family name, the handle, the real machine names, the other machines' user names). With the alias in, the gate failed 0456's merge on 4 mentions of the alias in a research note.

## 3. The export script, the export tag and the ledger (ticket 4710, package E3a, 2026-11-17)

Plan sections 4.2 and 5 (E3, without the import of returning patches, which is E3b, ticket 4704). File:
`tools/export/export.py` (standard library only; it imports `check.py`'s allow-list parser and git reader, so the
check and the export select the same files). Tests: `tools/export/tests/test_export.py`. `tools/export/` is private by
omission from the allow-list, and so is `ops/`.

### 3.1 What it does [C: tests, dry run 3.3]

```
python -I tools/export/export.py --target <upstream checkout or throwaway folder> --rev <rev> [--tag NAME]
       [--groups product,notes] [--terms FILE] [--no-env-terms] [--dry-run [--allow-findings]]
```

In order, stopping at the first refusal:

1. Arguments and target: the target folder must exist, must not be a link, and must not overlap this repository;
   `<target>/64bit/` must hold no link, junction or `.git` entry, and no dry-run marker (3.2). With `--tag`: the tag
   `export/NAME` must not exist (an export tag is never moved) and the ledger must have no line for it. `--groups`
   takes only `product` and `notes`.
2. `check.py --rev REV --groups ... --require-terms --summary` (the full check, not the merge gate), run as a child
   process; its output is shown indented on stderr (without the `--root` path). Exit 1 (findings) refuses with exit
   1, any other non-zero exit refuses with exit 2; in both cases nothing is written, no deletion either.
3. The files of REV that REV's own allow-list (or `--allowlist`) puts in the chosen groups. A path that differs from
   another only in letter case, or that could not be created safely on Windows (`:`, `\`, `..`, ...), is refused.
4. Every file under `<target>/64bit/` not on that list is removed, then the folders left empty. Removal comes before
   writing, so a file renamed only in letter case ends with its new name on a case-insensitive disk.
5. Each listed file is written from its **git blob** (read with `git cat-file --batch`; nothing is checked out), byte
   for byte, no replacement of any kind; a file whose bytes are already equal is not rewritten.
6. stdout: the manifest, one `path<TAB>size<TAB>sha256` line per file in path order, then
   `manifest-sha256<TAB><sha256 of the manifest lines>`. It is written as bytes with LF line ends, so the printed
   text is exactly the hashed text (a plain `print` on Windows gives CRLF; the first dry run showed it). stderr:
   counts written, unchanged, removed.
7. With `--tag NAME` only: `git tag export/NAME REV` (lightweight, in this repository, **never pushed** by the
   script), then one line appended to `ops/export/ledger.tsv` under the repository root (created with its header
   when missing): `tag, revision, manifest_sha256, files, date, upstream_pr, upstream_merge`, the date in ISO 8601
   local time with offset, the two upstream fields empty for the MC. The ledger line is written only with a tag
   (the tag is its key); dry runs and untagged runs leave no trace in the repository. The ledger file is left
   uncommitted: committing it is the MC's step.

Nothing is written outside `<target>/64bit/` except the tag and the ledger line. Exit codes: 0 done, 1 refused by
the check, 2 usage, target or git error.

Byte for byte means the committed bytes. On the workstation `core.autocrlf` is on, so the working tree's text files
have CRLF while the blobs (and so the export) have LF; a comparison against the working tree would show every text
file as different [C, 2026-11-17: git's "LF will be replaced by CRLF" warning at this ticket's commit]. The
executable bit of a blob is not carried over (Windows has none to set) [I: not tested].

### 3.2 Dry runs (design accepted by the MC, 2026-11-17)

- `--dry-run`: the same copy, no tag and no ledger line. `--tag` with `--dry-run` is refused.
- `--allow-findings` (only with `--dry-run`, and only into a target with no `.git`): a failed check is shown but the
  copy goes ahead, so the selection can be tried on a revision that E1/E2 have not cleaned yet. The copy gets the
  marker `64bit/NOT-AN-EXPORT.txt` (the revision and the check's verdict line). Every later run into a folder with
  the marker is refused (exit 2), with or without `--tag`, even on a clean revision; only another
  `--dry-run --allow-findings` may refresh it, and it keeps the marker. The marker is not in the manifest.
- Why it exists: on `main` the full check fails until E1 and E2 are done, so the real export (and `--tag`) refuses
  there today. The MC's conditions (the marker, a test for every refusal) are met (3.4).

### 3.3 The acceptance dry run on `main` 3606885d38ba (2026-11-17 about 10:35 EST, workstation) [C]

`sh research/06-tooling-and-workflows/evidence/4710/dryrun_main.sh 3606885d38bab9d75ea36cc44c373196899490d5 <work
folder>`: export.py `--dry-run --allow-findings --no-env-terms` into a fresh folder on C:, then
`evidence/4710/verify_dryrun.py`, which compares independently. Output: `evidence/4710/dryrun-main-output.txt`.

| What | Result |
|---|---|
| The check on `main` (product, notes; 5 terms from the MC's terms file, no environment terms) | FAIL, 335 findings in 123 files: product 911 files, 67 findings (user path 53, personal name 13, user name 1); notes 134 files, 268 (user path 252, personal name 16). Section 2.5 counted 1,297 at 460589c5; the drop is E1/E2 work, measured here only as a by-product |
| Manifest | 1,045 files, 22,667,758 bytes, manifest hash `d67ba108...4143`; export.py about 12 s |
| `check.py --rev <same> --list` (the allow-list's product and notes files) | 1,045 files, the same set as the manifest |
| Files on disk under `64bit/` (marker excepted) | the same 1,045 paths; every size and SHA-256 equal to the manifest; every file byte-equal to its git blob |
| A run without `--allow-findings` into a fresh folder | refused, exit 1, the folder stays empty |
| A `--dry-run` into the marked folder | refused, exit 2 |

The default `--groups` is product and notes (check.py's exported groups); notes go only after E2 (plan 4.3), so the
first real exports may want `--groups product`. The script's default keeps the environment's user and machine names
as terms (stricter than the MC's merge gate, 2.7); the run above used `--no-env-terms` like the gate, since the
environment's user name is the owner's first name, which the owner has not yet ruled on. Which flag the first real
export uses is the MC's call. *Corrected 2026-11-26, ticket 0773 (section 15): the owner ruled (D262) and the first
name is out of the exported files; export.py now refuses `--no-env-terms` except with `--dry-run --allow-findings`
(a copy then marked NOT AN EXPORT), so every export is checked with the environment's names.*

### 3.4 Tests

`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -X utf8 -m unittest discover -s tools/export/tests -p "test_*.py"`:
27 tests, 59-131 s on the workstation (two runs, 2026-11-17) (each builds temporary git repositories; invented terms, planted strings
built at run time). Covered: byte equality with the blobs (CRLF, binary with NUL bytes, UTF-8), the manifest and its
hash; a second run unchanged with the same manifest; files outside the allow-list (unclear, private, evidence,
unlisted, a notes file the glob does not match) never leave; `--groups product`; deletions and renames propagate,
empty folders go, a stray file in `64bit/` is removed; a case-only rename; nothing outside `64bit/` changes in an
upstream-like target checkout (sizes, hashes, modification times) and nothing in the source repository without
`--tag`; the manifest count equals `check.py --list`'s. Refusals, each with nothing written: a finding (into a fresh
target, and as an update of an earlier export, so no deletion either), `--tag` on findings, `--allow-findings`
without `--dry-run`, `--allow-findings` into a git checkout, `--tag` with `--dry-run`, a check usage error (no terms
file), `--groups` with `unclear`, a target inside the repository, a missing target, a `.git` inside `64bit/`, an
existing tag, a ledger line already present, bad tag names. Dry runs: no tag or ledger; the marker written with the
revision and the FAIL line, every later run refused, a refresh by another `--allow-findings` run, no marker on a
clean revision. The tag: created on the revision, absent from a bare remote after the run; the ledger header and line
(tag, revision, manifest hash, file count, ISO date, two empty fields), a second tag appending a second line. The
repository's own allow-list puts `ops/export/ledger.tsv`, `export.py` and its tests in the private group.

Not verified: no export into a real checkout of the upstream repository (none exists here yet; a temporary
repository stands in); links and junctions inside `64bit/` are refused by code that no test exercises (making links
needs rights the test runs do not have); the executable bit; no real tag was made in this repository. The import of
returning patches is E3b (4704).

## 4. The build and release tools cleaned at the source (ticket 4711, package E1a, 2026-11-17)

**2026-11-17 14:32 EST: the settings that replace user paths and the fork's address** in the build and release tools
(tools/release, tools/x64-dll, deps/third-party.lock, render/build.py, models/tests/test_art.cpp). Before: 34 findings
in these 23 files (33 user paths; 1 personal name, the fork's address in tools/release/stages/lock.py). After
(branch `task-4711-export-check-release-tools`, commit 4611862c2): none in them, none added elsewhere (the gate:
`PASS: 0 new findings`; the product group 67 findings in 51 files -> 33 in 29) [C].

`tools/release/settings.py` reads each setting from its environment variable, else `<key> = <value>` in the settings
file (`C5_SETTINGS_FILE`, else `deps/local-settings.ini`: committed, private because deps/ is not exported apart from
deps/third-party.lock, and it holds this machine's values), else a default:

| Key | Environment variable | Default | Read by |
|---|---|---|---|
| `pins_dir` | `C5_PINS_DIR` | none: take_release.py refuses (exit 2) | take_release.py `forbidden_work()` (was `FORBIDDEN_WORK`); stages/build.py's layout README line |
| `clones_dir` | `C5_CLONES_DIR` | the repository's parent when it is named `GitHub`, else none: take_release.py refuses | take_release.py `art_clones()` (was `ART_CLONES`), the `--fork` guard |
| `fork_url` | `C5_FORK_URL` | none: the lock stage fails | stages/lock.py `repository()` (was `REPOSITORY`), the candidate locks' `repository =` |
| `c5h` | `C5_C5H` | `<home>/Documents/civ5-headless` | stages/testdata.py, stages/wholegames.py `C5H` |
| `core_db` | `C5_CORE_DB` | none since ticket 0612 (2026-11-20; was the game's live cache `<home>/Documents/My Games/Sid Meier's Civilization 5/cache/Civ5CoreDatabase.db`): a pinned copy, hash in `CORE_DB_PINS` | stages/testdata.py `CORE_DB` |
| `render_game_dir` | `C5_RENDER_GAME_DIR` | none: those render tests skip | render/build.py `--test` (`C5R_GAME_DIR`) |
| `render_exe` | `C5_RENDER_EXE` | none: those render tests skip | render/build.py `--test` (`C5R_EXE`) |

Not through the settings file, because the environment already names the folder: `tools/x64-dll/thirdparty.py`
`DEFAULT_CACHE` and `cvlocalization/ab/run_ab.py` `TEMP` are `%LOCALAPPDATA%\Temp\claude[\cv64-deps]`; the model
reader's real-sample test (`models/tests/test_art.cpp`) defaults `C5M_INSTALL_ASSETS` and `C5M_MODS` to the c5h-b
instance under `%USERPROFILE%`. The DLL source and the fork clone stay the settings of 4701/4709
(`tools/x64-dll/dll_source.py`, `deps/dll-source.ini`). Comments and READMEs name the setting or the folder's role.

Behaviour on the workstation [C]: every resolved path is the same as before (tools/release/evidence/4711/before.txt
and after.txt: each value with its SHA-256, home masked); the layout README line and the lock texts' address are byte
for byte the old ones (after_checks.txt); the guards still refuse `--work` under the pins folder and the repository
(exit 2, nothing written), and with no `pins_dir` or no `clones_dir` setting the driver refuses (exit 2) instead of
guarding less. Tests: test_take_release.py now runs on temporary pins and clones folders with no settings file, plus
negative tests for a missing setting and a malformed settings file.

Left for 4702 or the MC [I]: test_hi_journey.py and test_take_release_major.py (not in 4711's files) call
take_release.main and read `deps/local-settings.ini`; in an exported checkout without it they need `C5_PINS_DIR`
and `C5_CLONES_DIR` (the clean build of E5). tools/d10/tests/test_fork_setting.py's docstring still says testdata.py
keeps its C5H and CORE_DB literals (no longer true; not in 4711's files).

## 5. User-folder paths scrubbed in the research notes, first slice (ticket 4712, package E2a, 2026-11-17)

**2026-11-17 15:56 EST: the 51 notes files with user-path findings that no running ticket holds now have none** [C].
Before (`main` 1036e4a2e, `check.py --groups notes --require-terms --no-env-terms`, 5 private terms): 268 findings in
72 notes files, 179 of them user paths in these 51 files; after: 89 findings in 25 files, none of them a user path in
these 51 files; the other files' 73 user paths and all 16 personal names unchanged. Per-file before and after counts:
`tools/export/evidence/4712/counts.tsv`; the rewrite script `scrub.py` beside it (172 lines changed). The rewrite,
the form every later slice uses for notes:

| Was | Now |
|---|---|
| a path under the MC's pins folder (setting `pins_dir`, `deps/local-settings.ini`) | `<pins_dir>\...` or `<pins_dir>/...` (the rest of the path and its slashes unchanged) |
| the pins folder itself | "the pins folder" |
| any other path under the user's home folder | `%USERPROFILE%\...` or `%USERPROFILE%/...`; in a ```` ```powershell ```` block `$env:USERPROFILE\...` so the command still runs |
| a path inside a verbatim quote of a log line or a file's text | the same, marked `[path scrubbed 2026-11-17]` (4 places) |

The repository's own folder appears once in these files, as a Defender exclusion (an absolute folder), and takes the
`%USERPROFILE%` form; no path inside the repository was written absolute. In the commands of the notes, `<pins_dir>`
is a placeholder like the existing `<epic>` and `<ticket>`; `%USERPROFILE%` runs as written in cmd. Not in this slice:
the 17 notes files held by running tickets (E2's next slice) and the personal-name findings (listed for the MC in
the ticket's hand-back: five, all the project owner's GitHub handle in fork names and repository addresses).

## 6. The product files no running ticket holds, cleaned at the source (ticket 4713, package E1b, 2026-11-17)

**2026-11-17 19:32 EST: the user paths in the shell tests, the proxy and server READMEs and `server/src/srv_exports.cpp`
are gone; the personal names and the MinHook notice are left for the MC** [C]. Before (`main` 4dd3279f1,
`check.py --groups product --require-terms --no-env-terms`, 5 private terms): 33 findings in 29 product files, 19 of
them in the ticket's 18 files; after (commit e61a2ceb4): 26 in 22, 12 in the ticket's files, none of them a user path
except the notice; the gate `PASS: 0 new findings`. What replaced each path:

| File | Was | Now |
|---|---|---|
| `shell/tests/test_lockstep.py`, `test_join_ring.py`, `test_terrain_scene.py`, `test_residency.py` | `C5R_EXE` / `C5R_GAME_DIR`, else a literal EXE copy and game folder under the user's home | `C5R_EXE` / `C5R_GAME_DIR` if set, else the settings `render_exe` / `render_game_dir` of `tools/release/settings.py` (section 4: `C5_RENDER_EXE` / `C5_RENDER_GAME_DIR`, else `deps/local-settings.ini`), as `render/build.py` reads them; with neither, a placeholder path that does not exist, so the tests that need it fail naming the variable, as they did where the literal was absent. One helper, `release_setting(env, key)` in `test_lockstep.py`, imported by the other three |
| `proxy/README.md` (inputs of the model chain) | the fork worktree's path | the DLL source setting (`tools/x64-dll/dll_source.py`: `VP_DLL_SRC`, else `deps/dll-source.ini`), which `gen/toolchain.py` has used since 4708 |
| `server/README.md` (server.cfg `run_dir`) | the instance root under the user's home | "an instance root with a Defender exclusion, such as `%USERPROFILE%/c5-instances`" |
| `server/src/srv_exports.cpp` (comment) | the civ5-headless `.def` file's absolute path | `host\civ5headless.def` of the civ5-headless checkout (the setting `c5h`) |

Behaviour on the workstation [C]: the four tests resolve the same EXE and game folder as before, with no variable set
and with `C5R_EXE` / `C5R_GAME_DIR` set (`tools/export/evidence/4713/before.txt`, `after.txt`: each value with its
SHA-256, home masked, from `probe_paths.py` through `run_probes.sh`); only with no setting at all (an empty
`C5_SETTINGS_FILE`) the value is now the placeholder instead of the old literal. `shell/build.cmd` builds and the shell
tests' collection passes (`run_checks.sh`; results in the ticket's log). `tools/release/settings.py`'s "read by"
column does not yet name the shell tests (the file is not in 4713's touches) [I].

Not changed, listed for the MC (the ticket's hand-back): the personal-name findings (the copyright line of
`README.md`; the project owner's GitHub handle in the origin comments of the eight shell files copied from the fork
and in `proxy/helper/vp64helper.cpp`'s issue address) and `shell/third_party/minhook/NOTICE` line 3 (a user path in
the copied library's origin record, a notice that stays as it is). Still held by running tickets on 2026-11-17 (E1's
last slice): `proxy/src/proxy_shell.cpp` (0199), `server/tests/render/*` (0496), `server/tests/units/oracle_check.py`
(0502). Outside the checked groups (product, notes: not in `check.py --list`), but carrying the same literals: `shell/tools/residency_drive.py`
and `shell/evidence/3200/w4_probe_server_terrain.py` [C: grep 2026-11-17].

## 7. The remaining unheld files and the fork owner's handle (ticket 4714, packages E2b and E1c, 2026-11-17)

**2026-11-17 21:25 EST: the seven files with user paths that no running ticket held on 2026-11-17 22:10 have none, and
the project owner's GitHub handle is gone from three notes** [C]. Before (`main` 0dbd3f0c2, `check.py --require-terms
--no-env-terms`, groups product and notes, 5 private terms): 113 findings in 47 files, of them 47 user paths in this
ticket's files (`proxy-design.md` 7, `upstream-fix-batch-1.md` 11, the `05-graphics-composition` README 1,
`water-system.md` 5, the `06-tooling-and-workflows` README 9, `run-and-drive-the-game.md` 13,
`server/tests/units/oracle_check.py` 1) and 8 personal names (the handle); after (commit 73a562254): 62 findings in 39
files, no user path in these files, 4 personal names left (below); every other file's findings unchanged (raw masked
outputs `tools/export/evidence/4714/check-before.txt`, `check-after.txt`).

- Notes: the rules of section 5, by `tools/export/evidence/4714/scrub.py` (45 lines): pins folder -> `<pins_dir>`, the
  home folder -> `%USERPROFILE%` (no ```` ```powershell ```` block holds one); two verbatim log quotes in
  `run-and-drive-the-game.md` marked `[path scrubbed 2026-11-17]` (the shim log's `SHGetFolderPathW` value and the
  Lua.log line `vp-dll-dev: selected save ...`). No path inside the repository was written absolute.
- `server/tests/units/oracle_check.py`: the instance it reads is `C5_ORACLE_INSTANCE`, else `c5h-c` under
  `<home>/c5-instances` (`Path.home()`, forward slashes), the folder the literal named. On the workstation the values
  `INST`, `MODS`, `MODS_LIST` and the `--game` default are the same before and after (SHA-256 prefixes,
  `tools/export/evidence/4714/oracle-probe.txt` from `probe_oracle.py`); `--help` runs; the collection of
  `server/tests/units` passes (21 tests; the script itself is no pytest file).
- The handle: `headless-host.md` line 14 "(60716ff, the fork only)", `leaks-and-growth.md` line 188 "pushed to the DLL
  fork", `instrumentation-in-the-dll.md` lines 51 and 148 "the DLL fork". No link had to stay.

Left for the MC (the hand-back): the handle in `proxy-design.md` line 3182 ("`origin` (<handle> fork)") and in
`upstream-fix-batch-1.md` lines 10, 483 and 917 (the fork's address `<handle>/Community-Patch-DLL`), outside the three
files the ticket named; `release-driver.md` (0509) and `shell/third_party/minhook/NOTICE` stay for later as planned.

## 8. Three unheld performance notes (ticket 4715, package E2c, 2026-11-18)

**2026-11-18 00:20 EST: `metrics-ledger.md`, `turn-time-baselines.md` and `dse-scanlimit-turn-time.md` in
`research/08-performance/` carry no user path** [C]. Before (`main` e640ba889, `check.py --require-terms
--no-env-terms`, groups product and notes, 5 private terms): 62 findings in 39 files, 11 of them user paths in these
three files (`metrics-ledger.md` 6, `turn-time-baselines.md` 3, `dse-scanlimit-turn-time.md` 2); after (commit
03a882fe7): 51 findings in 36 files, none in these three; every other file's findings unchanged (raw masked outputs
`tools/export/evidence/4715/check-before.txt`, `check-after.txt`; the two outputs without the three files are equal).

- The rules of section 5, by `tools/export/evidence/4715/scrub.py` (4714's script with the three files and no quote
  list): eight paths under the pins folder -> `<pins_dir>/dlls/...` (the build folders of the measurements:
  `x64-04b1211b-pfdee656`, `i0ship-26234371-ia`, `i0ship-26234371`, `base-instr-eeb9305e`, `x64-c43ea1b9`,
  `i0ship-ccfd6dd4`, `x64-76992d23`), three other home paths -> `%USERPROFILE%/...` (the ledger venv
  `c5-ops/venvs/ledger` twice, one of them the command in a plain ```` ``` ```` block of section 6.1; the civ5-headless
  test database `Documents/civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db`). None was a verbatim
  log quote, so nothing is marked. Every build folder name, hash, save and machine is unchanged; 11 lines changed.
- Left as they were (outside the ticket): the user paths in `research/08-performance/README.md` line 21 (0437),
  `server/tests/render/*` (11, 0424), `offline-scene-harness.md` (3, 0300), `x64-dll-build-configurations.md` (0504),
  `server-host.md` (6), `addresses.md`, `terrain-surface-verification.md`, `human-input-testing.md` (1 each) and
  `shell/third_party/minhook/NOTICE` (26 user paths in all); the 24 personal-name findings and the one user-name finding
  (the owner's decisions). The list is `check-after.txt`.

## 9. The import of returning patches (ticket 4704, package E3b, 2026-11-18)

Plan sections 4.1 ("Back") and 4.2: a change that upstream takes under `64bit/<path>` applies here to `<path>`, with
the export tag as the common base. File: `tools/export/import_patch.py` (standard library and git; it uses
`check.py`'s allow-list parser and `export.py`'s path rules, so the import and the export judge paths alike). Tests:
`tools/export/tests/test_import_patch.py`. Both are private by omission from the allow-list, and so is the import
table `ops/export/imports.tsv`.

### 9.1 What it does [C: tests 9.3]

```
python -I tools/export/import_patch.py --upstream <upstream checkout> (--commit C | --range A..B)
       [--root <this repository's checkout>] [--onto main] [--branch NAME] [--allowlist FILE] [--dry-run]
python -I tools/export/import_patch.py --patch <file from git diff or git format-patch> [same options]
```

In order, stopping at the first refusal:

1. **The change.** From a checkout of the upstream repository, read only and never fetched: `--commit C` is the diff
   of C against its first parent, `--range A..B` the whole diff from A to B as one change (made by the script with
   `git diff --full-index --no-renames`, prefixes forced to `a/` `b/`, so local diff settings do not matter). Or a
   patch file from `git diff` or `git format-patch` (format-patch's `From <id>` and `Subject:` lines are kept for the
   ledger and the message). A file changed by two diffs of one patch file (a format-patch series) is refused: use
   `--range`.
2. **Refusals, every one named** (exit 1, nothing changed): a path outside `64bit/`; a path that the allow-list at
   `--onto` (or `--allowlist`) does not put in `product` or `notes` (the message names its group); a binary change
   (`Binary files ... differ` or `GIT binary patch`); a link (mode 120000) or submodule (160000); a path that
   `export.py` could not write safely; input with no `diff --git` header (a plain `diff -u`).
3. **The base tag.** The newest `export/*` tag (by commit date) at which every file the change modifies, renames or
   deletes has exactly the blob the change starts from (the patch's `index <old>..<new>` id; abbreviated ids of 7 or
   more digits compare by prefix), and every file it adds is absent. No such tag: refused, "base is not an export
   tag", with the mismatching files against the newest tag. So a change built on an upstream edit that never came
   back here is refused until that edit is imported. A change that only adds files has nothing to compare; the newest
   tag without them is taken and the script says so. With `--upstream` it also prints how many of the files under
   upstream's `64bit/` at the change's base equal the tag (information only).
4. **The branch.** The checkout given by `--root` (default: the script's repository) must have no uncommitted
   change to a tracked file and no merge in progress. The new branch (default `import/<tag name>-<source id>`, the
   upstream commit's first 10 digits or `p` + the patch hash) is created at `--onto` (default `main`) and checked out
   there. `main`, `master` and the `--onto` branch itself are refused as `--branch`, and so is an existing branch:
   main never moves.
5. **The apply:** `git apply --3way -p2 --whitespace=nowarn`. `-p2` strips `a/64bit/`. The merge base of the 3-way
   is the blob in the patch's `index` line, which step 3 proved is the tag's, so a file that also changed here since
   the tag is merged line by line (test: upstream's line 2 and this repository's line 19 of one file both land).
6. **A clean apply** is committed on the new branch (message: the first upstream subject, the tag and its commit,
   the source, the upstream commits with author and subject, the files; hooks run). Exit 0.
7. **A conflict** is left for a person in the checkout, on the new branch, with conflict markers and unmerged index
   entries; the files that applied cleanly are staged. The script prints a `conflict<TAB><file>` line per file and
   writes the commit message to `$(git rev-parse --git-path IMPORT_MSG)`; the person resolves, `git add`s and
   commits with `git commit -F "$(git rev-parse --git-path IMPORT_MSG)"`. Exit 3. A file added upstream that this
   repository also added since the tag is an add/add conflict of this kind (git 2.43) [C].
8. **An apply that fails without a conflict** (a patch whose hunks do not fit its own start blob) removes the new
   branch (`reset --hard` of that branch only, whose tracked files were clean, then back to the branch it started
   on, then `branch -D`); exit 2, no ledger line.
9. **The ledger.** After a clean apply or a conflict, one line is appended to `ops/export/imports.tsv` under
   `--root` (created with its header): `date, branch, status (applied | conflict), tag, source (upstream A..B with
   full ids, or patch sha256:<hash>), source_commits (comma-separated), files (the stripped paths, ';'-separated),
   commit (empty for a conflict), onto (the commit the branch started at)`. `ops/export/ledger.tsv` stays the table
   of export tags (the MC, 2026-11-18: imports in their own file beside it). Neither file is committed by a script.

stdout: `source`, `base`, `files`, `branch` lines (tab-separated), and `conflict` lines. Nothing is pushed, fetched or
sent anywhere. `--dry-run` stops after step 3 and the branch-name check: no branch, no ledger line. Exit codes: 0
applied (or dry run passed), 1 refused, 2 usage or git error, 3 conflict left for a person.

The merge into `main` stays the MC's step (plan 4.2). The import branch starts at `main`, so a clean import that is
merged before main moves again is a fast-forward.

### 9.2 Limits [I unless marked]

- The base test compares blob ids. A file that upstream stored with other line ends than ours (an upstream
  `.gitattributes` or `core.autocrlf` normalising a CRLF file of ours on commit) has another blob id and is refused as
  "not an export tag", though its text is the same. The export writes the committed bytes (3.1); whether upstream's
  settings re-normalise any exported file is not measured.
- Renames come in as delete plus add with `--upstream` (`--no-renames`); a rename in a patch file is read from its
  `rename from/to` lines, and a pure rename (no `index` line) is checked only for the source existing at the tag.
- Blob ids are compared as SHA-1 ids; that the upstream repository is a SHA-1 repository is assumed, not checked.
- `--whitespace=nowarn` keeps the bytes as they come; this repository's `apply.whitespace` setting is not applied.

### 9.3 Tests (2026-11-18, workstation, git 2.43.0.windows.1, Python 3.11.5) [C]

`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -X utf8 -m unittest discover -s tools/export/tests -p "test_*.py"` (started
through `perf/quietlock/quietlock.py run`): 55 tests (28 of the import, 27 of the export), OK in 205 s. Every repository
is a temporary folder: 'here' (core.autocrlf off, and once on, the workstation's setting), a bare 'origin' of it, and
an 'upstream' repository with its own files, into which `export.py --tag r1` exports 'here' and which then commits its
`64bit/` folder. The private terms are invented.

Covered: a commit from upstream that modifies, adds and deletes files applies with the prefix stripped on
`import/r1-<id>`, which starts at main; main does not move; the diff to main is exactly those files; the message names
the tag and the upstream commit; the ledger line's fields; origin has no branch but main afterwards. A true 3-way
merge (upstream line 2, this repository line 19 since the tag). A `format-patch --full-index` file, a plain `git diff`
with abbreviated ids, a two-commit `--range`. The newest matching tag is the base (r2 after a second export), and the
older tag when upstream never took the newer export. `--dry-run` makes nothing. With `core.autocrlf=true` the
committed blob keeps LF. A conflict: exit 3, the file named, unmerged entry and markers in the file, the clean file
staged, nothing committed, ledger status `conflict`, `IMPORT_MSG` written. An add/add conflict. A patch that does not
fit its own start blob: exit 2, the branch removed, the checkout back on main, no ledger line. Refusals with no
branch, no ledger line and a clean checkout: a path outside `64bit/` (upstream's `README.md`), unlisted paths (private
by omission, and `[unclear]`), a binary change (from `--upstream`, and a `--binary` format-patch file), a base that is
not an export tag (a second upstream edit on top of one never imported), no export tag at all, not a git diff, one
file twice in a patch series. Usage: `--branch main` and `master`, an existing branch, a checkout with an uncommitted
change (left as it was), `--upstream` naming this repository, `--commit` without `--upstream`. The parser: C-quoted
names with octal UTF-8 and spaces, an unquoted name with a space, a rename, a link refused by its mode. The
repository's allow-list puts `ops/export/imports.tsv`, `import_patch.py` and its tests in the private group.

Not verified: no import from a real checkout of the upstream repository (none exists here; a temporary repository
stands in), and no real export tag exists in this repository yet; an upstream that normalises line ends (9.2).

## 10. The unheld render tests, shell origins and notes (ticket 4716, packages E1d and E2c, 2026-11-18)

**2026-11-18 06:12 EST: 23 of the ticket's 25 files carry no user path and no account handle; the two left are URLs
that must stay (below)** [C]. Before (`main` 7c6f026c5, the same counts as on 9ca83ffd3, `check.py --require-terms
--no-env-terms`, groups product and notes, 5 private terms): 51 findings in 36 files; after (commit d2f86f216 of branch
`task-4716-export-user-paths`): 22 in 14; the 29 removed findings are all in this ticket's files and every other
finding is unchanged (the diff of the two outputs has deletions only; raw masked outputs
`tools/export/evidence/4716/check-before.txt`, `check-after.txt`).

| Files | Was | Now |
|---|---|---|
| `server/tests/render/conftest.py` (`exe` fixture), `test_residency_source.py` (`GAME_DIR`), `ice/test_ice_reader.py` | `C5R_EXE` / `C5R_GAME_DIR`, else a literal EXE copy and game folder under the user's home | the variable if set, else the settings `render_exe` / `render_game_dir` of `tools/release/settings.py` (section 4); with neither, a placeholder path that does not exist, so the test fails naming the variable: 4713's `release_setting` (section 6), one copy per module, because the subfolder modules run without the parent conftest (pytest's conftest cut at the rootdir when run alone) and no shared helper file was in the ticket's touches |
| `server/tests/render/fow/test_fow_scene.cpp` (`GameDir()`) and `test_fow_scene.py` | the program's own default: the game folder literal when `C5R_GAME_DIR` is unset or empty | the program's default is a placeholder; `test_fow_scene.py` passes `C5R_GAME_DIR` to the program, from the setting when the variable is unset or empty (`program_env()`) |
| docstrings of `fow/test_fow_scene.py`, `ice/scene_ice_test.py`, `overlay/test_scene_overlay.py`, `shadow/test_scene_shadow.py` | `set C5R_EXE=` / `set C5R_GAME_DIR=` with the user's home | `%USERPROFILE%\...` (cmd expands it; the rest of the path unchanged) |
| origin comments (rule 13, D22) of `shell/build.cmd`, `common/shaders.hlsl`, `common/shell_shared.h`, `composite/vtable_index.c`, `server/shell_server.cpp`, and `shell/README.md` | "repository `<handle>`/Community-Patch-DLL" | "the project's fork of Community-Patch-DLL" (branch, path and commits unchanged) |
| notes: `message-queue-and-fixed-buffers.md` 184, `proxy-design.md` 3182, `upstream-fix-batch-1.md` 10/483/917, `upstream-fix-batch-2.md` 63, `installer-notes.md` 636 | the handle, or the fork's `<handle>/Community-Patch-DLL` | "the project's DLL fork" / "the project's fork of Community-Patch-DLL" (`installer-notes.md`: "the project's DLL fork (its GitHub address)", what `NOTICE.txt` names) |
| notes: `model-reader-verification.md` 43, `shell-foundations-verification.md` 56 | `git clone https://github.com/<handle>/c5-64bit-patch.git ...` | `git clone <this repository's GitHub URL> ...` |
| notes: `terrain-surface-verification.md` 12, `offline-scene-harness.md` 219, 964, 1359 | the pins folder, the instances folder under the user's home | `<pins_dir>/dlls/...`, `%USERPROFILE%/c5-instances/...` by `tools/export/evidence/4716/scrub.py` (section 5's rules; 219 is the example manifest written for the note, no verbatim quote, so nothing is marked) |

Behaviour [C]: with no variable set, each changed file resolves the same EXE and game folder as its literal did; with
the variables set, each takes them; with `C5R_GAME_DIR` empty, the fow program gets the setting (it took its literal
before); with no setting at all (an empty `C5_SETTINGS_FILE`), a placeholder that does not exist
(`tools/export/evidence/4716/probe.txt` from `probe_paths.py`, home masked). The builds and suites:
`tools/export/evidence/4716/acceptance.txt` (shell build, `test_build_cmd.py`, `test_safety_static.py`, and
`server/tests/render/run_all.py` for the suites render-host, ice-reader, fow-scene, overlay-scene and shadow-ice-scene).
On 2026-11-18 06:15-06:40 EST: shell build OK, `test_build_cmd.py` 3 passed, `test_safety_static.py` 55 passed;
render-host PASS (143), ice-reader PASS (11, the setmesh case on the live images); fow-scene: `test_fow_scene_cases`
PASS (every listed case, `real_modules` and the particle cases on the game folder the runner now passes), but
`test_fow_scene_every_case_is_listed_here` FAILS on main as well: the program has four cases (`held_frame_off`,
`held_frame_prepares`, `held_frame_stale_dropped`, `held_frame_waits_for_styles`, main's 7ad09f896, ticket 0424) that
the file's `CASES` lacks [C]. overlay-scene and shadow-ice-scene did not run: their builds fail on main's
`render/include/c5r/pathmodels/pathmodels.h` line 31 (`'c5models/anim.h' file not found`; the header is 0300's,
10e0a1d6a / 9ed412280), which nothing in this branch touches; their modules (only docstrings changed) collect, 9 tests
[C]. Both breaks are reported to the MC in the hand-back.

Left for the MC (the hand-back): two URLs that must stay, the player-facing issue address
`https://github.com/<handle>/c5-64bit-patch/issues/new` in `proxy/helper/vp64helper.cpp` line 72 (the error popup's
text, D118) and in the beta-notes text quoted in `installer-notes.md` line 368; `README.md` line 44 (the copyright
line) and `shell/third_party/minhook/NOTICE` line 3 (a third-party notice), not changed by the ticket's rule. Still
held by other tickets on 2026-11-18 05:55 (E1/E2's last slice): `server-host.md` (6 user paths, 1 handle),
`proxy/src/proxy_shell.cpp` (2 handles), `shell/composite/shell_composite.cpp` / `.ini` (the same origin comment, 1
each), `server/tests/render/scenebundle/test_sources.py` (1), `research/08-performance/README.md` (1),
`x64-dll-build-configurations.md` (1), `release-driver.md` (1 handle), `human-input-testing.md` (1), `addresses.md`
(1).

## 11. The D83 audit of the notes (ticket 4717, package E2, 2026-11-18)

The read-only audit list is `upstream-export-d83-audit.md` (147 notes files at main 02605bab9): 26 passages in 13 files to replace by a one-line description, no game picture or save in the group. The MC's rulings on its judgement classes are in the 0468 issue log (2026-11-18 06:58): J3 as proposed, J4 keep, J6 replace, K5 keep; J1 and J2 kept for now and named as classes in the package for the owner's read before any export (plan 4.9, D27). (Added by the MC at 4716's merge.)

## 12. The first D83 edit slice (ticket 4718, package E2, 2026-11-18)

22 of the audit's 26 replace passages (the other 4 are in addresses.md), in 12 notes files, replaced by our own descriptions (the facts and addresses kept, each place with a dated trace); J3 (model name lists) and J6 (a harness-written save name) applied. Left: addresses.md's four passages (held by epic 0437) and server-host.md's J6 place (held by 0466). Evidence `tools/export/evidence/4718/` (hit lists before and after, no passage text). A wider D83 point is in 0468's log (07:40): EXE byte patterns in the product's own source, for the owner's read. (Added by the MC at 4718's merge.)

## 13. Home paths in every written form and in unexported files (ticket 0596, 2026-11-20)

**Why the gate passed 0591's leak [C, 2026-11-20 00:30 EST, workstation].** 0591's head 075c51277 carried a save
path under the user's home in `proxy/tests/luaexec/evidence/live-c5h-c-summary.txt`, written as a printed Python dict
writes it (`C:\\Users\\<name>\\...`, doubled backslashes). The ticket assumed the user-path rule misses that form; it
does not: the rule's separator already took any run of `\\`, `\` and `/`, and a probe of the old rule matched the
single, doubled and quadrupled forms. The real cause: `**/evidence/` is `[private]` in the allow-list and the gate
searched only the exported groups (product, notes), so the file was never read. Running the old check on 075c51277
(`--rev 075c51277 --no-terms --no-env-terms`) lists no finding in the file.

**What changed in `tools/export/check.py`.**

- The repository pass. Every tracked file outside the checked groups (`[private]`, `[unclear]`, unlisted: `tracker/`,
  `docs/`, `ops/`, every `evidence/`) is committed and pushed though never exported, so it is searched for `user-path`
  alone (no terms, no e-mail, no file types) and reported as group `repository`. On by default in the gate (`--since`):
  fail closed, the MC's ruling on the thread (#7455, 2026-11-20), and like the rest of the gate it fails only on
  findings new over the merge base. `tracker/` issue logs and `docs/MC-STATE.md` are in its scope (unlisted, so
  private by omission; a test commits both). `--repo-paths` turns it on in the other modes; `--no-repo-paths REASON`
  turns it off and needs a reason, printed on its own output line and at the end of the verdict line (`;
  repository pass OFF: <reason>`); without a reason it exits 2. The export script (`export.py`, no `--since`) is
  unchanged. In the gate a finding's key is (file, rule, text, whether the file is exported), so moving a private
  file with a home path into `[product]` or `[notes]` counts as new.
- More written forms of a Windows home path: the separator is now any run of `\`, `/`, JSON's `\u005c` and URL `%5C`
  or `%2F` (JSON's escaped slash `\/` is a backslash and a slash, so it was already a run). The POSIX forms
  (`/c/Users/<name>`, `/Users/<name>`, `/home/<name>`) also take escaped slashes (`\/`, `\\/`); their look-behind now
  refuses a backslash too, so an escaped URL (`host.example\/home\/x`) is judged by the character before its
  backslashes, as the plain URL is (not a finding).

**Counts on `main` 0afdb9394 before and after [C, 2026-11-20 00:30-00:40 EST, generic rules only:
`--rev main --no-terms --no-env-terms --groups product,notes,unclear --summary`, local `main` at 0afdb9394].**

| Group | Before (user-path) | After (user-path) |
|---|---|---|
| product (1,076 files) | 2 in 2 files | 2 in 2 files |
| notes (171 files) | 10 in 5 files | 10 in 5 files |
| unclear (399 files) | 114 in 37 files | 114 in 37 files |
| repository (14,461 files, new pass) | not searched | 32,793 in 3,140 files |

The new forms add no finding in the exported groups. The repository pass's old findings per top-level folder (unexported
files only; for a later clean-up ticket):

| Folder | Findings | Files |
|---|---|---|
| `server` | 14,542 | 1,218 (all `server/evidence`) |
| `tools` | 9,997 | 589 (`x64-dll` 3,921, `d10` 3,915, `harness` 1,759, `i0b` 272) |
| `ipc` | 2,266 | 81 |
| `research` | 2,104 | 392 (evidence subfolders of 03, 06, 05; `sources` 39 in 12) |
| `perf` | 1,982 | 312 (`evidence` 1,772) |
| `shell` | 845 | 256 |
| `tracker` | 556 | 124 |
| `render` | 272 | 111 |
| `ops` | 119 | 23 |
| `docs` | 78 | 20 |
| `deps` | 21 | 6 |
| `models` | 9 | 6 |
| root files, `evidence` | 1 each | 1 each |

By form (all groups): single backslash 18,623, doubled 10,585, forward slash 2,426, quadrupled 1,005, `/c/Users/` 255,
others under 15. They are the gate's base and fail no merge; a branch fails only on a home path it adds. Cleaning them
is not part of 0596.

**What the pass costs a gate run [indication, not a timing result: workstation, not quiet, the export suite running
beside it; 2026-11-20 00:52-00:54 EST].** The MC's gate command on the 0596 branch against `main` (merge base
e215e900c, 14,854 unexported files read at head and again at the base), three interleaved trials each: with the pass
median 35.1 s (33.0-45.3), with `--no-repo-paths` median 11.5 s (9.3-12.2): the pass adds about 24 s. Most of it is
reading every unexported blob twice through `git cat-file`; skipping the files whose path, blob and group are equal at
head and base (they add the same findings on both sides) would remove most of it, but changes the "findings the base
already had" count, so it is left for a follow-up if the MC wants it. A full check of `main` (no `--since`) with
`--repo-paths`: 18.8 s (00:35, one run). The export script's suite (`tools/export/tests`, 55 tests, check.py run without
`--since`) is OK on the branch (617 s, slow beside the other runs).

**The 0591 case through the new gate [C, 2026-11-20 00:31 EST]:** `python -I tools/export/check.py --rev 075c51277
--since main --allowlist tools/export/allowlist.txt --no-terms --no-env-terms` gives `FAIL: 2 new findings in 1 files`
(both on line 58 of the evidence summary; 32,919 base findings not counted).

**Tests** (`tools/tests/export/test_check.py`, 49 tests, OK in 104.5 s on 2026-11-20 00:50; invented user name `jdoe`;
the pass's skip without a reason exits 2; issue logs and `docs/MC-STATE.md` searched): every written
form (single, doubled, quadrupled, forward, JSON `\/`, `\u005c`, `%5C`, escaped `/c/Users/`, `/home/`, `/Users/`) caught
in a printed-dict line; the 0591 shape (a stop-readiness dict in a summary) caught at its line; escaped placeholders,
`Public` and an escaped URL with `/home/` not findings; the repository pass in tree mode (user paths in private and
unclear files found, a machine alias in a private file not; off by default outside the gate); in a temporary git
repository the gate fails on a doubled-backslash path in a new `evidence/` file and passes it with `--no-repo-paths`,
an old home path in a private file is not counted, and a private file with a home path moved into the export fails.

**Not verified / limits [I].** The pass reads file contents only (not file names, commit messages or history; the
whole-history grep at merges stays). A home path written with the user name split or encoded otherwise (UTF-16 without
a byte-order mark inside a text file, base64) is not found. `test_safety_static.py` has no user-path scan (searched
2026-11-20), so nothing changed there.

## 14. The product group's last user paths, and what is left of E1 (ticket 4702, package E1, 2026-11-25)

**2026-11-25 06:10 EST: the product group has no user path; what is left of E1 is eight findings in six files, every
one an export decision for the owner** [C]. Command: `python -I tools/export/check.py --rev HEAD --require-terms
--no-env-terms --summary` (5 private terms, no environment terms). Before (`main` ee9695f16): product 10 findings in 8
files; after (branch `impl-4702`): 8 in 6, the two removed ones the user paths below, nothing added (masked outputs
`tools/export/evidence/4702/check-before.txt`, `check-after.txt`).

| File | Was | Now |
|---|---|---|
| `server/tests/render/scenebundle/test_sources.py` line 13 (docstring) | `set C5R_EXE=` with the user's home folder | `%USERPROFILE%\...` (section 10's rule for these docstrings; the rest of the path unchanged) |
| `shell/third_party/minhook/NOTICE` line 3 (our provenance line, issue 3006, not MinHook's text) | the local clone's folder under the user's home | `a local clone (%USERPROFILE%/tools/minhook-v1.3.4)`; source, tag, commit and date unchanged |

Left in product, for the owner (`tools/export/evidence/4702/owner-decisions.txt`, one line each with the options; the
owner decided on 2026-11-25, D260, and ticket 0761 applied it, see the last paragraph of this section):
`README.md:44`, the copyright line (the owner's family name as `personal-name`, the first name as `user-name`); the
GitHub handle in four origin comments (rule 13, D22: `proxy/src/proxy_shell.cpp` lines 6 and 1176,
`shell/composite/shell_composite.cpp` line 1, `shell/composite/shell_composite.ini` line 1; section 10 rewrote the
same form in six other files) and in two player-facing addresses (`proxy/helper/vp64helper.cpp` line 88, the error
popup's issue address, D118; `proxy/src/proxy_session.cpp` line 568, the patch page in the version-mismatch message).
The ticket's text calls all six "origin-comment lines"; two are URLs that have to stay real addresses.

The release tests without the private settings file [C]: `tools/release/tests/test_take_release_major.py` (two tests
failed with no `pins_dir` before: `setting pins_dir is not set`) and `test_hi_journey.py` now set `C5_PINS_DIR` and
`C5_CLONES_DIR` to temporary folders and `C5_SETTINGS_FILE` to an absent file (the first in its base fixture, the
second in `setUpModule`; old values restored), as `test_take_release.py` has done since 4711; they pass with
`C5_SETTINGS_FILE` naming an absent file, as in an exported checkout (2026-11-25 06:45-07:01 EST: `-p
"test_*major.py"` 41 tests OK in 55 s; `test_hi_journey.py` 96 tests OK in 1,225 s on a loaded workstation;
`test_safety_static.py` 55 passed; `tools/export/evidence/4702/acceptance.txt`). `tools/d10/tests/test_fork_setting.py`: the stale
docstring (section 4) corrected, and its no-user-path test now covers `testdata.py` too. Not decided here: whether
`deps/local-settings.ini` becomes an ignored file with a committed `.example` (E5's question, section 4).

Still open outside product (E2, notes; not this ticket): 14 findings in 8 notes files on the same commit, 10 user paths
(`server-host.md` 6, `addresses.md`, `human-input-testing.md`, `x64-dll-build-configurations.md`,
`research/08-performance/README.md` 1 each) and 4 handles (`new-exe-build-26577625.md`, `installer-notes.md`,
`release-driver.md`, `server-host.md`). All but `installer-notes.md`'s are gone since 0761 (next paragraph).

**2026-11-25 15:56 EST, ticket 0761 (D260): the export check is down to 5 findings in 4 files, none of them this
ticket's** [C]. Command as above without `--summary` (5 private terms, no environment terms). Before (`main`
0b11dd083): 22 findings in 14 files; after (branch `task-0761-export-findings`): 5 in 4 (masked outputs
`tools/export/evidence/0761/check-before.txt`, `check-after.txt`). The 5 left: the handle in the four origin comments
of `proxy/src/proxy_shell.cpp` (lines 6, 1176) and `shell/composite/shell_composite.cpp` / `.ini` (line 1), ticket 0769
(files held by 0042 and 0758), and `installer-notes.md` line 388, ticket 0767 (the file is held by it). What changed:

| Where | Was | Now |
|---|---|---|
| `README.md` line 44, the copyright line | the owner's full name "and contributors" | `Copyright (C) 2026 VP64 contributors.` (D260: no personal name) |
| `README.md` line 40 | the DLL fork named by the owner's first name | "the project's Community-Patch-DLL fork" (D260's rule for the rest of README.md) |
| `proxy/helper/vp64helper.cpp` line 88, the error popup's default hand-in line (D118, D122) | the DLL fork's `issues/new` | `https://github.com/LoneGazebo/Community-Patch-DLL/issues/new` (upstream) |
| `proxy/src/proxy_session.cpp` line 568, where the version-mismatch summary sends the player for an update | this repository's page | `https://github.com/LoneGazebo/Community-Patch-DLL/releases` (upstream) |
| `proxy/tools/rt_tests/session_unit.cpp` line 489, `proxy/tools/test_vp64helper.py` line 171 (tests; added to the touches by lead-0763, #15267) | assert the old links | assert the new ones |
| 7 notes (`addresses.md`, `server-host.md` 6, `human-input-testing.md`, `x64-dll-build-configurations.md`, `research/08-performance/README.md`) | user paths | section 5's rule: `<pins_dir>/...` under the pins folder, else `%USERPROFILE%/...` (no verbatim log quote among them) |
| `server-host.md` line 839 | "on the <handle> fork only" | "on the fork only" (section 7) |
| `new-exe-build-26577625.md` line 466, `release-driver.md` line 1036 | the old links, quoted as the popup's text | the new links, with a dated trace of the old ones |

No allow marker was added to `check.py`. Not changed (group `unclear`, never exported, and outside the touches): `proxy/tools/test_vp64helper.py`
line 368 still spells the handle, as the term a negative check looks for in the popup's lines (`check.py --groups
unclear` on the branch: 124 findings in 50 files, that line one of them) [C, 2026-11-25].

**2026-11-25 20:20 EST, ticket 0775 (D263 (C)): the update links name the fork's releases page, and the export check
has one named exception for the fork's two public addresses** [C]. The owner's D263 (C) amends D260 for the update link
only: until upstream merges the pull request, where a player looks for an update of the patch is
`https://github.com/<handle>/Community-Patch-DLL/releases`. Changed to it (branch `impl-0775`): `proxy/src/proxy_session.cpp`
`kWhere` (the version-mismatch summary, line 570; it was upstream's releases since 0761 above), `tools/installer/patch64.iss`
`AppPublisherURL` (line 42) and the unknown- and unsupported-build messages and links of `ExeBuildCheck` (lines 381-394),
and the "New releases of the patch are on" sentence of `docs/INSTALL.md` section 8 and `docs/BETA-TESTERS.md` section 8
(plus a pointer to section 8 in `BETA-TESTERS.md` section 2). Unchanged and still upstream's: every bug-report link
(`AppSupportURL`, `ReportLink`, the error popup's, the docs' `issues/new`; D260) and VP's own release links
(`vp_releases.tsv`, `build_installer.py` `UPSTREAM`, the docs' `Release-5.5` tag). The tests that assert the links follow:
`session_unit.cpp` line 489 and `tools/harness/tests/test_installer.py` (`PATCH_RELEASES`; the 0588 unknown-build refusal
names it; the support-link test now requires that the script's only non-upstream GitHub address is the releases page,
5 times); `tools/installer/tests/test_vp55.py` asserts no update link and was not changed.

- The exception, `tools/export/check.py` `PUBLIC_FORK_RELEASES` and `PUBLIC_FORK_REPOSITORY` (D263 and MC #15669
  recorded beside them; the repository address because `deps/dll.lock` is exported): a match of a private-term rule
  (user-name, machine-name, personal-name, private-term) lying wholly inside one of the two addresses, written exactly
  (case-sensitive, `https://`, the repository address with or without `.git`, followed by nothing that continues a URL
  path: `/`, `\`, `#`, `?`, a letter or digit, `.` plus one), is no finding. The handle anywhere else is still one: the
  fork's other addresses (`issues/new`, `releases/tag/...`, `tree/x64`), `git@` and `http://` forms, another case, the
  handle on its own, this repository's own address. Other rules on the same line are not affected. No allow marker is
  used. `tools/export/tests/test_check_public_url.py` (new, 10 tests, 0.4 s; it takes the handle from `check.py`'s
  constants and so spells none) tests both sides, plus a `--tree` run of the command line.
- Measured 2026-11-25 20:04-20:20 EST [C]: `check.py --rev impl-0775 --since origin/main --require-terms` (5 private
  terms, 2 environment terms): PASS, 0 new findings. With an allow-list (outside the repository) that puts the 9 touched
  files and `deps/dll.lock` in product: 18 findings, none of them the handle (all the environment's user name, plus one
  user path in `deps/dll.lock` line 269, older than this ticket: a home-folder path in a pin note, which the lock's export
  has to clean); the same run with `main`'s `check.py`: 31, the 13 more being the handle in exactly these lines
  (`deps/dll.lock` 66, the two docs, `kWhere`, `session_unit.cpp`, `test_installer.py`, `patch64.iss` 5, `check.py`'s
  two constants). `tools/export/tests` 100 tests OK (1,050 s); `tools/tests/export/test_check.py` 49 OK (117 s);
  `proxy/build_proxy.py` (runbook 1.4) OK in 109 s; `test_proxy_session.py --only unit` 9/9 (session_unit 141/141);
  `test_proxy_rt.py --only-offline` 4/4 (test_proxy_rt has no check of this link; its full run needs a server build and
  was not made); `test_installer.py` 61/61 (361 s); `test_vp55.py` 10/10 (246 s); `test_safety_static.py` 55 passed.
- Remove both constants and their tests when upstream merges (the links then move to upstream, D263).

## 15. The owner's first name out of every exported file, and the export refusing on environment terms (ticket 0773, 2026-11-26) [C]

**2026-11-26 19:20 EST: `check.py --require-terms` WITH the environment's names passes on the product and notes groups
(0 findings in 1,454 product and 220 notes files); export.py and package.py refuse on any environment-term finding.**
The owner's yes is D262 (2026-11-25); D278 (2026-11-26 18:5x) moved the edit ahead of the cut so the fork's public `x64`
branch could go out the same day. MC #18024 approved the dry run, the named replacements and the exception below.

- Before (`main` 1c25545bc, this machine's 5 private terms and 2 environment terms): 855 `user-name` findings, all the
  owner's first name (the workstation's user name): product 72 in 38 files, notes 783 in 133. No other rule fired.
- The edit: `tools/export/evidence/0773/scrub_name.py` (it takes the name from check.py's environment term and spells
  it nowhere). In every product and notes file of the allow-list at HEAD (never `[verbatim]`, `research/sources/`, the
  decision log or the Architect's proposals, which are not exported anyway), the name as a whole word, case-sensitive,
  became "the owner" ("the owner's" for the possessive; "The owner" at a sentence, list, heading or table-cell start);
  in code files only inside comments and Python docstrings. 165 files, 797 occurrences, plus 30 named replacements
  listed in the script's `SPECIAL` table: 23 where the word is the game's art and not the owner (art rows, art style,
  the art library, the art column of a table: written "The art"), 4 where the plain rule reads wrongly ("the seven
  `art_*` cases", "no question for the owner"), 2 user-folder names (`<user>`), and `exact_t`'s class name in `unit-renderer.md`
  written as its alias `ArtFiles` (0776's). On the lines the scrub changed, the owner's pronouns became they/them/their/themself
  (MC #18024; 10 words). `scrub_name.py --verify` compares each changed code file with its blob at the base, comments
  and docstrings removed on both sides: 29 code files, no other difference (and it fails on a planted string change in
  a Python and a C++ file).
- Not comments, so left by the scrub (28): 25 are the game's art folder in asset-path literals of tests (`render/tests`
  8, `server/tests` 17), now a named exception of check.py; 3 in `shell/tests/test_terrain_scene.py` (a planted save
  named after the owner and the asserted refusal text) were changed by hand in the test only: the save is `Private`, the
  asserted text the tool's "series' own copies" (MC #18024 (B); `pytest -k s7` 14 passed). That test file imports the
  private `shell/evidence/3200/w5_series_3207.py` at module level, so it cannot run in an export as it is [C, by
  reading; not fixed here].
- The exception, `tools/export/check.py` `GAME_FOLDER_NAMES` and `game_folder_segment` (MC #18024 (A)): a `user-name`
  match of exactly the game's folder word written as one segment of a path is no finding: followed by `/` or `\` and a
  letter, digit, `_` or `(` with a quote, `>`, `/` or `\` before it; or after `/` or `\` with a quote, backtick, `<`,
  `/` or `\` after it; or a quoted segment of a Python path join. Not when the next segment is a profile folder
  (`Documents`, `AppData`, ...). In prose, alone, after `-` or a backtick, at a line start, or under another rule
  (machine, personal, private term) it is still a finding, and a user folder is still a user path.
  `tools/export/tests/test_check_game_folder.py` (7 tests, 0.5 s; the word is taken from check.py) tests both sides
  and a `--tree` run.
- The refusal: export.py runs its check with the environment's names always; `--no-env-terms` is accepted only with
  `--dry-run --allow-findings` and then marks the copy NOT AN EXPORT. package.py passes no `--no-env-terms` to export.py
  or to its findings list, and refuses on a finding of its `check-env-terms.txt` run as a second guard (unless
  `--allow-findings`). Tests: `test_export.py` (an environment user-name and machine-name finding refuse a plain, dry
  and tagged run with nothing written; `--no-env-terms` refused in three forms; the trial copy marked),
  `test_package.py` (an environment-name finding refuses, and `--allow-findings` packages it as NOT AN EXPORT with the
  finding listed masked); the test environments now set invented user and machine names instead of `--no-env-terms`.
- Also in this ticket: `deps/dll.lock` moved from `[unclear]` to `[product]` (MC #15669 on 4706; the check with the
  environment's names has no finding in it); 0772's two leftovers: `city-renderer.md` 10.1, the instruction bytes of the
  three create-site steps described in words with a dated trace, and the work-drive paths there and in
  `proxy-design.md` 34.1 written as `<work folder>`; 0769's D83 passages were already on `main` (c74211ef9).
- Commands: `python -I -X utf8 tools/export/evidence/0773/scrub_name.py --dry-run|--write|--verify`; `python -I -X utf8
  tools/export/check.py --rev <branch> --require-terms` (the full check with environment terms);
  `python -I tools/export/check.py --rev <branch> --since main --require-terms --no-env-terms` (the merge gate, form
  unchanged).
- Not verified: the sentence-start rule was read on all 794 changed lines of the dry run, not proved; a name that a
  future note writes in prose fails the export (that is the intent), while the merge gate, which still runs without
  the environment's names, does not catch it at merge time.

## 16. The `64bit/` README from the owner, and the general contributor guide (2026-11-26) [C]

The owner's answers on the public `x64` branch before its first push (one-off session with the owner, 2026-11-26
20:00-20:30 EST). They hold for every later export unless the owner changes them.

- **The README's top is the owner's text.** The repository root `README.md` (exported as `64bit/README.md`) opens with
  the owner's own section, from `# VP64` (the owner changed "# Introducing VP64" to "# VP64") through "What it is",
  word for word except three typo fixes the owner approved: "initiative", "the EXE's farm-field", "natural wonders"
  (was "NWs"). Edit that section only with the owner's yes; the sections below it (the three programs, what is where,
  where to start, "If you are an agent", rules the code keeps, notes, bug reports, licence) are ours and follow the
  usual rules (D95: no internal ids in public text beyond what `research/README.md` explains).
- **The diagram link** points at `research/07-architecture-decisions/ownership-end-product.svg` (the owner's draft
  wrote the bare file name; the SVG is not copied to the root).
- **Bug reports** stay on the upstream issue tracker (D260).
- **Contribution guides.** The extras guide (`CONTRIBUTING-extras.md`, the pull request template and the issue form)
  is done and exported since 2026-11-20 (D212). A general contributor guide for the `64bit/` folder is to be written
  by the Architect at the owner's request (2026-11-26), through the MC; until it lands the README's "Rules the code
  keeps" and "If you are an agent" are the only general guidance.
- Every file the README links to is in the export (checked against the 37d7ccf51 package's `stage/64bit/`).
- **The decision digest hides internal operations (the owner, 2026-11-26 ~20:45 EST).** In
  `research/07-architecture-decisions/decision-digest.md`, every decision about commissioning or using specific
  machines, budgets (agent counts, usage limits, plan allowance, context cost), switching account places, or the
  management hierarchy and role changes reads `&lt;internal operations decision&gt;` (HTML entities, so the angle
  brackets show on GitHub instead of being read as a tag); its number, date and status stay. 71 of D1-D261 at the
  first pass: D11, D12, D14, D37-D42, D48, D50-D53, D55, D59, D61-D64, D68-D72, D74-D77, D84, D90, D97, D98, D100,
  D101, D103, D105, D106, D108-D111, D113, D115, D117, D126, D128, D131, D133-D135, D139-D142, D152, D153, D167,
  D175, D177, D182, D188, D196, D201, D204, D219, D226, D227, D238, D244, D251. Kept as not in those classes (owner may
  move any): process rules such as D43, D45, D47, D54, D73, D129, D206, D217, and decisions whose subject is technical
  or product but that name a machine in passing (D107, D163, D178, D254, D258). The MC applies the same rule to every
  decision it adds to the digest. The full text stays in the private decision log; the notes still cite these
  numbers.
- **Operations notes are not published (the owner, 2026-11-26 ~20:50 EST).** Notes that do not affect the end
  product and only record how the project's operations run (the tracker, the queue, the workstation and the test
  machines, work planning) stay out of the export. The Architect's proposals, `decision-log.md`, the worker-account,
  queue, tracker-server, quiet-testbed, remote-machine, capacity, D-drive, desktop-app, session-hook and
  effectiveness notes were already out (`[private]` or `[unclear]`). Moved to `[unclear]` the same day (10 files):
  `03/effort-and-packages.md`, `06/backlog-triage-2026-11-24.md`, `06/reverse-engineering-setup.md`,
  `06/release-large-instance-difference.md`, `06/silent-start.md`, `06/visual-checks.md`,
  `07/epic-0040-breakdown.md`, `07/epic-0042-breakdown.md`, `08/cpu-time-and-affinity.md`, `08/machine-commit.md`.
  The section READMEs still list them (the link has no target in the export, as `research/README.md` explains).
- **The owner's verbatim words are not published (the owner, 2026-11-26 22:1x EST; a decision number is the MC's to add).** Every
  quotation of the owner in an exported file (notes, comments, docstrings) is replaced by `[reworded for privacy]` and
  a paraphrase of what they meant; product labels the owner chose ("Remove 64bit patch", option names), option texts
  put to the owner in an interview, decision texts, tool output and other people's words stay. First pass on main
  1d0fc9061: 76 places in 47 files, plus 1 in a note main brought in the same evening (77) (four Sonnet agents on disjoint file sets, each reviewing every `owner` hit in the
  298 exported files that mention the owner; then checked here). The originals with their replacements:
  `research/sources/owner-words-reworded-2026-11-26.md` (private). `research/README.md` explains the mark to readers.
  Not covered: a quotation with no quote marks and no attribution within a few lines; string literals (none found).
  The check after the pass: over every exported file, a pattern for an attribution to the owner (`the owner`,
  `the owner's`) followed in the same sentence, within 120 characters, by a word such as said, wrote, asked,
  replied, words, request or answer, or a colon, comma or parenthesis, and then a double-quoted passage of 4 or more
  words. It found 19; each was read: 18 are option help text, log or tool output, code strings, our own earlier
  wording or a pull request's title, and one (a beta player's bridge report relayed by the owner, `city-renderer.md`
  13.11) was reworded too, as possibly the owner's words. Claimed tickets whose touches hold an edited file
  (2026-11-26 22:4x): 0042 (lead-0042), 0749, 0812 (dispatcher-B), 0795 (dispatcher-A); quote-only edits.
- **Source upstream per release (the owner, D280, D281, 2026-11-26, through the MC).** Once `x64` is public, the
  source goes upstream once per installer release: a new pull request if none is open, otherwise a push to the open
  pull request's branch; until the first one merges, every release adds to the initial pull request of 2026-11-27;
  nothing between releases. The owner's OK on a release is also the yes to push that release's source, if
  `check.py --require-terms` and the D95 greps pass. A new pull request's text, and the first public `x64` push
  (D278), are still asked separately.
- **The general contributor guide** is the Architect's `CONTRIBUTING.md` (draft of 2026-11-26, SHA-256 3dc2a6dd...),
  exported as `64bit/CONTRIBUTING.md` and linked from the README; any change to its text goes to the owner first.
  The owner approved it as written ([reworded for privacy]: approved, 2026-11-26 ~21:05 EST).
- **Test-infrastructure notes stay published (the owner, 2026-11-26 ~21:05 EST [reworded for privacy]: keep the test notes).** The notes on the
  human-input harness, running and driving the game, headless and parallel testing, the validation tiers, the
  metrics ledger, the test-health sweep and the runbook stay in the export: the README and `CONTRIBUTING.md` send
  contributors to them for commands and test methods, even where the tools they name are not exported.

## 17. The tools the extras guide names go with it; the content check stays with the maintainer (ticket 0815, 2026-11-26) [C]

`CONTRIBUTING-extras.md` (2.1 E1, 4.4 step 3) and the pull request template (E1 boxes) tell a contributor to run
`tools/d10/statscmp.py` and `tools/d10/savecmp.py`, and the guide (2.6 E10) and the template (E10 box) told them to run
`tools/export/check.py`; none of the three was exported (`tools/d10/` was `[unclear]`, `tools/export/` private by
omission). Branch `task-0815-export-guide-tools`, commit 3cd56a20b on base `main` bb55c585e.

- **Exported now** (`[product]` of `tools/export/allowlist.txt`): `tools/d10/statscmp.py`, `tools/d10/savecmp.py`,
  `tools/d10/tests/test_statscmp.py`, `tools/d10/tests/test_savecmp.py`. Because `[unclear]` wins over `[product]`,
  the `[unclear]` line `tools/d10/` became one line per remaining file (`README.md`, `collect.py`, `run_d10.py`,
  `selftest.py`, `tests/data/`, the four `test_run_d10*`/`test_fork_setting`/`test_pin_defaults` tests); `evidence/`
  and `__pycache__/` stay private by the `[private]` rules.
- **The read for private content and D83** (by reading all four files, then the check): standard library only; no user
  path, name, e-mail or LAN address; no EXE byte. `savecmp.py`'s structure constants and the source lines it cites are
  the DLL's own serialization code (open source), not the EXE. Left as they are (not private, D95 noise only): ticket
  ids and decision numbers in the docstrings, and mentions of unexported files (`tools/i0/ab.py`, `tools/d10/evidence/`).
- **The two test saves stay** (`tools/d10/tests/data/*.Civ5Save`, `file-save`, a rule that cannot be silenced; a save
  is game output and carries the game's own data). So `test_savecmp.py` now skips its `SaveCmp` cases with the reason
  "the two test saves of tests/data/ are not present (they are not exported)" and adds `SyntheticSave` (6 tests: a
  save of the same container format built in code, no game data): the parts read back, identical, identical content
  (another zlib stream, same content), the version-string exception (a), and different for a body byte, the header
  turn, the pregame, a longer body and trailing bytes after a final block, plus a refused non-save. In the repository:
  44 tests OK (`python -I -m unittest discover -s tools/d10/tests -p "test_s*cmp.py"`, 55 s). In a scratch export
  (`export.py --dry-run` of 3cd56a20b into `F:/c5-64bit-work/0815/export`, then in its `64bit/`
  `python -I -m unittest discover -s tools/d10/tests -v`): 44 tests, OK, 24 skipped (the saves), 0.5 s; both tools'
  `--help` run with `python -I`; no import outside the standard library.
- **The content check is not exported; the guides say the maintainer runs it** (acceptance (2), second option). Why:
  (i) in the public tree the files live under `64bit/` while the allow-list's patterns are written from this
  repository's root, so a contributor's `check.py --rev HEAD --since <base>` would select no file and pass on nothing;
  (ii) its name rules need the private terms list, which never leaves this repository, so a contributor's run checks
  less than the maintainer's; (iii) a returning pull request comes back through `import_patch.py` (section 9) onto a
  branch here, where the merge gate (`check.py --rev <branch> --since main --require-terms`) runs on it anyway.
  `CONTRIBUTING-extras.md` 2.6 E10 now asks the contributor to read `git diff --stat <base>` for pictures, saves, logs,
  dumps, captures and paths into their user folder, and says the maintainer runs the content check on every pull
  request and why it is not in the folder; the worked example's E10 line and the template's E10 box say the same.
  Exporting `check.py` would need a path-prefix option and an allow-list that names only exported paths: not done.
- **Acceptance (3)** on 3cd56a20b [C]: `python -I -X utf8 tools/export/check.py --rev HEAD --require-terms --summary`
  (5 private terms, 2 from the environment): PASS, 0 findings, product 1,459 and notes 221 files (base bb55c585e:
  1,455 and 221; the 4 new files are the difference). Merge gate `--since main --require-terms --no-env-terms`: PASS, 0
  new findings. `package.py --rev HEAD --dll-branch export-e5-dll-one --fetch --out C:/c5-64bit-work/0815/package2`
  (invented no-reply author; nothing pushed; 96 s): 1,680 files; `files-by-group.txt` lists the four `tools/d10/`
  files, `CONTRIBUTING-extras.md` and `.github/`; both export checks 0 findings; the public-text check of the texts no
  hit; every commit check OK. (A first run without `--fetch` stopped at "--dll-branch export-e5-dll-one: not found in
  the fork clone" after writing the file lists: the fork setting's clone does not hold that branch locally.)
- Not verified: the guides' commands as run inside an upstream checkout's `64bit/` with a real `stats.db` pair (the
  tools were run only on the tests' inputs).
