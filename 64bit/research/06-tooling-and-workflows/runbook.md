# The runbook of commands

One file of exact, copyable commands, grouped by what a worker wants to do (decision D233, `docs/briefs/COMMON.md`
rule 32). Look here before you search the repository or start a helper. It is an index: each entry links the file
that explains the command; it does not repeat the explanation.

First version: one-off 9080 (the librarian), 2026-11-21, from the scripts' `--help` and headers, the research files
of sections 06 and 08, the evidence scripts beside merged tickets, the tickets' logs and the auditor's incident list
(`ops/effectiveness/incidents.jsonl`). Nothing heavy was run to write it: no build, no game, no take (the first
beta had the workstation).

## How to read and keep this file

- **Form of an entry.** `### I want to ...`, the command, then one line: what it needs first · about how long ·
  where the output goes · the last ticket and date known to have run it · the confidence mark · the file that
  explains it.
- **Confidence marks.** `[L]` ran today (the date in the entry) in the form given, or, for a build or game command,
  its `--help` ran today and the options are copied from it (the entry says "help"); `[C]` read from the script or
  its README; `[I]` taken from a ticket's log or an evidence script that ran; `NOT VERIFIED` where nothing confirmed it.
- **Where it runs.** From the root of your own checkout or worktree, in Git Bash, on the workstation, unless the
  entry says otherwise. A PowerShell or cmd form is given where it differs.
- **Placeholders.** `<t>` your ticket; `<inst>` a pool instance (c5h-a ...); `C:/c5-64bit-work/<t>` your work folder
  (C: only for work, D: is storage only since D133, F: holds instances and large archives);
  `%USERPROFILE%/c5-ops` the ops folder outside git (queue store, session log, tracker server state);
  `<src>` a DLL source tree at the pin (section 1.1).
- **Keep it current (rule 32).** When you work out a command this file lacks, or find that one here fails, write
  the working form here before your hand-back, with your ticket and the date, and say so in your "time lost" line.
  A command whose script is only on an unmerged branch carries the words `NOT ON MAIN` on its line.
- **It cannot rot silently.** `tools/harness/tests/test_runbook_paths.py` fails when a repository path or a link in
  this file no longer exists. Run it after editing:
  `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/harness/tests/test_runbook_paths.py`
  (about 1 s; `[L]` 2026-11-21).

## Contents

0. [Traps that cost the most time](#0-traps-that-cost-the-most-time) (read once)
1. [Builds](#1-builds): pins and sources, server.exe, proxy, shell and composite, render, x64 DLL, 32-bit DLL,
   43-civ variants, release take, validation tiers, installer, EXE build table
2. [Tests](#2-tests): settings every suite needs, suite by suite, ASan, shared sources and their builders
3. [Test instances](#3-test-instances): pool, provision, layouts, launch, load, turns, Lua, stop, boxes, sound,
   human-input journeys, headless D10, test machines, the land/water switch-off check, a save for one run, the restore
   check before a release, keeping a long session's run folders
4. [Capture](#4-capture): Visual Checks, scene player and bundles, taps, trigger log, RenderDoc, crash evidence,
   replays from F: in quiet windows, every displayed frame of a visible window (dupcap), stock against product with the
   same inputs on the laptop (capture_job), the lockstep's numbers on a hidden workstation instance, the server's
   pass split by stage, the terrain stage's parts (live logs and offline per draw), stock against product interface
   pictures on one hidden instance
5. [Measurement](#5-measurement): quiet lock, turn benchmark, testbed series, memory, metrics ledger, disk, studies
6. [Coordination](#6-coordination): queue, tracker, hand-back and merge request, privacy grep, monitors, worktrees,
   session log, export

---------------------------------------------------------------------------------------------------------------------

## 0. Traps that cost the most time

Each of these cost several workers time between 2026-11-11 and 2026-11-21 (auditor's incidents, tickets' logs).
The working form is first.

- **Fresh worktree: build the harness binaries before the first launch, lay or hold.** `tools/harness/native/bin` (created by the command)
  is git-ignored. Git Bash:
  `cmd //c "$(cygpath -w "$PWD/tools/harness/native/build.bat")"` · about 1 min · 12+ incidents, about 50 min ·
  `[I]` `research/06-tooling-and-workflows/evidence/3507/scripts/build_harness.sh` · do NOT add
  `MSYS_NO_PATHCONV=1` to `cmd //c` (0587: built nothing). PowerShell: `cmd /c tools\harness\native\build.bat`.
- **Exit codes hidden by `| tail` and `;`.** Start driver scripts with `set -u -o pipefail`, chain with `&&`, write
  suite output to a file and read its summary line. About 47 min lost, 17+ incidents (merges committed after a
  failed merge among them). [EFFECTIVENESS.md](../../docs/briefs/EFFECTIVENESS.md) lines 94-97.
- **`python -I` everywhere, with `-X utf8`.** `-I` ignores `PYTHONIOENCODING` (cp1252 crashes on non-ASCII output):
  `python -I -X utf8 <script>`. `python -I -m unittest <file>.py` cannot import: use
  `python -I -X utf8 -m unittest discover -s <dir> -p <file>`.
- **pytest: always** `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider <path>` (a
  third-party plugin on this machine crashes collection and the notice still says exit 0). PowerShell:
  `$env:PYTEST_DISABLE_PLUGIN_AUTOLOAD='1'; python -I -m pytest -q -p no:cacheprovider <path>`.
- **Git Bash rewrites arguments that start with `/`.** `cmd /c` becomes a path: write `cmd //c "<windows path>"`;
  `-D` defines go through `CL`; Lua paths like `/InGame/...` need `MSYS_NO_PATHCONV=1`; write `C:/...` not `/c/...`
  for any argument a Python tool compares; `tar` on `C:/...` needs `--force-local`; a bare `build.bat` or `./x.exe`
  is "not recognized": give the full Windows path (`$(cygpath -w ...)` or `$(pwd -W)`).
- **Here-documents and inline Python eat backslashes** (`\n`, `\0`, `\b`) and break on apostrophes: about 35
  incidents. Write anything multi-step as a script file with the Write tool; edit source with the Edit tool; use
  `<<'EOF'` (quoted) when a here-document is unavoidable.
- **Long runs go through the quiet lock** and need `--who` and `--what` and an absolute executable path:
  `python -I perf/quietlock/quietlock.py run --who <t> --what "<text>" -- <command>` (section 5.1).
- **A timing hold (`run --take`) needs MC-B's window first** (thread post; he empties the machine, games included): a take
  on a busy machine waits 25 min and then holds a DISTURBED, refused-elsewhere lock. Inside the hold, `--who` must equal
  the `C5_QUIETLOCK_WHO` your launches carry, or the harness's launch gate refuses your own games ("the quiet-machine lock
  is held") · 0655, 2026-11-21, about 25 min lost · `[L]`.
- **Stopping your own stale registered run** (`taskkill` is refused on account B, and forbidden for games anyway):
  `C5_JOB_OWNER=<t> python -I perf/quietlock/quietlock.py stop --pid <pid>` · 25 min lost once (4752, 2026-11-17).
- **One `frontend prepare` per launch:** the load token is used up by one launch (section 3.5).
- **After `ticketq.py post`, wait with `--since`,** never `--from-now` (it skips an answer already posted; section 6.1).
- **Set the owner in every shell that starts work:**
  `export C5_JOB_OWNER=<t> C5_POOL_OWNER=<t> C5_QUIETLOCK_WHO=<t>` (PowerShell: `$env:C5_JOB_OWNER='<t>'` and so on).
- **The `--config` defaults differ:** `server/build_server.py` defaults to debug, `proxy/build_proxy.py` to release.
  Always pass `--config release` unless you want debug.

---------------------------------------------------------------------------------------------------------------------

## 1. Builds

Rules for every build here ([release-builds.md](release-builds.md) 6.5, [x64-dll-build-configurations.md](x64-dll-build-configurations.md)):

- `server/build_server.py`, `proxy/build_proxy.py`, `render/build.py` and `tools/x64-dll/build_all.py` call the launch
  gate themselves: exit 3 = another team holds the quiet lock; `python -I perf/quietlock/quietlock.py wait`, then run
  again. Builds without their own gate (`shell/build.cmd`, the 32-bit `build_vp_clang.py`, the installer) are started
  through `quietlock.py run` (section 5.1).
- One whole-program (LTO) link at a time, machine-wide (D121): `tools/x64-dll/build.py --config release` and
  `pgo-instr` take the place themselves and wait in place (progress line every 5 min naming the holder; gives up after
  60 min with exit 4, `--link-place-wait MIN` or `C5_LTO_LINK_WAIT_MIN`; the objects stay, `--link-only` links later).
  See the holder: `python perf/quietlock/linkplace.py status`. `tools/release/linkslot.py` is retired; do not use it.
- Outputs go outside the repository (every builder refuses `--out` inside it), on C:.
- The EXE address tables must exist locally (setting `exe_tables`, env `C5_EXE_TABLES`, D202): every builder runs
  `tools/exebuild/emit_header.py` first and fails naming the setting without them (section 1.12).
- This machine's settings (paths of the pins, clones, tables, game copies): `python -I tools/release/settings.py`
  prints them (`deps/local-settings.ini` or env). The DLL source default: `python -I tools/x64-dll/dll_source.py`.
- Toolchain: LLVM 19.1.7 (clang-cl, lld-link), VS 2022 (MSVC 14.4x), Windows SDK 10.0.26100.0, VS 2008 (VC9) for every
  x86 DLL-side build, CMake + Ninja (render), Inno Setup 6.2.2 (installer, pinned by SHA-256), Python 3.11.
  `VSCMD_SKIP_SENDTELEMETRY=1` (bug 0108) is set by the builders themselves.

### 1.1 I want a DLL source tree at the pin (every x64 build needs `--src`)

Fastest: the shared read-only pin folder. **Main's pins since 0765 (VP 5.5, x64-vp55 a2627c8ac):**
`C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac` (the source, fingerprint v1 94579acb...) and
`C:/c5-64bit-work/keep/pins-5.5/deps-x64-release` (its `build_all.py --config release` with `C5_RELEASE=Release-5.5`,
x64 DLL sha256 2019c9d3...) · made by impl-0783 on MC-B's word, 2026-11-26 00:55-01:10, `bash server/evidence/0783/pins55.sh`
(652 s) · `[L]` its README.txt. The VP 5.4.8 pins (builds for a 5.4.8 machine, such as the laptop's tb1 on 2026-11-26,
from a commit before 0765's merge): `C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185` and
`C:/c5-64bit-work/keep/pins-0.1b/deps-x64-release` (made by 0616). The commands below name the 5.5 pins. **On main the build tools REFUSE the 0.1b source folder**
(`build_proxy.py` and `build_server.py` check it against `deps/dll.lock`, a2627c8ac; MC-B #17882, 2026-11-26): a 5.4.8 build
needs a git tree at 1b87dd185 with `--unlocked`, a `--shared` clone of the local fork clone on C: (COMMON 15). Never
write into either pin folder. · `[I]` their README.txt,
[release-builds.md](release-builds.md) 6.5.

Your own copy (Git Bash) from a fork clone that has the pin (pins: `deps/dll.lock` `commit` for x64, `deps/i0.lock`
`commit` for 32-bit):
```
git -C <fork clone> archive -o C:/c5-64bit-work/<t>/src.tar <commit from deps/dll.lock>
mkdir -p C:/c5-64bit-work/<t>/src && tar --force-local -x -f C:/c5-64bit-work/<t>/src.tar -C C:/c5-64bit-work/<t>/src
```
or `git -C <fork clone> worktree add --detach <dir> <commit>` · seconds · every tool checks the source with
`resolve_src` (a git checkout at the pin without tracked changes, or a plain copy whose fingerprint equals the lock's;
`--unlocked` accepts only a git tree at another commit) · `[I]` `proxy/evidence/0535/build_0535.sh`, 0616 ·
[tools/x64-dll/README.md](../../tools/x64-dll/README.md) "What --src must be".

Fingerprint after a re-pin: `python tools/x64-dll/src_fingerprint.py --git <fork clone> [--commit REV]` or
`--dir <tree>` (prints the `fingerprint = ...` line) · `[L]` help 2026-11-21; the re-pin also runs
`python proxy/gen/regen_model.py --src <pin source> --lock deps/dll.lock --write` (`deps/dll.lock` header) `[C]`.

A fork clone of your own for a release take (D111; never the owner's own clone):
```
git clone --shared --no-checkout "$USERPROFILE/Documents/GitHub/Community-Patch-DLL" C:/c5-64bit-work/<t>/fork
FORK_URL=$(python -I tools/release/settings.py | awk -F'\t' '$1=="fork_url"{print $2}')   # the fork_url setting
git -C C:/c5-64bit-work/<t>/fork remote set-url origin "$FORK_URL"
git -C C:/c5-64bit-work/<t>/fork fetch origin x64-0.1b i0-0.1b ia-luaexec-no-loadstring
git -C C:/c5-64bit-work/<t>/fork fetch origin tag Release-5.4.8
```
· about a minute · `[I]` 0626 (2026-11-20, `tools/release/evidence/0626/`) · [release-driver.md](release-driver.md).

The MC's pins folder (setting `pins_dir`, `%USERPROFILE%/c5-i0`): `dlls/x64-<commit8>-p<tools8>/` and
`i0ship-<commit8>[-ia]/`. Read only for everyone but the MC. `[C]` `tools/release/settings.py`.

### 1.2 I want server.exe and the proxy against the shared pin build (the normal case)
```
python -I -X utf8 server/build_server.py --src C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac --deps C:/c5-64bit-work/keep/pins-5.5/deps-x64-release --config release --out C:/c5-64bit-work/<t>/server-build
```
Needs exe_tables; `--deps` a finished `build_all.py` folder of the same `--config` · 161-304 s (0616, 2026-11-20) ·
`<out>/bin` (server.exe, civ5save.exe, the x64 DLL and its libraries, PDBs: ready for `server/run_server.py --bin`),
`<out>/proxy/bin` (`--no-proxy` skips it), `<out>/render`; last line `build_server: OK` or `build_server: FAILED: <why>`
· 0616, 2026-11-20 · `[I]`, options `[L]` help · [release-builds.md](release-builds.md) 6.5-6.6, script docstring.
Segment Heap server (allocator trial): the same command with `--heap-type segment` (an embedded manifest; default
`default` = no manifest). Two arms for an A/B: build both from ONE clean commit (the build id is in `.text`), then check
`python -I -X utf8 perf/evidence/5037/pesections.py <a>/bin/server.exe <b>/bin/server.exe` (`.text` same) and, while it
runs, `python -I -X utf8 perf/evidence/5037/heapkind.py --image-dir <b>/bin` (SEGMENT or NT, read only) · 91-102 s
with `--no-proxy` · 5037, 2026-11-24 · `[L]` · [server-allocator.md](../08-performance/server-allocator.md).

### 1.3 I want server.exe from scratch (the DLL and its libraries too)
```
python -I server/build_server.py --config release --src C:/c5-64bit-work/<t>/src --out C:/c5-64bit-work/<t>/server-build-rel --offline
```
Needs the third-party archives in the cache (`python tools/x64-dll/thirdparty.py --offline` checks them), the LTO link
place · about 19 min (1,123 s, 4967, 2026-11-20) · as 1.2 · `[I]` ·
[new-exe-build-26577625.md](../02-exe-internals/new-exe-build-26577625.md) C.1. Other switches (`[L]` help):
`--civ5save-only` (zlib + `civ5save.exe`, no `--src`), `--test-dialog`, `--no-render-events`, `--jobs N`,
`--unlocked`, `--quietlock-override "WHO: WHY"` (emergencies).

### 1.4 I want the proxy alone (x86 `CvGameCore_Expansion2.dll` + `vp64helper.exe`)
```
python -I -X utf8 proxy/build_proxy.py --src C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac --out C:/c5-64bit-work/<t>/proxy
```
Needs VS 2008, LLVM, exe_tables; `--config` defaults to release; `--no-purity-test` drops step 7 (not for release
checks) · 61-73 s with the purity test cached (`C:/c5-64bit-work/cache/purity-test/`), about 4-6 min otherwise ·
`<out>/bin/`, `build-proxy.log`, `build-proxy.json` · 0616, 2026-11-20 · `[L]` help · [proxy-build-settings.md](proxy-build-settings.md).

### 1.5 I want the shell programs and the composite (plain, or ASan)
Git Bash, through the quiet lock (the `C5_SHELL_BUILD` value is THIS checkout's own folder; unset = refused, exit 2):
```
C5_SHELL_BUILD="C:\\c5-64bit-work\\<t>\\shell-build" python -I perf/quietlock/quietlock.py run --who <t> --what "shell build" -- cmd //c "shell\\build.cmd"
C5_SHELL_BUILD="C:\\c5-64bit-work\\<t>\\shell-build-asan" python -I perf/quietlock/quietlock.py run --who <t> --what "shell build asan" -- cmd //c "shell\\build.cmd asan"
```
cmd: `set C5_SHELL_BUILD=C:\c5-64bit-work\<t>\shell-build` then `shell\build.cmd [asan]`. PowerShell:
`$env:C5_SHELL_BUILD='C:\c5-64bit-work\<t>\shell-build'; cmd /c shell\build.cmd`. Needs VS 2022, SDK 10.0.26100.0,
`shell/third_party/minhook`, exe_tables; builds c5render first · about 4 min (2026-11-20, under load); ASan not timed
· `%C5_SHELL_BUILD%\bin\` (shell_composite.dll, test_host.exe, shell_server.exe, the tap hosts, farm guard test
programs, shell_composite.ini copied once), last line `BUILD OK: <bin>` · 4967, 0627 (2026-11-20); asan 0535
(2026-11-18) · `[I]` (no `--help`: any argument but `asan` exits 2) · [test-health-on-main.md](test-health-on-main.md) 6.
The frame build the shell suite also reads: `cmd //c "shell\\frame\\build.cmd <dir>"` (sets `C5_FRAME_BUILD`; section 2.3).

### 1.6 I want the render library or a render target, and the render tests
```
python render/build.py --config Release --target c5render --build-dir C:/c5-64bit-work/<t>/render-build
python render/build.py --test -R "landwater_region|coaststrips"
python -I perf/quietlock/quietlock.py run --who <t> --what "render asan" -- python render/build.py --asan --test -R coaststrips --build-dir C:/c5-64bit-work/<t>/build-asan
```
Needs VS 2022, LLVM, CMake + Ninja, exe_tables; tests need settings `render_game_dir` / `render_exe` (env
`C5_RENDER_GAME_DIR`, `C5_RENDER_EXE`) or `--game-dir`/`--exe`; any skipped test fails the run (COMMON rule 9) ·
default build tree `C:/c5-64bit-work/render-build/<key>/<config>` per checkout · 4911, 4914 (2026-11-20) · `[I]`,
options `[L]` help (bare: the docstring is the documentation) · `render/build.py` docstring.

The full ASan render build without tests: `python -I perf/quietlock/quietlock.py run --who <t> --what "render asan" -- python render/build.py --asan --build-dir C:/c5-64bit-work/<t>/build-asan` (cold about 3-4 min); this configuration builds with /WX, so a constant-expression check in a test fails with C4127 (the one-off of 2026-11-22 after 0665) `[L]`.

### 1.7 I want the x64 DLL and its libraries in one command (build_all)
```
python -I -X utf8 tools/x64-dll/build_all.py --src <src> --out C:/c5-64bit-work/<t>/deps --config release
```
Needs `<src>` at the pin, third-party archives (`--offline` refuses downloads), the smoke database (`--db`, default
the release descriptor's `test_db`), the LTO link place · about 13-16 min release (800-946 s, 4252; 15 min 0616);
debug 65-115 s · `<out>/dll/` (DLL, `build-summary.json`, `build.log`), `stubs/`, `host/`, `logs/<step>.log`,
`build_all.json`; last line `build_all: OK` with the DLL's SHA-256 · 0616, 2026-11-20 · `[L]` help ·
[tools/x64-dll/README.md](../../tools/x64-dll/README.md). `--config debug|release|release-pgo` here (no `fast`, no `pgo-instr`).
`--config release-pgo` (0513, 2026-11-24): the dll step with deps/dll.lock's locked PGO profile (its `profile-file`, or
`--profile FILE`; a file with another SHA-256 is refused before any step unless `--profile-unlocked`); stubs, libraries
and smoke host as release; then `server/build_server.py --config release --deps <that out>` lays it out (its
`bin/build.json` says `deps_config: release-pgo`) · `[C]` tests · [x64-dll-build-configurations.md](x64-dll-build-configurations.md) 4b.

### 1.8 I want only the DLL (`build.py`): fast for functional work, release, PGO
```
python tools/x64-dll/build.py --src <src> --config fast --stubs <a release build's deps/stubs> --out C:/c5-64bit-work/<t>/dll-fast
python tools/x64-dll/build.py --src <src> --config release --stubs <stubs> --out C:/c5-64bit-work/<t>/dll-rel
```
`--config debug|release|fast|pgo-instr|release-pgo` (`[L]` help). Fast = release without LTO: compile about 4 min, link 1.9 s;
marked "not for timing", a layout holding it is named `<layout>-fast`; turnbench refuses it. Release link 470 s,
13.3 GB peak (2026-11-14), takes the link place. Exit 0 / 1 / 3 narrowing gate / 4 gave up waiting for the place ·
fast 0159 (2026-11-14), pgo-instr 4801 (2026-11-17) · `[I]` · [x64-dll-build-configurations.md](x64-dll-build-configurations.md) 1-4a.
The steps by hand (stubs, database, localization, smoke host) are in [tools/x64-dll/README.md](../../tools/x64-dll/README.md)
"The steps by hand". Link repeatability: `python -I tools/x64-dll/repeatcheck.py <out>/link.rsp` (no `--help`; build.py
prints the same line) · [x64-lto-link-repeatability.md](x64-lto-link-repeatability.md). ThinLTO: none (D121).

PGO (D184; the configuration `release-pgo`, ticket 0513, 2026-11-24; the release take uses it when deps/dll.lock
has `release-config = release-pgo`, set since 0774, 2026-11-26): `python tools/x64-dll/build.py --src <src> --config
release-pgo --stubs <stubs> --out F:/c5-64bit-work/<t>/dll-pgo [--shorten]` (the locked profile from deps/dll.lock
`profile-file = <pgo_dir>/<name>`: the setting `pgo_dir` (deps/local-settings.ini, env `C5_PGO_DIR`) is the folder
`C:/c5-64bit-work/keep/pgo`; unset, the build refuses naming it; `--profile FILE --profile-unlocked` for a trial).
`build.py --config release --profile` is refused since 0513. **Retraining** (the retrain rule: build-summary.json
`pgo_retrain.needed`, at every new pin; about 30 min, two LTO links of 12-16 GB; outputs on F: while C: is low): (1)
instrumented build `python -I perf/quietlock/quietlock.py run --who <t> --what "pgo-instr" -- <python.exe> -I -X utf8
<repo>/tools/x64-dll/build.py --src <pin src> --config pgo-instr --stubs <a release build's stubs> --out F:/c5-64bit-work/<t>/instr --shorten --jobs 8`
(15 min, link 519 s on 5.5); (2) a template bin with server.exe of the pin: the lock's `release` layout's bin, or `python -I -X utf8 server/build_server.py --src <pin src> --deps <a release build_all of the pin> --config release --offline --no-proxy --out F:/c5-64bit-work/<t>/srv` (4 min);
(3) `python -I -X utf8 tools/x64-dll/pgo/train.py --dll <t>/instr/CvGameCore_Expansion2.dll --template-bin <bin> --work F:/c5-64bit-work/<t>/train --raw F:/c5-64bit-work/<t>/raw --profile F:/c5-64bit-work/<t>/profile --saves <folder of the sets' saves>`
(under `quietlock.py run`; about 8 min; the sets and `--release` default from deps/dll.lock `base`: Release-5.5 = G,L,S
(0771's `VP55-HUGE_0350-B`, `HI-0771-LC21B55-T200`; no `--db`), Release-5.4.8 = H,M,S with `--saves F:/c5-64bit-work/0438/saves
--db F:/c5-64bit-work/0438/clientdb/stats-on/Civ5DebugDatabase.db`; writes `<work>/dll-lock-profile.txt` with `profile-file = <pgo_dir>/<name>`);
(4) `python -I tools/x64-dll/pgo/check_profile.py <profile> --work <dir>`;
(5) copy the profile to `C:/c5-64bit-work/keep/pgo/` (a README line there), replace the `profile`/`profile-file` lines of deps/dll.lock with the
txt's; (6) a release-pgo build and the identity check `python -I tools/x64-dll/pgo/identity.py --arm pin=<pin release DLL> --arm pgo=<release-pgo DLL> [--arm instr=<instr DLL> --reuse instr:L=<train>/run-L ...] --template-bin <bin> --work <dir> --saves <dir>` (sets by the release as in (3); IDENTICAL required) ·
scripts of the 5.5 retrain `tools/x64-dll/evidence/0774/scripts/` · 4801 (2026-11-17), 0513 (2026-11-24), 0774 (2026-11-26) · `[L]` · [speedup-p1-pgo.md](../08-performance/speedup-p1-pgo.md) 11, 12.
Define-off check at a pin built with marker defines (4808, 2026-11-23, `[L]`, ~13 min, one LTO link, output on F:):
`bash perf/evidence/0504/4808/defoff.sh` (build_as_pin with the pin's describe, CL set to the marker defines, SOURCE_DATE_EPOCH read
from the pin DLL's __DATE__/__TIME__, then dllcmp --allow-build-id). A fast define-on build with the 0.1b markers (~5 min):
`bash perf/evidence/0504/4808/kb.sh <name> [--config release] DEFINES`. Trap: speedkit's defineoff/kitbuild scripts do not fit pins
built with marker defines.
A fast define-on x64 DLL for a live check of a feature that needs the render messages (4986, 2026-11-23, `[L]`, about 6 min, no
LTO link; a local scratch branch only, never pushed): `git clone --shared <long-lived fork clone> <dir>`; fetch x64-0.1b and
x64-render-events from origin; `checkout -B <scratch>` at x64-0.1b; `cherry-pick -x addd57b8bf3f`; then `python -I -X utf8
perf/speedkit/kitbuild.py --fork <dir> --branch <scratch> --stubs C:/c5-64bit-work/keep/pins-5.5/deps-x64-release/stubs --out <out>
--config fast --define VP64_HIDDEN_EMBARK --define VP64_SPEED_DANGER --define VP64_SPEED_DANGER_DG7`; swap
`<out>/dll/CvGameCore_Expansion2.dll` into the layout's bin and restore by hash. Pictures from it are DIAGNOSTIC (D112).
A counting DLL at the 0.1b pin (counters behind your own `VP64_COUNT_*` define on a local scratch branch, the marker defines on)
and its census runs, headless (5040, 2026-11-24, `[L]`): scratch clone and branch `bash perf/evidence/5040/setup_fork.sh` (a
`git clone --shared` of the local fork clone, branch `count-5040` at 1b87dd185; seconds), edit, `bash perf/evidence/5040/fork_commit.sh
"<message>"`; build
`bash perf/evidence/5040/kb.sh <name> [--config release]` (fast 249 s; release 715 s with the LTO link place), output on F:; a
pin layout of your own `python -I -X utf8 server/build_server.py --src C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac --deps
C:/c5-64bit-work/keep/pins-5.5/deps-x64-release --config release --no-proxy --out F:/c5-64bit-work/<t>/server-build` (215 s);
the identity job of M and H with the counters' CSVs from the candidate's run folders `bash perf/evidence/5040/id.sh <build> <job>`
(about 6 min; the headless server's working folder is its run folder, so a DLL's output files land there); longer runs without a
reference `bash perf/evidence/5040/long.sh <build> <job> [M_TO] [H_TO]`. Research: [speedup-danger.md](../08-performance/speedup-danger.md) 9.

### 1.9 I want the 32-bit (i0) release DLL of the pin by hand
From a worktree of the fork at `deps/i0.lock` `commit`, PowerShell (the PATH prefix is needed:
`NoDefaultCurrentDirectoryInExePath=1` hides `update_commit_id.bat`):
```
git -C <fork clone> worktree add --detach <dir> <commit from deps/i0.lock>
$env:PATH = "C:\Program Files\LLVM\bin;<dir>;$env:PATH"; python build_vp_clang.py --config release
```
Git Bash, from `<dir>` (the take's form, NOT VERIFIED by hand): `PATH="/c/Program Files/LLVM/bin:$PWD:$PATH" python build_vp_clang.py --config release`.
A `git archive` copy has no git: stub `update_commit_id.bat`. Start it through `quietlock.py run` · about 5 min
(271 s, 2026-11-14) · `<dir>/clang-output/Release/CvGameCore_Expansion2.dll` · normally built by the take (0626,
2026-11-20, plain and instance-aware `-ia`; the IA patch per base is `IA_PATCHES` in `tools/release/stages/build.py`) ·
`[I]` · [build-and-install-dll.md](build-and-install-dll.md) 1, 5.

### 1.10 I want a 43-civ variant
| Part | Command | Notes |
|---|---|---|
| x64 DLL | `python -I -X utf8 tools/x64-dll/build_all.py --src <src> --out <out> --config release --major-civs 43` (also `build.py ... --major-civs 43`) | define `MOD_GLOBAL_MAX_MAJOR_CIVS=43` on the DLL step only; version `+m43`; the smoke must print "Supporting 43 major civilizations" · 0616, 4252 `[I]`, option `[L]` help |
| server + proxy | `server/build_server.py --deps <the 43 build_all out> ...` (1.2) | no define: server and proxy are the same for 22 and 43 (MC decision G1) |
| shell / composite | unchanged | |
| 32-bit plain and `-ia` | only through the take (`take_release.py ... --major-civs 43`): the define is injected into the copy's `build_vp_clang.py` predefs; layouts `i0ship-<c8>-m43[-ia]` | no fork commit |
| release take | `python -I tools/release/take_release.py Release-5.4.8 --carry pins --major-civs 43 --descriptor <d> --fork <clone> --work <run> --scratch <s> --who <t> --gate-wait 60 --tier candidate --only <stage>` for stages survey, branches, testdata, lock, build, model, purity, pack | 37-38 min, build 1,791 s · `tools/release/evidence/0626/s11_take43.sh`, 0626 2026-11-21 `[I]`; a work folder of one count refuses another (exit 2) |
| installer | `build_installer.py --work <std take> --work43 <m43 take> ...` (1.13) | one installer holds both |

Validated values 22 and 43; 2-62 with `--unvalidated-major-civs`; 63+ refused. 43-civ journeys need an instance
carrying the 43-civ VP (0643). · [player-limits.md](../03-dll-engine-boundary/player-limits.md),
[release-builds.md](release-builds.md) 7.

### 1.11 I want a release take (`tools/release/take_release.py`)
Candidate tier (build to fixchecks), started in the background, never under a shell timeout:
```
python -I tools/release/take_release.py <tag> --fork C:/c5-64bit-work/<t>/fork --gate-wait 60 --work C:/c5-64bit-work/<t>/run [--skip-ia]
```
The validated take of the pins for a build the owner plays (the first beta's form):
```
python -I tools/release/take_release.py Release-5.4.8 --tier validated --carry pins --for-art --descriptor C:/c5-64bit-work/<t>/<descriptor>.json --fork C:/c5-64bit-work/<t>/fork --work C:/c5-64bit-work/<t>/std/run --scratch C:/c5-64bit-work/<t>/std/scratch --who <t> --gate-wait 60
```
with, in the same shell, `C5_JOB_OWNER`, `C5_POOL_OWNER`, `C5_RELEASE_INSTANCE`, `C5_RELEASE_HI_INSTANCE`,
`C5_RELEASE_HI_TICKET`, `C5_RELEASE_HI_ADDRESS`, `C5_RELEASE_HI_BY`, `C5_RELEASE_TESTBEDS=local`,
`C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt`, and `unset VP64_DEV_READY_BUILD` ·
`C5_RELEASE_TICKET=<ticket> C5_RELEASE_ADDRESS=<live queue address>`: the instance stage claims with them (else with
`C5_RELEASE_HI_TICKET`/`_ADDRESS`); without either it stops at once, nothing claimed (0640, 2026-11-23, `[C]` hermetic; the
earlier work-around `pool.py transfer <I> --by <who>-release --ticket <id> --address <addr>` is no longer needed) ·
Installed-layout journeys (0640, 2026-11-23, `[C]`): `C5_RELEASE_HI_INSTALLED=<I>` plays the product route only
(`C5_RELEASE_HI_ROUTES` unset = product); a stock route there stops before the claim (cannot be judged). A fresh stock route: after
the installer's removal, `C5_RELEASE_HI_ROUTES=stock C5_RELEASE_HI_INSTANCE=<I>` without `C5_RELEASE_HI_INSTALLED`, or the tier path ·
`tools/release/evidence/0626/s10_take.sh` (0626, 2026-11-20/21) `[I]`.
Needs settings `pins_dir`, `clones_dir`, `fork_url`, `core_db`, `exe_tables`; 65 GB free on C:; the LTO link place ·
candidate about 1 h; validated standard take about 4 h 35-40 min; with the 43-civ take, the installer and one
installed instance about 6 h · exit 0 tier reached, 10 STOP (one line naming the stage and how to unblock; fix and run
the same command again, it resumes), 1 failed (`<work>/logs/<stage>-<name>.log`), 2 arguments, 3 gate. A file
`<work>/STOP` ends it before the next step (never while a journey's game runs). Rerun one stage with `--only STAGE`,
from a stage on with `--from STAGE`. D162 answer from the state alone:
`python -I tools/release/take_release.py <tag> --art-check --work <dir>` (exit 0 yes, 10 no, 2 no state).
Tiers and stages: `candidate` = survey, branches, testdata, lock, build, narrowing, census, model, purity, pack,
offline, savetest, drift, fixchecks; `validated` adds refsave, instance, wholegames, hi_journey, report
(`tools/release/stages/validated.py`) · `[C]`, options `[L]` help ·
[release-driver.md](release-driver.md) W7.3, [release-builds.md](release-builds.md) 7, [tools/release/README.md](../../tools/release/README.md),
[new-vp-release-checklist.md](new-vp-release-checklist.md).
Read a descriptor: `python -I tools/release/descriptor.py [--tag TAG] [--field NAME] [--check-files]` (files in
`tools/release/releases/`) `[C]`.
A new release with no descriptor and a two-part tag (Release-5.5, 0764, 2026-11-25, `[L]`): the tag check wants
`Release-x.y.z`, so name the base commit, start from the old descriptor and carry a release copy of the series:
`python -I tools/release/take_release.py Release-5.5 --base-commit b0bce8c2f23c865cf5b0779986654366c88cef62 --base-release Release-5.4.8 --tier candidate --carry series --series tools/x64-dll/evidence/0764/carry-Release-5.5.tsv --skip-ia --fork <clone> --work F:/c5-64bit-work/<t>/take --scratch F:/c5-64bit-work/<t>/scratch --who <t> --gate-wait 60 --only <stage>`
(wrapper `bash tools/x64-dll/evidence/0764/take.sh <work> <scratch> <clone> --only <stage>`) · testdata 1.5 min (writes
`<work>/testdata/Release-5.5.json` and the 5.5 database), build 40 min, drift 8 min · results
[vp55-dll-branches.md](../03-dll-engine-boundary/vp55-dll-branches.md) 5-6.
The patch side of a pin move (0765, 2026-11-25, `[L]`; research [vp55-patch-side.md](../03-dll-engine-boundary/vp55-patch-side.md),
scripts `research/03-dll-engine-boundary/evidence/0765/`):
- A small source copy of a pin (40 MB, the build folders only; the fingerprint covers only those, so every builder
  takes it): `bash research/03-dll-engine-boundary/evidence/0765/srccopy.sh <commit> <dest>` (seconds; it reads
  `C:/c5-64bit-work/0765/fork`: edit `G` for your clone), then `python tools/x64-dll/src_fingerprint.py --dir <dest>`.
- The smoke of a new release's DLL needs its database: `C5_RELEASE_TEST_DB=<the release's test db>` (or
  `C5_RELEASE=<its descriptor>`) before `server/build_server.py --config release --src <copy> --out <out> --offline`
  (about 18 min on C:, 2026-11-25). The release's own tools (`test_proxy_rt.py`, `run_server.py`, the take) read the
  descriptor through `C5_RELEASE=<file>`.
- The localization database of a release from its texts (the testdata stage keeps the base release's):
  `python -I research/03-dll-engine-boundary/evidence/0765/locbuild.py --plan <testdata>/db/Civ5DebugDatabase.db.build/plan.txt
  --game-dir <testdata>/game/<game folder> --exe <civ5-headless>/out/Release/<build>/civ5headless.exe --base <a base
  game Localization-Merged.db without mods> --work <dir>` (1-6 min; output `<dir>/Localization-Merged.db` and
  `locbuild.json`); 5.5's is kept in `C:/c5-64bit-work/keep/release-5.5-db/`.
- The purity lists at a new pin: `python -I -X utf8 proxy/tools/derive_purity.py --src <copy> --dll <x64 DLL> --out <dir>
  --commit <c>` (seconds; `report.tsv` names each dropped row and why); after the hand re-audit, `[cited at <c>]` on
  the rows whose cited text changed, `AUDITED_AT` in derive_purity.py = the pin, then
  `python -I -X utf8 proxy/tools/derive_purity.py --make-baseline --fork <clone>` (0 unresolved wanted) and
  `python -I -X utf8 proxy/tools/gen_pure_list.py --check`. A citation of a file whose name starts with `!` is not read
  (write it in prose). Without the re-baseline `build_proxy.py` stops at its step 7.
- The lock names the pins folder as `<pins_dir>` (the `pins_dir` setting): `run_d10.py`, `run_pair.py` and
  `perf/speedkit/defineoff.py` resolve it (and `%VAR%`).
- The 43-civ take of a release with a two-part tag on the moved locks: `python -I -X utf8 tools/release/take_release.py
  Release-5.5 --base-commit b0bce8c2f23c865cf5b0779986654366c88cef62 --descriptor <the release's descriptor> --tier
  candidate --carry pins --skip-ia --major-civs 43 --fork <clone> --work F:/c5-64bit-work/<t>/take43 --scratch
  F:/c5-64bit-work/<t>/scratch43 --who <t> --gate-wait 60 --only <stage>` for survey, branches, testdata, lock, build
  (`research/03-dll-engine-boundary/evidence/0765/p4_m43.sh take`; 1 h 47 min on a loaded F: drive, the two whole-tree
  source archives alone about 25 min), then one headless game on its x64 layout:
  `C5_RELEASE=<descriptor> python -I -X utf8 server/run_server.py server/tests/specs/continents-large-30majors-fixed-seed.json
  --turns 10 --run <dir> --bin <layout>/bin --fail-on-unserved` (148 s).
- The verify leg's `--proxy` is the proxy DLL FILE (`<build>/proxy/bin/CvGameCore_Expansion2.dll`), not its folder
  (PermissionError otherwise); with `VP_PROXY_PURITY="<purity.tsv>|<x64 DLL>"` it exercises the UI-Lua mirror.
- The harness checklist runs as a module from the checkout without `-I`: `python -X utf8 -m tools.harness.checklist
  --instance <inst> --turns 10 --out <json> --evidence-dir <dir>` (with `-I`: "No module named 'tools'").

### 1.12 I want the validation tiers (epic 0630, D220/D221; `tools/release/tiers/`)
A hotfix take (budget 60 min; only `hotfix` exists so far) on the last accepted take's results, started in the
background, never under a shell timeout, with the same environment as 1.11:
```
python -I tools/release/tiers/take_tier.py <tag> --tier hotfix --base-take <accepted take's work> --results C:/c5-64bit-work/<t>/results --descriptor <d> --fork <clone> --carry <the base's mode> --for-art --jobs 4 --work C:/c5-64bit-work/<t>/hotfix --scratch C:/c5-64bit-work/<t>/hotfix-scratch --who <t>
```
Options besides take_release.py's (all of them but `--tier` and `--art-check`): `--base-unaccepted` (a base that
reached no tier: dry runs and D232 only, said loudly and recorded), `--require-adoption` (D232: any build in the
tier is a STOP), `--results DIR` (the results store). Exit codes, `STOP` file, `--only` and `--from` as 1.11.
The report: `<work>/tier-report.md`, its first line the gate (`GATE PASS ...` or `GATE FAIL ... at <stage> ...;
first failing checkpoint: ...`); for a take that stopped before its last stage:
`python -I tools/release/tiers/stages/tierreport.py <work>`. D162 answer of a tier take:
`python -I tools/release/take_release.py <tag> --art-check --work <tier work>` (reads the tier's hi_journey).
Needs a base take whose build and pack are intact; for the journeys one clean Goldberg instance of build 26577625 in
the mods form (`C5_RELEASE_HI_INSTANCE`) and `C5_HARNESS_BIN` in a fresh checkout · 50-60 min when only proxy,
shell, tools or Lua changed on an accepted base, 90-105 min on a first take of new code (busy workstation) · exit 0
tier reached · `[C]`, options `[L]` help 2026-11-21 · [validation-tiers.md](validation-tiers.md) 15-17,
[docs/RELEASE-TIERS.md](../../docs/RELEASE-TIERS.md).
The first beta's gate (D232, one time; 0630, 2026-11-21) `[C]`, from ONE clean checkout of main:
```
for s in survey branches testdata lock build narrowing census model purity pack; do python -I tools/release/take_release.py <tag> --tier validated --carry pins --for-art --descriptor <d> --fork <clone> --work <cand>/run --scratch <cand>/scratch --who <t> --only $s || break; done
python -I tools/release/tiers/store.py import <cand>/run --results C:/c5-64bit-work/<t>/results
python -I tools/release/tiers/take_tier.py <tag> --tier hotfix --base-take <cand>/run --base-unaccepted --require-adoption --results C:/c5-64bit-work/<t>/results --descriptor <d> --fork <clone> --carry pins --for-art --jobs 4 --work <cand>/tier --scratch <cand>/tier-scratch --who <t>
```
In the same shell set `C5_TIER_EXE_BUILD=26577625 C5_TIER_BASE_EXE_BUILD=26577625` and `C5_HARNESS_BIN` (the main
checkout's `tools/harness/native/bin` (created by the command)): a base taken only to pack ran no live check, so its EXE build is the
operator's statement; without it the tier STOPs at `tierraise` in its first minute ("exe_build cannot be read on
the base take", exit 10; the MC, 2026-11-22 05:52) `[L]`. Two more traps of the same night `[L]`: the tier's
build folder was adopted without `gen/` and `gen-exebuild/` (`offline` failed rt 107/111): fixed by 0664 on a1e280631,
whole adoption now brings both and a base without them stops at `build` `[C]` (tests; its first live use is the next
tier take); while a tier runs, keep the base take's own source tree intact (census, model, purity and hutest read
`x64_src` from it); and `C5_RELEASE_HI_INSTANCE` only NAMES the instance: the
`hi_journey` stage claims it itself (owner `C5_POOL_OWNER`), so do not hold that instance's claim yourself, or the
stage retries "claimed by MC" every 120 s until its wait runs out (70 min lost, the MC, 2026-11-22 06:25-07:36;
release your own claim with `pool.py release <name> --owner <you>` and the next retry takes it).
Two more for a hotfix over an earlier take (the second beta candidate, 2026-11-22 12:22, `[L]`): do NOT export
`C5_RELEASE_INSTANCE` in the tier's shell: `test_tier_hi_journey.py` (in the `suites` stage) does not clear it and 29
tests fail while that instance runs a game ("EXE build unknown"); the tier needs only `C5_RELEASE_HI_INSTANCE`. And the
`suites` stage's export gate is `tools/export/check.py --rev <candidate> --since <base take's commit>`, counted per
file: one new `C:/Users/<name>` path or user name anywhere since the base (MC-STATE included) fails it; run that
command on the candidate commit before starting the tier.
then the usual plan resumes in `<cand>/run` (the first command without `--only`). The early installer of the
standard variant: `python -I tools/installer/build_installer.py --work <cand>/tier --out <deliverables>/early-std
--release-name <name>-std-only` (its D162 check passes only after the gate passed); before the hand-over the two
packs must hold the same files: the two `manifest.json` never hash equal, because each names its own work folder
in the x86 DLL's `origin` field (the MC, 2026-11-22 08:17 `[L]`); compare the files instead and the manifests without
that field: `diff <(cd <cand>/run/pack/vp64pack && sha256sum * | grep -v manifest) <(cd <cand>/tier/pack/vp64pack &&
sha256sum * | grep -v manifest)` and `diff <(grep -v origin <cand>/run/pack/vp64pack/manifest.json) <(grep -v origin
<cand>/tier/pack/vp64pack/manifest.json)`, both empty. The first beta's gate (run 3, 2026-11-22 05:53:58-08:16:48)
ran `--from offline` 2 h 23 min, of which 70 min was the held claim above: about 73 min of stages from `offline` on
(offline 2, suites 1, savetest 9, wholegames 19, hi_journey 40 with both routes; the stages before `offline` ran in
the earlier runs).
The results store: `python -I tools/release/tiers/store.py (import WORK | list) [--results DIR] [--dry-run]`
(`--dry-run` on import) `[L]` help. The suites that need no game, by area or diff:
`python -I tools/suites/run_suites.py (--all | --areas A,B | --diff BASE[..HEAD] | --suites S,T) [--tier hotfix] [--budget S] [--out DIR] [--list]`
(map `tools/suites/areas.json`, skips allowed only in `tools/suites/skip_allowlist.json`) `[L]` help 2026-11-21.
Trap (MC-B from 0630 step 0, 2026-11-22, `[C]`): main's `take_tier.py` refuses a TIER take as the base of a hotfix
("its report stage is missing": `base_unaccepted()` looks only for the validated plan's `report` stage), so a third
candidate as a hotfix over `cand2/tier3` is refused; the second candidate's `--base-unaccepted` hid it. Fixed on the
0630 team branch only (28bef5de0: a take with `config.tier` is accepted by its `tierreport` + reached tier + the D162
yes); until 0630 M2 merges, cherry-pick 28bef5de0 into the take's worktree or base the hotfix on `cand1/run` again.

TRAP (0673, 2026-11-22): a hotfix tier over `C:/c5-64bit-work/cand1/run` taken on current main (177 commits later)
runs past the `suites` stage's 900 s budget (exebuild-tables alone 526 s; STOP exit 10, release-hotfix and safety-static
cut off, export-gate never started): run those suites by hand or resume with `--from savetest` after `offline`, and say
which did not run. The beta 4 take's form of it (0725, 2026-11-24, `[L]`; scripts `tools/release/evidence/0725/`): by hand
`python -I perf/quietlock/quietlock.py run --who <t> --what "<take>: suites by hand" -- python -I tools/suites/run_suites.py
--diff <base take's commit>..<candidate> --tier intermediate --lanes 3 --out <cand>/suites-by-hand` (26 min), then the tier
again with `--from savetest`; `exebuild-tables` is now NOT RUN without `--game-files`: run it by itself, `run_suites.py
--suites exebuild-tables --game-files --tier intermediate --out <dir>`. A hotfix whose diff over an already-taken cut
names only `server/` (0751, 2026-11-25, `[L]`, 19 min): only the hotfix selection, `run_suites.py --suites
exebuild-tables,release-hotfix,safety-static,proxy-gen,shell-tables --lanes 3 --rerun-failed 1 --tier intermediate
--game-files --out <dir>` through quietlock, with `C5_LUA51_DLL` = the take's `build/x64-*/deps/stubs/lua51_x64.dll`,
and `tools/export/check.py --rev <candidate> --since <base take's commit>` for the export gate (never `--range` beside
`--suites`: it then selects by diff); scripts `tools/release/evidence/0751/`. The `installer` suite's test installs run on a
hidden desktop since ticket 0728 (2026-11-25) and no longer take the foreground; before that fix (branches without
`tools/installer/tests/hidden_run.py`) leave it out on the workstation (`--suites` without it). A test installer for a developer from such a take (stages to `pack`, then
`build_installer.py --work <tier work> --out <new folder> --test-build --release-name <name with UNTESTED>`) and its
silent install on a developer's own game (`/P64OPT=` to keep the options, `/P64IGNORERUNNING=1` while test instances
run, test builds only): 0673's log of 2026-11-22 20:44 · `[L]`.
The intermediate tier (0630 M2, 2026-11-23, `[L]`; validation-tiers-m2.md 8.4i): `take_tier.py ... --tier intermediate` runs the
suites ALONE right after the build, as a barrier with a 60 min budget (20 min with B's and A's heavy starts held; 60+ min beside 9
heavy jobs): ask MC-B for a held barrier hour. release-hotfix (320 tests) and release-intermediate (165) replace release-tiers in
tier I; release-tiers itself is run at every merge that changes tools/release: `python -I tools/suites/run_suites.py --suites
release-hotfix,release-intermediate --lanes 3 --tier intermediate --out <dir>` (about 18 min on a busy workstation; the single
release-tiers suite took over 50 min there). Before a take, run the suites that hold any test added since the last take through the
take's runner: the same command with `--repo <runco>`. `run_suites.py --rerun-failed 1` (intermediate only) reruns a failed suite
once; flakes are listed in results.json `flakes`. A suite's interpreter is areas.json `python` (perf-ledger needs
`%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe`); the scenario suite needs `C5_LUA51_DLL=<a lua51_x64.dll>` (the tier's
suites stage sets it from the take's build).
After a STOP the side-by-side driver starts no new stage: stages that were waiting run only on a resume. To resume, run the same
take_tier command with the same `--work` from the same checkout: ok stages are skipped on unchanged inputs within seconds, and the
stopped stage runs again; a resumed stage counts once (MC #11436). Before a resume, copy the stopped stage's evidence out of
`<work>/<stage>/runs` (the resume writes over it). With `C5_RELEASE_INSTANCE` unset the instance stage takes the pool's choice and
can take the oldexe instance: name a second 4390913 instance there if one is granted. Measured: an intermediate take on the
workstation, every stage fresh, 3 h 8 min wall clock; the longest game stages sweep 83 min, scenes 74, wholegames 51, hi_journey
49, savetest 37. Traps: `pool.py list` took 3 min 7 s on the busy workstation (give it 300 s, or read
`%USERPROFILE%/c5-instances/_pool/claims/*.json`); appends from several processes to one file lose lines on Windows (test fakes
write one file per process).
The intermediate tier over a release's own take, with the suites by hand (0734, 2026-11-25, `[L]`; scripts
`tools/release/evidence/0734/`, validation-tiers.md 23): the tier's suites stage runs `run_suites.py --all`, which holds the
`installer` suite (a foreground "Setup" window on the workstation, 0728), so run the light stages with `--only` (survey to pack),
then in the barrier the suites by hand through `quietlock.py run`: `run_suites.py --suites <every needs-none suite of the tier but
installer and release-tiers, plus exebuild-tables> --lanes 3 --rerun-failed 1 --tier intermediate --game-files --budget 3600
--repo <take checkout> --out <dir>` with `C5_LUA51_DLL=<work>/build/x64-*/deps/stubs/lua51_x64.dll` (34 min with the bootstrap
below beside it), then `take_tier.py ... --from offline` (the tier's `suites` stays pending, so its report reads GATE FAIL at suites).
TRAP `[C]` run_suites.py at dc259f0b1: `--suites S,T --range A..B` selects by the DIFF A..B, not by `--suites` (`select()` tests
`a.diff`, which `--range` sets, before `a.suites`): only the always-suites ran (54 s). Give `--range` only with `--all`; run
export-gate apart (`--suites export-gate --range A..B` runs it, as the always-suite). A base take without a scenes stage (a hotfix
tier take) needs its scene set first: `python -I tools/release/tiers/stages/scenes.py bootstrap --base-take <base> --results
<store> --work <dir> --who <t>` (26 min; it may run beside the suites). Set `C5_DOCUMENTS=<own folder>` for offline's hutest
(MC-B 2026-11-25: hutest_host wrote D119 reports into the owner's real My Games before 0737). After any failed stage the
side-by-side driver starts nothing new (as after a STOP): resume the stages left with `--only <stage>` each, never `--from`
(it reruns every later stage).

### 1.13 I want the installer (test build, or the real one)
Test build (no D162 check; for test instances only):
```
python -I perf/quietlock/quietlock.py run --who <t> --what "installer test build" -- python -I tools/installer/build_installer.py --work <std take work> --work43 <m43 take work> --out C:/c5-64bit-work/<t>/installer-test --release-name <name> --test-build
```
The real installer: the same without `--test-build`, plus `--source-commit <the public x64 branch's commit>` (D261,
ticket 0767; `--source-repo` defaults to the setting `fork_url`); each work folder must pass `take_release.py <tag> --art-check`
(D162), else exit 4; the 43-civ waiver only on the MC's word (`--art-check-waiver m43 "D199: <reason>"`); never to
the owner or GitHub without the 0082 confirmation (D166). Dry check without compiling: `--stage-only`.
Needs both takes' build and pack stages; ISCC 6.2.2 at `C:/Program Files (x86)/Inno Setup 6/ISCC.exe` (SHA-256
pinned); lua/zlib tarballs (`--deps`); a Community-Patch-DLL clone for VP's Lua fixes (`--vp-clone`) · 26 s ·
`--out` (new or empty): `VP64-Patch-<release>-for-VP<x.y.z>[-TEST].exe`, `SHA256SUMS.txt`, `build-record.json`,
`payload/`, `iscc.log` · 0626, 2026-11-21 (test build) · `[I]`, options `[L]` help ·
[installer-notes.md](installer-notes.md) 12-13, [tools/installer/README.md](../../tools/installer/README.md).
Installing it on a test instance: section 3.4.
A new Vox Populi release (ticket 0767, 5.5): add its DLL rows to the version table with
`python -I tools/installer/vp_releases.py --fork <Community-Patch-DLL clone>` (reads every tag's DLLs, about 5 GB of
`git show`; `--check` compares), then the version check and the update path from the last beta, hermetic, with VP's
real DLL and Lua bytes from the clone:
`python -I perf/quietlock/quietlock.py run --who <t> --what "installer tests" -- python -I -X utf8 tools/installer/tests/test_vp55.py -v`
(`C5_VP_CLONE` the clone; `C5_BETA4_INSTALLER=<released exe beside its payload/ folder>` runs a scratch copy of it for the first install,
`C5_REAL_HELPER=<vp64helper.exe>` ships the real helper in the fixture installers, `C5_KEEP_SCRATCH=1` keeps the
scratch) · version check 40 s, update path 4 min · 0767, 2026-11-25 `[L]` · [installer-notes.md](installer-notes.md) 17.
Since one-off 9081 the build runs each variant's staged `VP64/server.exe` with the options verbs to make the options
page; a take whose server.exe predates 9081 is refused (exit 4). What a server.exe gives, by hand:
`<build_server out>/bin/server.exe --write-options <dir>/VP64Options.default.ini --write-options-schema <dir>/options-schema.tsv`
· 58 ms · exit 0 written, 1 a file not written, 2 usage · one-off 9081, 2026-11-21 `[L]` ·
[installer-notes.md](installer-notes.md) 16.
A TEST installer of a work branch (to see an installer change with a real server.exe; standard variant only, no game
stage): run, from the branch's worktree, in the background, each stage in turn
`python -I tools/release/take_release.py Release-5.4.8 --tier validated --carry pins --descriptor <a copy of a take's descriptor> --fork <clone> --work <w>/run --scratch <w>/scratch --who <t> --gate-wait 5 --only <stage>`
for survey, branches, testdata, lock, build, narrowing, census, model, purity, pack, then
`build_installer.py --work <w>/run --out <new folder> --release-name <name> --test-build` (no `--work43`) · 31 min,
build 21 min with the LTO link · one-off 9081, 2026-11-22 00:44-01:16 `[L]` · [installer-notes.md](installer-notes.md) 16.10.
The options page from a built server.exe (4940, 2026-11-22, `[L]`): `server.exe --write-options X.ini --write-options-schema X.tsv`,
then `tools/installer/options_schema.py`'s `build_page(<both files read as bytes, decoded UTF-8>, {}, 'i3')`. Trap: reading them
with `read_text` turns CRLF into LF and the page refuses them.

### 1.14 I want this checkout's EXE build tables (after a catalogue change, or a new EXE build)
Run when a builder fails with "the EXE address header" (`emit_header` exit 2 naming `exe_tables`), e.g. after a merge
that added rows to `exebuild/symbols/`; not needed when the shared tables fit:
```
python -I tools/exebuild/gen_table.py --identity --old F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe
python -I tools/exebuild/gen_table.py --old F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe --new F:/c5-64bit-work/golden/civ5-steam-build26577625-ws/CivilizationV_DX11.exe --steam-build 26577625
```
1.3 s and 2.9 s · `<exe_tables>/<build>/table.json` (local only: never committed, pushed or quoted, D202); exit 0
complete, 4 incomplete (answer `review.tsv` in `<exe_tables>/<N>/hand-review.tsv`, run again), 1 a BAD pattern,
2 setting · 4960 (2026-11-19/20) · `[I]`, options `[L]` help · [exe-build-table.md](../02-exe-internals/exe-build-table.md) 6, 8.
The emitter by hand (tests only): `python -I tools/exebuild/emit_header.py --out <gen folder> [--tables DIR]`.
A new `code` row that is a look-alike of another function (same bytes, other operands, e.g. a second event manager's
register) is not carried by bytes (`review.tsv`, low confidence): find it in the new EXE by its shape with its operands
fixed (e.g. the channel index and the list offset), append `name<TAB>0xVA<TAB>reference` lines to
`<exe_tables>/<N>/hand-review.tsv`, run the second command again; a `C(name)` token of a caller's pattern confirms it ·
0655 (2026-11-21, `fg_a_register` and its three operands) · `[L]` · [farm-builder-guard.md](../02-exe-internals/farm-builder-guard.md) 13.4.

---------------------------------------------------------------------------------------------------------------------

## 2. Tests

Rules: every pytest run with `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1` (section 0); a suite over a minute through
`quietlock.py run` with `C5_JOB_OWNER` set; a missing build or reader FAILS a case, it does not skip, and a skip fails
the run (COMMON rule 9); one pytest per `perf/` tests folder (module names collide). Which folders no merge check runs,
their inputs and their results on main: [test-health-on-main.md](test-health-on-main.md).

### 2.1 I want the merge checks a MERGE-REQUEST names
```
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/harness/tests/test_safety_static.py
python -I tools/export/check.py --rev HEAD --since main --require-terms --no-env-terms
```
The safety test runs only under pytest: `python -I tools/harness/tests/test_safety_static.py` and `unittest discover`
run zero tests and exit 0 · 42 s, 55 passed; the export gate 45 s, PASS · `[L]` 2026-11-21 · the file's docstring,
[upstream-export.md](upstream-export.md). The other merge checks: all builders of a changed shared source (2.10),
scene-bundle rule A when server.exe draws something new by default (2.5), the test folder of every `tools/` area the
merge touches.

### 2.2 I want the shell stand-in suites (`shell/tests`, `shell/frame/tests`)
Build the inputs (each through `quietlock.py run`), set the variables, then run WITHOUT `-I` (`shell/tests/conftest.py`):
| Variable | Value | Built by |
|---|---|---|
| `C5_SHELL_BUILD` | the shell build folder | `shell/build.cmd` (1.5) |
| `C5_FRAME_BUILD` | the frame build folder | `cmd //c "shell\\frame\\build.cmd C:\\c5-64bit-work\\<t>\\frame-build"` (about 1 min) |
| `C5_WATER_READER` | `<dir>/bin/water_reader_test.exe` | `python -I server/tests/render/water/build_water_tests.py --out <dir>` |
| `C5_SHADOW_READER` | `<dir>/bin/shadow_reader_tool.exe` | `python -I server/tests/render/shadow/build_shadow_tests.py --out <dir>` |
| `C5_ICE_READER` | `<dir>/bin/ice_tap_test.exe` | `python -I server/tests/render/ice/build_ice_tests.py --out <dir>` |
| `C5_FOWMASK_PRODUCER` | `<dir>/bin/fowmask_producer.exe` | `python -I server/tests/render/fow/test_fow_build.py --out <dir>` (about 3.5 min) |
| `C5_OVERLAY_READER` | `<dir>/bin/test_reader_tool.exe` | `python -I server/tests/render/overlay/test_reader_build.py --out <dir>` |
| `C5_RENDER_TEST_BUILD` | `<dir>/bin` | `python -I server/tests/render/build_render_tests.py --out <dir>` (about 5 min) |
```
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests shell/frame/tests
C5_SHELL_BUILD=<shell build> C5_FOWMASK_PRODUCER=<dir>/bin/fowmask_producer.exe PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests/test_fow_models.py
```
All six readers about 12 min; the whole suite about 60 min alone · 0627 (2026-11-20 23:17 to 2026-11-21 00:15) ·
`[I]` · [test-health-on-main.md](test-health-on-main.md) 5-6. Some cases are load-sensitive (they pass alone; listed there).
The exact-lockstep cases alone (ticket 0691; v3 stand-in, about 2.5 min; with the shell build of 1.5 and `C5_FRAME_BUILD`
set as above, through `quietlock.py run`): `python -X utf8 -m pytest -q -s -p no:cacheprovider shell/tests/test_lockstep.py
-k "exact_"` (`-s` prints test (d)'s hung-mark and SendMessage times; `C5_SHELL_TIMING_LOG=<file>` keeps them as JSON
lines). A stand-in server whose heartbeat stops during a stall: `lockstep_server.exe ... --stall-for S
--stall-heartbeat-stop` · 0691 (2026-11-23) · `[L]`.
The fatal end of a hung server (ticket 0697): `-k fatal_end` (`test_exact_hung_server_fatal_end_writes_the_totals`):
`test_host.exe ... --fatal-sink` sets the composite's hung sink and ends as the proxy's `Fatal(3)` does
(`VPShell_FlushTotals("fatal", ...)`, then TerminateProcess 3); the composite's log ends with `pacing totals: why=fatal`.
The held-frame cases (ticket 0691, MC #11169; about 40 s): `-k "held_frames_are_not or unreported_hold or stuck_hold"`; a
stand-in server that holds a frame for M EXE presents with the hold reported: `lockstep_server.exe ... --hold-for-exe N,M
--hold-on <file>` (M < 0 never clears; `--hold-unreported` = a server before the report). Fails-before against an older
composite: build that commit's shell in a scratch worktree (`git worktree add --detach <dir> <commit>`, then 1.5 there with
its own `C5_SHELL_BUILD`), copy the NEW `lockstep_server.exe` into its `bin`, run the new test file with `C5_SHELL_BUILD`
= that folder; remove the worktree after · worker-0673n (2026-11-23) · `[L]`.
ASan: build with `shell\build.cmd asan` into its own folder, then add
`ASAN_OPTIONS="log_path='C:/c5-64bit-work/<t>/asanlogs/asan':log_exe_name=1" C5_ASAN_LOG_DIR='C:\c5-64bit-work\<t>\asanlogs'`
(quote the `log_path`: the drive colon ends it otherwise) · `proxy/evidence/0535/suites_0535.sh` (2026-11-18) `[I]`.
The timing files' series under the quiet lock (`test_lockstep.py` + `test_verification_defects.py`, N runs, each its own
`quietlock.py run --take --minutes 15`, 10 min between runs, desk 0136 read before each take, at most 4 h): builds with
`sh shell/evidence/0729/build_0729.sh` (harness native, shell, frame into `C:/c5-64bit-work/0729/b`, about 3 min on
2026-11-25), then from the worktree root `sh shell/evidence/0729/series_0729.sh <out name> [--stop-on-fail] [--first K
--runs N --desk-since <epoch>]` (out `C:/c5-64bit-work/0729/<out name>`: `series.jsonl`, `attempts.jsonl`, `run<i>.log`,
`fail_run<i>/`; a file `STOP` there ends it before the next run; exit 7 = a HOLD or QUIET WINDOW post on desk 0136, read it
and restart with `--first`). About 10 min per run plus the gap. For another ticket copy the two scripts and change the owner
· 0729 (2026-11-25) · `[L]`. The killed-server rule's unit cases alone (no build, under a second):
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests/test_verification_defects.py -k killed_server_rule`.
Negative control of the cut tap's fault handling (0690, 2026-11-22, `[L]`): through quietlock,
`python -I shell/evidence/0690/old_control.py --shell-build <shell build> --out C:/c5-64bit-work/<t>/old-control` (~30 s; prints
NEGATIVE CONTROL OK).

### 2.3 I want the server render suites (`server/tests/render/run_all.py`)
```
python -I server/tests/render/run_all.py --list
python -I server/tests/render/run_all.py --out C:/c5-64bit-work/<t>/ra --exe <pre-update copy>/CivilizationV_DX11.exe --game <pre-update copy> --who <t> [--only <suite>] [--only <suite>] ... [--no-build] [--asan]
```
`--only` takes ONE suite and is repeated: `--only render-host --only scenebundle-programs --only scenebundle-py`
(`--only a b c` exits 2 at once, "unrecognized arguments"; 4735, W47, 2026-11-21 `[L]`). 15 suites (`--list`): render-host, cut-tap, decal-tap, ice-reader, overlay-reader, shadow-reader, fow-scene,
overlay-scene, shadow-ice-scene, water-scene, forest-scene, scenebundle-programs, scenebundle-py, offline-equals-live,
unchanged-vs-main (`--main <dir>`). It gates every step itself. `--exe`/`--game` (or `C5R_EXE`, `C5R_GAME_DIR`) point
at a copy of EXE build 4390913, never the Steam install (D200; another build stops the run, exit 2) · 7 suites about
40 min (4915, 2026-11-21) · last line `run_all: OK: <n> suite(s) passed`, logs `<out>/logs/`, exit 1 on a failure
or a skip, 9 on a `<out>/STOP` file · `[L]` `--list` 2026-11-21 · Without `--only` it also runs
`offline-equals-live`, which fails on a claimed pool instance and reads the F: library outside a rule A slot: pass
`--only` for the suites you want, or set `C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt` (0659,
2026-11-22 `[L]`) · `run_all.py` docstring,
[offline-scene-harness.md](offline-scene-harness.md).
**scenebundle-programs on a loaded machine (ticket 0800, 2026-11-26):** `sceneshot_test`'s timeout is 7200 s by default
(was 1,500 s; it ran past 3600 s beside nine heavy jobs four times on 2026-11-25/26). On a loaded workstation or under
`--asan` add `--sb-timeout-scale 2` (every scene-bundle program's limit times 2, sceneshot_test 14400 s; `--sb-timeout S`
sets the per-program default, 600 s, and keeps sceneshot_test's floor); run_all forwards them to
`build_scenebundle_tests.py --run-only` as `--timeout-scale` / `--timeout`. A program that runs out of time is NOT A RESULT:
the suite line reads `SUITE scenebundle-programs TIMEOUT ... 1 timed out (timed out, no verdict (NOT A RESULT):
sceneshot_test.exe after 7200 s; ...)`, never FAIL; the last line is `run_all: NO VERDICT: ...` and the exit 4 when
nothing else failed (a real failure still gives `FAILED` and exit 1, with the timeouts named after it). Then rerun the
named program alone, with a larger limit, through quietlock:
`python -I server/tests/render/scenebundle/build_scenebundle_tests.py --out <out>/sb --run-only [--asan] --only sceneshot_test --timeout-scale 2`
(the builder itself: `TIMEOUT <exe>: no verdict after <s> s` lines, a `SUMMARY <p> passed, <f> failed, <t> timed out`
line, exit 4 for timeouts only). Replaces the line of 0482/0751 "under ASan run it alone with `--timeout 3600`".
`[L]` server/tests/render/test_run_all.py (9 tests, in the render-host suite) and one run below (0800).
Which EXE build a copy is (before pointing `--exe`/`C5R_EXE` at it; run_all refuses any but 4390913), from
`tools/harness`: `python -I -c "import sys; sys.path.insert(0,'.'); import exe_build as e; print(e.describe(e.exe_build(sys.argv[1])))" "<install folder>"`
· prints `... is build 4390913 (TimeDateStamp 0x546CD0A8, ...)` · 0674, 2026-11-22 `[L]`.
The fog scene's stand-in cases without run_all (one build, then any case by name; exit = failed cases, 99 without
C5R_EXE): `python -I perf/quietlock/quietlock.py run --who <t> --what "fow scene build" -- <python> -I <repo>/server/tests/render/fow/test_fow_build.py --out C:/c5-64bit-work/<t>/fowb --c5render <render/build.py --build-dir>`
(the render build dir holds `c5render.lib`; about 1 min), then
`C5R_EXE=<copy>/CivilizationV_DX11.exe C5R_GAME_DIR=<copy> C:/c5-64bit-work/<t>/fowb/bin/test_fow_scene.exe [case ...]`
(the 30 cases about 1 min, WARP) · 0674, 2026-11-22 `[L]`.
One suite by hand, render-host:
`C5_RENDER_TEST_BUILD=<dir>/bin C5R_EXE=<copy>/CivilizationV_DX11.exe C5R_GAME_DIR=<copy> PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider server/tests/render/test_render_host.py` (about 9 min).
The residency's rig tests alone (render_host_test.exe of build_render_tests.py; 0703, 2026-11-23, `[L]`, about 3.5 min):
`C5_RENDER_TEST_BUILD=<out>/bin C5R_EXE=<4390913 copy>/CivilizationV_DX11.exe PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q
-p no:cacheprovider -rfEs server/tests/render/test_bake_fit.py server/tests/render/test_bake_yield.py
server/tests/render/test_residency_source.py` (also in run_all's render-host suite since ccea7804d). Switch `C5_RENDER_BAKE_FIT`
(0 off = the default since the MC's ruling of 2026-11-23, 1 while the camera moves, 2 always), read by server.exe at start.
Trap: `python -I server/tests/render/test_bake_fit.py` alone fails `No module named 'renderfx'` (`-I` keeps the script's folder
off sys.path): run it through pytest as above or run_all.

### 2.4 I want "offline equals live" (scene-bundle rule A)
```
export C5_SCENE_LIBRARY=C:/c5-64bit-work/scene-library/bundles
export C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt
python -I server/tests/render/run_all.py --out C:/c5-64bit-work/<t>/rulea --exe <copy>/CivilizationV_DX11.exe --game <copy> --who <t> --only offline-equals-live
```
Set `C5_SCENE_LIBRARY` to the checked C: copy (4.3): only then does run_all run `scenelib.py check` (the test's own
default is the F: master). `C5_SCENE_ASSET_ROOT` may be exported globally: run_all passes it only to this suite.
`C5_P1_TOLERANCE` (`PIXELS:DELTA`) overrides the accepted `1:1`. A rule A gate that also runs `--only scenebundle-programs`
on a loaded workstation adds `--sb-timeout-scale 2`; a `TIMEOUT` suite (exit 4, `run_all: NO VERDICT`) is no result for
that program, never a failure: rerun it alone as in 2.3 (ticket 0800). Every rule A run so far ran in MC-B's slot ·
about 15 min for 16 bundles (0625, 2026-11-20: 17 passed, 900 s); 637-796 s (0042, 2026-11-21) · `[I]` ·
[offline-scene-harness.md](offline-scene-harness.md) "Commands a worker uses" 3, 6, 7 and the 0625 section,
[docs/briefs/epic-0301-offline-scene-harness.md](../../docs/briefs/epic-0301-offline-scene-harness.md).
Rule A's arms of a renderer path switch: the TEST variables the player reads replace the bundle's key, the picture must be
byte-equal with each. Frame jobs (0711): `C5_SCENESHOT_FRAME_JOBS=0|1|..8`, `C5_SCENESHOT_FRAME_JOBS_JITTER=0|1`; record
ahead (0753): `C5_SCENESHOT_RECORD_AHEAD=0|1|auto` with `C5_SCENESHOT_FRAME_JOBS=1` (the arms off / on / on + jitter). Each
run's logs say `sceneshot: render_config: record ahead 1 (TEST: C5_SCENESHOT_RECORD_AHEAD)` and, at the end, `sceneshot:
record ahead: on, driver lists N: lists K, ...` (K > 0 proves the path ran). The three arms and the other render suites as
one block: `bash server/evidence/0753/block_0753.sh [raon raj smoke 3d raoff 3 5]` (phases in `allb_0753.sh`; outputs
under `C:/c5-64bit-work/0753/allb`, `C5_0753_ALLB` moves them; no phase starts after `STOP_AT`, default 19:00) · 0753,
2026-11-25 · `[L]` (timings in pan-coherence.md 5.20).
The render-host suite under the D3D debug layer (0753, `[L]`): `C5_RENDER_TEST_KEYS="render_record_ahead=1;
render_test_record_ahead_always=1;render_test_d3d_debug=1" C5_RENDER_TEST_BUILD=<run_all out>/rt/bin python -I -m pytest -q
-p no:cacheprovider -rfEs --basetemp <dir> <the render-host suite's files, as run_all's SUITES lists them>`, then
`python -I -X utf8 server/evidence/0753/debug_scan_0753.py <dir>` (exit 0: the layer on and no corruption, error or
warning message; 1: messages, printed; 2: the layer was never on). `C5_RENDER_TEST_KEYS` (renderfx.py) adds any render keys
to every Rig config (a test's own keys win); the layer is `C:/Windows/System32/d3d11_3SDKLayers.dll` (present on the
workstation 2026-11-25).

### 2.5 I want the unit, effects, extras and feed tests
```
python -I perf/quietlock/quietlock.py run --who <t> --what "unit scene build" -- python -I server/tests/units/build_units_tests.py --out <dir> [--asan]
C5_UNITS_TEST_EXE=<dir>/bin/test_scene_units.exe C5R_GAME_DIR=<copy> C5R_EXE=<copy>/CivilizationV_DX11.exe PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q server/tests/units/test_units_runner.py
python -I perf/quietlock/quietlock.py run --who <t> --what "<name>" -- python -I server/tests/units/run_units_seed_test.py --out <dir> [--asan]
```
The pytest wrapper is `test_units_runner.py` (the `.cpp` header's `run_units_tests.py` does not exist). The same
`--out <dir> [--asan]` form runs `server/tests/units/run_units_fights_test.py`, `run_units_queue_test.py`,
`run_units_records_test.py`, `server/tests/effects/run_trigger_emitter_test.py`,
`server/tests/effects/run_effect_routes_test.py`, `server/tests/extras/run_extras_test.py`,
`server/tests/feed/run_feed_test.py`. Trigger scenes: 4.6. Unitfeed:
`python -I server/tools/unitfeed/build_unitfeed.py --out <dir>`, then
`UNITFEED_BIN=<dir>/bin C5_UNITS_DB=<db copy> C5R_GAME_DIR=<copy> C5R_EXE=<copy>/CivilizationV_DX11.exe UNITSCENE_EXE=<render build>/tools/unitscene/unitscene.exe UNITANIM_EXE=<render build>/tools/unitanim/unitanim.exe PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q server/tools/unitfeed/test_unitfeed.py --basetemp <dir>/pytest`
(also after any change to the scene bundle format). Unit-extras seams:
`python -I server/tests/extras/units/build_units_extras_tests.py --out <dir> --c5render <render build dir>` (required),
then `<dir>/bin/test_units_extras.exe` · 72/72 plain and ASan, 19/19 trigger scenes, unitfeed 19, seams 13/13
(0042 W32, 2026-11-20) · `[I]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 13.6, 19.2.
The roads scenes test (4940, 2026-11-22, `[L]`, 10 tests about 14 min): `python -I perf/quietlock/quietlock.py run --who <t> --what
'roads scenes' -- python -I -m pytest server/tests/extras/test_roads_scenes.py -q -p no:cacheprovider -rA --basetemp <dir>` with
`C5_SCENESHOT_EXE=<sceneshot build>/bin/sceneshot.exe`, `C5_SCENE_LIBRARY=C:/c5-64bit-work/scene-library/bundles`, `C5_GAME_DIR`
and `C5R_EXE` = a pure_vp install, `C5_SCENE_MODS=<mods548-vt asset root>/Documents/My Games/Sid Meier's Civilization 5/MODS`.
Traps (0642, 2026-11-23, `[L]`): `run_effect_routes_test.py` needs `--render-build <render/build.py build dir>` (the `--out <dir>
[--asan]` form alone exits 2); `test_units_runner.py` needs `C5_UNITS_TEST_EXE=<out>/bin/test_scene_units.exe`; the loader test of
`test_decal_wiring.py` needs `C5R_TINY_TAP` (a Tiny tap; the default path is on D:).
Trap (4982, 2026-11-23, `[L]`): a scene test that replays a library bundle with its recorded install exits 9 when that install's pool
instance is claimed by another run; give the player --install and --exe of an install outside the pool (C5_GAME_DIR, C5R_EXE), as
test_air_intercept_scenes.py and test_fix_unit_placement_scenes.py do. The C library copy's files are read-only: a test that copies a
bundle and rewrites a member copies content only (shutil.copyfile), as synhide.py does (else PermissionError in test_fix_air_bases_scenes E9b).
The models .psb reader test (4787, 2026-11-23, `[L]`, about 2 min each): `python -I perf/quietlock/quietlock.py run --who <t> --what
"test_psb" -- python -I models/build.py --target test_psb --out C:/c5-64bit-work/<t>/m-plain`, then with `C5R_GAME_DIR` = the
`render_game_dir` setting, through quietlock, `C:/c5-64bit-work/<t>/m-plain/test_psb.exe` (147 checks, 0 failures; without the variable it
skips with exit 77). ASan: the same build with `--asan --out C:/c5-64bit-work/<t>/m-asan`, run with the MSVC
`VC/Tools/MSVC/<ver>/bin/Hostx64/x64` folder on PATH (else exit 127).
The negative arm of a units case (0742, 2026-11-25, `[L]`, about 104 s build): `git archive HEAD` into a scratch dir, disable the one
guard the case covers in that copy, `python -I server/tests/units/build_units_tests.py --out <dir> --c5render <render build>`, run the
case: it must FAIL there and pass on HEAD.

### 2.6 I want the proxy's tests (most are scripts, not pytest files)
```
python -I perf/quietlock/quietlock.py run --who <t> --what "proxy farm guard test" -- python -I proxy/tests/test_farm_guard_proxy.py --scratch <new dir>
python -I -X utf8 proxy/tests/test_crash_filter.py --scratch <dir>
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider proxy/tests/test_build_settings.py proxy/tests/test_purity_drops.py
```
`test_farm_guard_proxy.py` is a script (`--scratch`, `--no-asan`; under pytest it errors "fixture 's' not found"):
11/11 (0042 W32, 0627, 2026-11-20). The same `--scratch <dir>` form runs `proxy/tools/test_cfgini.py`,
`proxy/tools/test_vp64helper.py`, `proxy/tests/combat_hold/test_combat_hold.py`, `proxy/tests/minimap_fog/test_minimap_fog.py`
(0727, 2026-11-25, `[L]`, about 1 min: `test_minimap_fog: 15/15 passed`, its binding variant needs the local EXE tables),
`proxy/tests/wheel/test_wheel.py` (0755, 2026-11-25, `[L]`, about 40 s through `quietlock.py run`: `test_wheel: 9/9
passed`, wheel_test 118/118 in the proxy's toolchain and under MSVC x86 ASan; `--no-asan` leaves the ASan run out;
its live form on the laptop, inside MC-B's grant, from the worktree root with the harness natives built: `SHIP=<bin/,
proxy/ with the proxy's CvGameCore_Expansion2.dll, composite/shell_composite.dll, purity.tsv, probe/wheel_probe.dll (perf/evidence/5042/build.bat),
saves/W5042-TINY_0008.Civ5Save> OUT=<F: folder> bash proxy/tests/wheel/live/laptop0755.sh`, two headed jobs of about 7 min,
key 1 then 0; the purity file for a pins-0.1b x64 DLL: `proxy/tools/derive_purity.py --src <pin src> --dll <deps dll> --out
<dir> --commit 1b87dd185`, seconds; camera-and-picking.md 11.6),
`proxy/tests/livereload/test_livereload.py`,
`proxy/tests/luaexec/test_luaexec.py`, `proxy/tests/ui_bridge/test_ui_bridge.py`, `proxy/tests/inout/test_inout.py`;
`proxy/tools/test_proxy_rt.py` needs `--build <build_server out>` (a debug or a release build since 0752; before it a
debug or fast server gave 231/235: d28's exit code and three strict checks, `FAILED: database` on the x64 DLL's
`Results::Bind(int, float)` import, [upstream-fix-batch-2.md](../03-dll-engine-boundary/upstream-fix-batch-2.md); on an
older main build the server with `--config release`); `proxy/tools/test_purity.py` needs the source at the
pin · only the two pytest files: 13 passed, 1 skipped `[L]` 2026-11-21 · [test-health-on-main.md](test-health-on-main.md) 4,
[farm-builder-guard.md](../02-exe-internals/farm-builder-guard.md).
The skip is `test_build_objects` (`C5_PROXY_BUILD_OUT not set`): set `C5_PROXY_BUILD_OUT=<build_server out>/proxy` (or a
`build_proxy.py --out` folder) and `test_build_settings.py` runs it on that build's objects: 4 passed, 5 s · 0603,
2026-11-24 · `[L]`.
Purity lists after a hand re-audit (0628, 2026-11-24, `[L]`): a `--shared` fork clone of your own
(`git clone --shared --no-checkout <the local Community-Patch-DLL clone> C:/c5-64bit-work/<t>/fork`, seconds), then
`python -I -X utf8 proxy/tools/derive_purity.py --make-baseline --fork C:/c5-64bit-work/<t>/fork` (rewrites
`proxy/model/purity-baseline.json`, about 15 s), `python -I -X utf8 proxy/tools/derive_purity.py --src
C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac --dll C:/c5-64bit-work/keep/pins-5.5/deps-x64-release/dll/CvGameCore_Expansion2.dll
--out <scratch> --commit 1b87dd185` (ends `N kept ..., 0 dropped`) and `python -I -X utf8 proxy/tools/gen_pure_list.py
--check`. A row re-audited at a later commit starts its evidence column with `[cited at <commit>]` ·
[proxy-design.md](../03-dll-engine-boundary/proxy-design.md) 17.5b, [release-builds.md](release-builds.md) 6.4.
hutest against a build_server output (0681, 2026-11-22, `[L]`, about 1 min): `python -I perf/quietlock/quietlock.py run --who <t>
--what hutest -- python -I proxy/tools/hutest/build_hutest.py --b <build_server out> --src
C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac --deps C:/c5-64bit-work/keep/pins-5.5/deps-x64-release --out <scratch>`; ends
`hutest: PASS`.
The negative run (0693, 2026-11-23, `[L]`): add `--break`; it ends `hutest --break: PASS (every perturbed value was caught, nothing
else failed)` with 17 expected failures (20 since ticket 0314). `--no-build` reuses the hutest binaries in `--out`.
Since 0737 (2026-11-25, `[L]`) every hutest run's last line is `real Documents check: passed (...)`; a `FAILED` there
means the run wrote into the real Documents (exit 1; the entry is left alone, tell the MC). Each run folder holds its own
`Documents/My Games/Sid Meier's Civilization 5/VP64/reports` (the pacing check's `install-exe` report of every run lands
there). Offline check of the helpers: `python -I proxy/tools/hutest/test_documents_redirect.py` (9/9, seconds) ·
[proxy-design.md](../03-dll-engine-boundary/proxy-design.md) 34.4.
The proxy Lua bridge on upstream master (tickets 0308, 0313, 0314; 0314's scripts in
`research/03-dll-engine-boundary/evidence/0314/`, `[L]` 2026-11-24): (1) a `git clone --shared --no-checkout` of the local fork clone
`Community-Patch-DLL-x64` into `C:/c5-64bit-work/<t>/fork`, `git -C .../fork tag -f Release-5.4.9 ba8aafdca` (local only, never
pushed), `git -C .../fork worktree add --detach C:/c5-64bit-work/<t>/src-master 76b3a5f70` (the master port; build_all
`--unlocked` refuses a plain copy of any commit but the pin's); (2) master's test database, about 1 min when the game copy is
there: `python -I tools/release/stages/testdata.py --tag Release-5.4.9 --out C:/c5-64bit-work/<t>/testdata --fork
C:/c5-64bit-work/<t>/fork --base Release-5.4.8 --no-compare` (through `quietlock.py run`); (3) with `C5_RELEASE_TEST_DB=<t>/testdata/db/
Civ5DebugDatabase.db`: `python -I tools/x64-dll/build_all.py --src <t>/src-master --out <t>/master-deps --config release --offline
--unlocked --no-gate --jobs 6` (580 s); (4) `python -I -X utf8 server/build_server.py --src <t>/src-master --deps <t>/master-deps
--config release --offline --unlocked --out <t>/b-master` (about 100 s; the purity test is skipped on an unlocked source); (5) the
verify leg: `python -I proxy/tools/run_proxy_game.py --bin <t>/b-master/bin --proxy <t>/b-master/proxy/bin/CvGameCore_Expansion2.dll
--spec server/specs/continents-tiny-fixed-seed.json --turns 50 --mode proxy --fail-on-unexpected --ini strict=1 --ini local_answers=1
--ini local_verify=1 --ini test_slot_poll=4 --ini lua_mirror=1 --db <t>/testdata/db/Civ5DebugDatabase.db --loc-db
%USERPROFILE%/Documents/civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Localization-Merged.db --run <dir>` (add `--game-dir
"<t>/testdata/game/Sid Meier's Civilization V"` for master's game folder; the pin's leg takes `--db .../vp-modpack-5.4.8-cdrift/
Civ5DebugDatabase.db`); the counts are in `<dir>/proxy/server/server-report.txt` and `proxy-lua-calls.tsv`.
A DLL fork branch on Release-5.5 checked against upstream master (ticket 0762, 2026-11-25, `[L]` workstation; scripts in
`tools/x64-dll/evidence/0762/scripts/`, each step through `quietlock.py run`, `C5_JOB_OWNER` set): (1) a `--shared` fork clone
with remote `upstream` = LoneGazebo, `git fetch upstream master --tags` (tag `Release-5.5` = b0bce8c2f); (2) x64 compile check:
`python -I tools/x64-dll/build.py --src <branch worktree> --unlocked --config debug --census --out <dir>` (85 s; 0 errors,
327 unresolved = the closed libraries); (3) 32-bit builds of master, the branch and master again in ONE worktree folder,
`python build_vp_clang.py --config release` with LLVM on PATH, about 8.5 min each, started only while `perf/quietlock/linkplace.py
status` says "none taken" (it links with /LTCG): `s03_x86_builds.sh`; byte compare `tools/x64-dll/evidence/4705/scripts/pe32cmp.py
NEW REF` (never byte-equal, even master vs master); (4) 5.5 test data: `python -I tools/release/stages/testdata.py --tag Release-5.5
--out <t>/testdata --fork <clone> --base Release-5.4.8 --no-compare` (2 min 15 s; no DLL build needed: the tag's own DLL post-processes);
(5) the save test games 32-bit A vs B: `C5_RELEASE=<t>/testdata/Release-5.5.json python -I tools/x64-dll/evidence/0762/scripts/s12_x86ab.py
--a <A dll> --b <B dll> --out F:/... --games tiny,standard` (about 32 min beside other work; exit 0 = PASS; `report.json`); (6) the
debug product path, no LTO: `C5_RELEASE_TEST_DB=<t>/testdata/db/Civ5DebugDatabase.db python -I tools/x64-dll/build_all.py --src <src>
--out <deps> --config debug --offline --unlocked --no-gate --jobs 6` (166 s), then `python -I -X utf8 server/build_server.py --src <src>
--deps <deps> --config debug --offline --unlocked --out <b>` (148 s), then the verify leg above with `--bin <b>/bin --proxy
<b>/proxy/bin/CvGameCore_Expansion2.dll --db <t>/testdata/db/Civ5DebugDatabase.db --game-dir "<t>/testdata/game/Sid Meier's
Civilization V"` (`s11_proxy_game.sh`, 30 turns about 9 min) · [upstream-export-e4-dll-commit.md](../03-dll-engine-boundary/upstream-export-e4-dll-commit.md) 10.
Upstream master live on one instance, the 32-bit stock route against the proxy route (0315, 2026-11-25, `[L]` c5h-b; scripts in
`research/03-dll-engine-boundary/evidence/0315/`, E below; claim, MC-B's grant, `C5_DOCUMENTS` set, owners exported): `python -I E/mods.py .
<inst> backup`, `python -I E/mods.py . <inst> swap` (master's three mod folders from 0314's testdata stage, the CP `.modinfo` with the
GamecoreInit imports, the mods database removed, `nopopups.please`), `python -I tools/instances/provision.py set-dll <inst> --dll
D:/c5-64bit-work/3515/i0ship-8be73b6f-ia/CvGameCore_Expansion2.dll` (the 32-bit master `-ia` build), then per arm `python -I -X utf8 E/prep.py
<inst>` (a new Tiny game), `cli.py -i <inst> launch`, `frontend.py dismiss`, `python -I tools/harness/lua.py --instance <inst> --state InGame
-f E/probe.lua`, `turns.py end`, `cli.py stop`; the proxy arm after `provision.py set-proxy <inst> --bin <build>/bin --proxy
<build>/proxy/bin/CvGameCore_Expansion2.dll --pdb`; compare: `python -I -X utf8 E/compare_lualog.py <arm1>/Lua.log <arm2>/Lua.log` (exit 0 =
the GamecoreInit lines equal). Restore, in this order: the recorded proxy and server binaries back if you swapped any by hand, `python -I
E/mods.py . <inst> restore keep-proxy`, `provision.py set-proxy <inst> --original --runs-to <new folder>`, `provision.py set-dll <inst>
--previous`, `python -I E/mods.py . <inst> restore` (all equal), `provision.py verify <inst> --quick`. TRAP: `set-proxy --original` refuses
("unrecorded file ... (in the mod folder)") while master's extra mod files are there, and the plain restore would delete the proxy layout:
hence `keep-proxy`. TRAP: in a UI context `rawequal` and `rawget` are nil (`attempt to call global 'rawequal'`): probe without them.
About 6 min per arm on Tiny.
The merge check of a proxy Lua bridge change (`proxy/src/proxy_lua.cpp`, `server/src/remote/*.cpp`; 0314 and 0315 checkers,
2026-11-25, `[L]`, about 20 min in all on the workstation, each through `quietlock.py run --who <t>`, `C5_DOCUMENTS` set): (a) the
build_server line of 1.2 on the 0.1b pins (230 s, its proxy step 111 s); (b) the build_proxy line of 1.2 (89 s), hutest and
`hutest --break --no-build` as above (65/65; 20 expected failures); (c) builders: compiled_by.py lists only build_server.py and
build_proxy.py (purity_unit.cpp), plus build_hutest.py found by grep; (d) `test_proxy_rt.py --build <B> --scratch <dir>` (235/235,
166 s), `proxy/tests/luaexec/test_luaexec.py --scratch <dir>` (9/9, ASan 23/23, 35 s), the two pytest files above, and the remote
tests: `python -I -X utf8 server/tests/remote/test_dll_fatal_report.py --out <dir> --build-out <B>` (47/47),
`test_exe_crash_report.py --out <dir> --build-out <B>` (135/135), `test_dll_interface_version.py --out <dir>` (19/19),
`server/tests/remote/stale_instance/test_stale_instance.py --out <dir> --stubs C:/c5-64bit-work/keep/pins-5.5/deps-x64-release/stubs
--src C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac` (24/24; both arguments required), `python -I
server/tests/luaraise/run_luaraise_test.py --out <dir>` (20231 passed, `run_luaraise_test: OK`); `<B>` = the (a) out folder. The two
crash-report tests need `C:/Program Files (x86)/Microsoft Visual Studio/Installer` on PATH from Git Bash (trap below).
Trap (0682, 2026-11-22, `[L]`): `test_proxy_session.py --only unit` still needs a `--build` argument; any path works.
The save-mods end at the DLL's first deserialization error and the proxy's server-fatal.txt rule (0744, 2026-11-25, `[L]`,
no game, no server build; the same PATH entry for vswhere): `python -I perf/quietlock/quietlock.py run --who <t> --what savemods --
python -I -X utf8 server/tests/remote/test_savemods.py --out <dir> [--asan]` -> `test_savemods: 25/25 checks passed` (about 12 s;
`--asan` adds the MSVC ASan build, its canary and the cases again: 55/55, about 1 min); the proxy's end-to-end cases `death-server-fatal-pid` and
`death-server-fatal-otherpid` are in `python -I -X utf8 proxy/tools/test_proxy_session.py --build <B> --scratch <dir>` (needs a
build_server out); research `../03-dll-engine-boundary/save-mod-mismatch.md` 6.
The proxy's `Fatal` with a composite (ticket 0697): `test_proxy_rt.py --build <build_server out> --scratch <dir> --only-shell` runs the
cases `shell-fatal`, `shell-fatal-flushhang`, `shell-fatal-old`: `rt_tests/composite_standin.cpp` built with
`/DSTANDIN_FATAL` (and `/DSTANDIN_NO_FLUSH`: an older composite) calls the proxy's hung sink `STANDIN_HUNG_AFTER_MS` ms
after its load; `STANDIN_FLUSH_HANG=1` makes its `VPShell_FlushTotals` never return (the 1,500-ms bound ends the wait).
The pacing unit alone (0718, 2026-11-23, `[L]`, about 10 s, no game): `python -I -X utf8 proxy/tools/test_proxy_rt.py --build <any path>
--scratch <dir> --only-offline pacing` -> `pacing_unit: N/N checks passed`. `test_proxy_rt.py` always needs `--scratch` (the MC's check
2026-11-24 stopped at the argument error without it).
The keyboard-hook unit tests alone (0720, 2026-11-23, `[L]`, about 1 min, 61 s with `--asan`; 9/9 checks, section 9 of the script):
`python -I perf/quietlock/quietlock.py run --who <t> --what kbhook -- python -I proxy/tools/test_proxy_rt.py --build <build_server out>
--scratch C:/c5-64bit-work/<t>/rt --only-kbhook [--asan]`. A stall table of composite logs: `python -I shell/evidence/0720/stalls.py
<shell_composite*.log>`.

### 2.7 I want the tools, tracker, queue and ops tests
| Folder | Command | Count, time, last |
|---|---|---|
| harness (all) | `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/harness/tests` (through the lock) | 2,201 passed, 17 min (0627, 2026-11-20) `[I]` |
| HI kit | `... pytest -q -p no:cacheprovider tools/harness/tests/test_hi_kit.py` (it is in `tools/harness/tests/`, not `tools/hi/kit/`) | 105 passed, 15 skipped, 8 s `[L]` |
| x64 DLL tooling | `... pytest -q -p no:cacheprovider tools/x64-dll/tests` | 151 passed, 27 s `[L]` |
| queue | `python -I -X utf8 -m unittest discover -s tools/queue/tests -p "test_*.py"` (through the lock) | 111 tests, 36 min (9085, 2026-11-24) `[L]` |
| tracker | `python -I -m unittest discover -s tracker -p "test_build.py"` (and `-p "test_live.py"`; `python -I -m unittest tracker/test_build.py` cannot import) | live 16 OK; build 55 run, 1 FAILED on main 4e567dc68 (page over its 2 MB budget) `[L]` |
| ops | `... pytest -q -p no:cacheprovider ops/tests --ignore=ops/tests/test_hooks.py` and `python -I ops/tests/test_hooks.py` (a script) | 39 passed, 10 s; hooks exit 0, 28 s `[L]` |
| orphan monitors | `... pytest -q ops/monitors/test_orphan_monitors.py` | 4 passed, 2 s `[L]` |
| installer | `... pytest -q -p no:cacheprovider tools/installer/tests` (through the lock; every Inno Setup test install runs on a hidden desktop in a job object, `hidden_run.py`, so it takes no focus on the workstation, ticket 0728) | 95 passed, 7 min `[L]` 2026-11-22 (9081; 36 in 0627); the no-focus form not yet run whole on the workstation |
| release | `... pytest -q -p no:cacheprovider tools/release/tests` (through the lock; do not commit in that checkout while it runs) | 414 passed, 100 min (0627) `[I]` |
| installer: options page data (9081) | `python -I -X utf8 -m unittest discover -s tools/installer/tests -p test_options_schema.py -v` (builds a stand-in server.exe with LLVM clang++) | 22 tests, 3 s `[L]` 2026-11-21 |
| installer: install and removal include | `python -I perf/quietlock/quietlock.py run --who <t> --what "installer include tests" -- python -I -X utf8 tools/installer/tests/test_install_removal.py [-k CLASS]`; negative control: `P64TEST_INSTALL_ISS=<mutated copy of install.iss>` | see the 9081 row of [installer-notes.md](installer-notes.md) 16.7 `[L]` 2026-11-21 |
| installer: include compiles, no window (workstation-safe) | `MSYS_NO_PATHCONV=1 "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" /Q /O<scratch> tools/installer/tests/p64test.iss` (compiles `install.iss` into the test driver; runs nothing, so no Setup window; the include tests start the driver on a hidden desktop since ticket 0728) | exit 0, a few s `[L]` 2026-11-24 (0733) |
| installer: no-focus checks (0728, workstation-safe) | `python -I -X utf8 -m unittest discover -s tools/installer/tests -p test_no_foreground.py -v` (static: no installer start outside `hidden_run.run`; offline: what CreateProcessW gets; live: Python on the hidden desktop, timeout ends the tree; starts no installer) | 10 tests, 10 s `[L]` 2026-11-25 |
| installer: include tests on a host without ISCC/clang (0728) | build on the workstation (no window): `MSYS_NO_PATHCONV=1 "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" /Q /O<P> tools/installer/tests/p64test.iss` and `"C:/Program Files/LLVM/bin/clang.exe" -O1 tools/installer/tests/standin_vp64helper.c -o <P>/helper/vp64helper.exe`; then `P64TEST_PREBUILT=<P> P64TEST_WORK=C:/c5-64bit-work/<t>/tests python -I -X utf8 -m unittest discover -s tools/installer/tests -p test_install_removal.py` (and `-p test_vpfix.py`) | oldpc 49 tests 104 s, 20 tests 47 s `[L]` 2026-11-25 |
| installer: foreground log of silent test installs (0728; NEVER on the workstation: phase A takes the foreground) | `python -I -X utf8 tools/testbed/tb.py --host oldpc submit --name "<t> installer foreground probe" --headed --timeout 3600 --expect-min 25 --file <P>=p64 -- "%TESTBED_PYTHON%" tools/installer/tests/evidence/0728/fg_probe.py --prebuilt "%TESTBED_BIN%/p64" --reps 5 --suite` (`<P>` as the row above; out: `fg_summary.json`, `fg_events.jsonl`) | 4 min `[L]` 2026-11-25, job 20261125-002441-a242 · [keyboard-hook-and-hangs.md](../02-exe-internals/keyboard-hook-and-hangs.md) 6.1 |
| options (both trees) | `python -I ipc/build.py --arch both --target test_options`, then through `quietlock.py run` with absolute paths `<repo>/build/<arch>/Release/options/test_options.exe <repo>/docs/OPTIONS.md <repo>/build/<arch>/Release/options/scratch` (`<arch>` x86, x64) | 838 passed in each tree `[L]` 2026-11-22 (9081) |
| this runbook | `... pytest -q -p no:cacheprovider tools/harness/tests/test_runbook_paths.py` | 5 passed, 1 s `[L]` |
| others | the same pytest form per folder: `tools/d10/tests`, `tools/diskwatch`, `tools/exebuild/tests`, `tools/export/tests`, `tools/instances/tests`, `tools/scenario`, `tools/scenelib`, `tools/testbed`, `tools/wait`, `tools/tests`, `tools/humanplay/tests`, `tools/hi/tests`, `tools/hi/journeys/tests` | counts in [test-health-on-main.md](test-health-on-main.md) 3 |

`...` = `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest`. The harness's native shim test is a script: build the
natives (section 0), then `python -I -X utf8 tools/harness/native/tests/test_shim.py`. All `[L]` runs 2026-11-21.
The stop that waits for the EXE's end (0700, 2026-11-23, `[L]`, about 3 min): `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest
-q -p no:cacheprovider tools/harness/tests/test_core_stop_lingering.py`. Stand-in: `python -I tools/harness/tests/lingering_standin.py
--hold 40 [--old] --work C:/c5-64bit-work/<t>/x`.
A release-stage fix shown against main's code offline (0634, 2026-11-23, `[L]`): copy tools/release, tools/hi and
tools/harness/dialogwatch.py into a scratch folder (`C:/c5-64bit-work/<ticket>/old/tools/...`), put main's stage there with `git show
<main>:tools/release/stages/<stage>.py > ...`, then from that folder `python -I -X utf8 -m unittest discover -s tools/release/tests -p
test_<stage>.py -k <NewClass> -v` (hi_journey's 6 tests: 40 s). The release-tiers suite took 49 min on 2026-11-23 beside two other
suites; its log is written only at the suite's end.
test_options x64 alone from Git Bash (4982, 2026-11-23, `[L]`, about 2 min): `python -I perf/quietlock/quietlock.py run --who <t> --what
"test_options build" -- python -I ipc/build.py --arch x64 --target test_options`, then `quietlock.py run ... --
<repo>/build/x64/Release/options/test_options.exe <repo>/docs/OPTIONS.md <scratch>`: 905 passed on 2026-11-23 (838 on 2026-11-22).

### 2.8 I want the perf tests
One pytest per `tests` folder, never all of `perf/` in one session:
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider perf/speedkit/tests` (25 passed, 2 s),
`perf/turnbench/tests` (15 passed, 30 s), `perf/memsampler/tests` (25 passed), `perf/cputime/tests` · `[L]` 2026-11-21.
The ledger page tests need the ledger venv:
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 %USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I -m pytest perf/ledger/page -q` `[C]` ·
[perf/README.md](../../perf/README.md).
Frame-pacing instruments (0688, 2026-11-23, `[L]`): `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider perf/pacing/tests`
(~75 s); `C5_SHELL_BUILD=<shell build> PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests/test_pacing.py` (without the variable pytest may fail to start on a user-site plugin, 0705) (~2.5 min);
`C5_RENDER_TEST_BUILD=<run_all out>/rt/bin python -X utf8 -m pytest -q -p no:cacheprovider server/evidence/0688/test_pass_lines_0688.py`
(~1 min). Cost arms: `C5_PACING_COST_ROUNDS=6` / `C5_PASS_COST_ROUNDS=6` on the two `test_cost_arms_*` tests.
A run's pacing report (0688): `python -I -X utf8 perf/pacing/pacing_report.py --composite <run>/shell_composite.log [--csv <lockstep csv>]
[--render <run>/render.log] [--json OUT]` (F1-F6 of epic 0686; freezes joined to the server's long-pass lines).

### 2.9 I want the ASan variants
All use MSVC `cl /fsanitize=address` (LLVM 19's clang-cl ASan runtime crashes at start-up here), go through
`quietlock.py run`, run under a memory cap (`shell/tools/memcap.py`) with a canary that must be caught: shell
`shell\build.cmd asan` (2.2), frame `shell\frame\build.cmd <outdir> asan`, c5render `python render/build.py --asan --test`
(1.6), server render `run_all.py --asan` (2.3), single builders `--asan` (into `<out>/bin-asan`), unit/effects/extras/feed
scripts `--asan` (2.5), proxy scripts by default (`--no-asan` skips), options
`python -I options/test_asan.py [--out DIR]` · `[C]`. Under x86 ASan a fault caught by SEH leaves the frame poisoned
and the next report is false (4855).
The save-mods pure block under MSVC ASan with a canary (0681, 2026-11-22, `[L]`, ~40 s):
`C5_0681_ASAN=1 python -I -X utf8 server/tests/tools/test_save_mod_mismatch.py`.
The pacing unit under MSVC x86 ASan with the build table's include flags (0718, 2026-11-23, `[L]`, about 1-2 min):
`python -I perf/quietlock/quietlock.py run --who <t> --what "pacing asan" -- python -I -X utf8 server/evidence/0718/asan_pacing_0718.py
--scratch <dir>` -> `asan_pacing: OK` (its canary is caught first).

### 2.10 I want every builder of a shared source (the merge check "all builders of a shared source")
The tests and builders that compile, include or name given files, found mechanically (ticket 0605; the list a carry's
or any product change's acceptance carries):
```
python -I tools/exebuild/compiled_by.py <file> [<file> ...]             # text; --json, --all (every CMake target), --no-partners
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/exebuild/tests/test_compiled_by.py
```
Needs nothing built (static: batch/sh command lines, Python path expressions, CMake, `#include`) · about 6-10 s per
call on the workstation (reads every script once), the test 11-25 s · prints three groups: tests that compile it (or
name it: a Python script), partner tests (a small test that compiles it with another product source brings that
source's tests), product and tool builds; a CMake project with more than 8 hits is one summary line (run its ctest:
render `render/build.py --test`, 1.6) · 0605 (2026-11-24) · `[L]` · the tool's docstring (what it does not see) and
`research/02-exe-internals/exe-build-table.md` section 8 step 7. The table below is the hand-made scan it replaces;
keep it as a cross-check.

A change to a file that several builders compile needs a link of each (0598 broke three builders, 2026-11-20). Static
scan of 2026-11-21 (one-off 9080, from main 4e567dc68; CMake trees not covered). A worked all-builders pass:
[unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 19.2. A worked pass for a change to
`render/src/unitanim/combat.cpp` or `combat.h` (4743, 2026-11-22 08:37-09:12 `[L]`): `C:/c5-64bit-work/0042/w48/allb48.sh <1|2|3>`
(phase 1 render lib + server, about 4 min; phase 2 render ctest, trigger_scene + run_charge_scenes x2 + run_trigger_scenes
x4 + run_turn_scenes, units, extras, unitfeed, routes, emitter, about 19 min; phase 3 render-host, scene bundle,
sceneshot --smoke, recorder, farm, unit shadow, about 11 min), each build through `quietlock run --who <t>`, with
`C5R_GAME_DIR`/`C5R_EXE`/`C5_GAME_DIR` = a game copy outside the pool, `C5_JOB_OWNER`/`C5_POOL_OWNER`/`C5_QUIETLOCK_WHO`
and `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1` (the script is in a scratch folder: copy it). Abbreviations:
- **R13** = `server/build_server.py`, `server/tests/extras/units/build_units_extras_tests.py`,
  `server/tests/render/build_render_tests.py`, `server/tests/render/farmfields/test_farmfields_build.py`,
  `server/tests/render/forest/test_forest_build.py`, `server/tests/render/fow/test_fow_build.py`,
  `server/tests/render/overlay/test_scene_build.py`, `server/tests/render/scenebundle/build_scenebundle_tests.py`,
  `server/tests/render/shadow/build_scene_shadow.py`, `server/tests/render/test_render_host.py`,
  `server/tests/units/build_units_tests.py`, `server/tools/sceneshot/build_sceneshot.py`,
  `server/tools/unitfeed/build_unitfeed.py`.
- **U6** = `server/build_server.py`, `server/tests/extras/units/build_units_extras_tests.py`,
  `server/tests/render/scenebundle/build_scenebundle_tests.py`, `server/tests/units/build_units_tests.py`,
  `server/tools/sceneshot/build_sceneshot.py`, `server/tools/unitfeed/build_unitfeed.py`.

| Shared source | Builders |
|---|---|
| `server/src/render/srv_render_common.cpp` | R13 + `server/tests/render/cities/build_models_tests.py`, `cut_tap/build_cut_tap_tests.py`, `decal/build_decal_tests.py`, `ice/build_ice_tests.py`, `overlay/test_reader_build.py`, `shadow/build_shadow_tests.py`, `shadow/build_unit_shadow.py`, `water/build_water_tests.py` (21) |
| `options/src/vp64_options.cpp` | R13 minus `test_render_host.py`, + `proxy/build_proxy.py`, `proxy/tests/options_boot/test_options_boot.py`, `proxy/tools/test_proxy_rt.py`, `server/tests/extras/run_extras_test.py`, `server/tests/hidden_embark/run_hidden_embark_test.py`, `shell/build.cmd` (18) |
| `server/src/feed/srv_feed.cpp` | R13 minus `test_render_host.py`, + `server/tests/feed/run_feed_test.py`, `run_remote_feed_test.py`, `run_render_events_test.py`, `run_ui_record_test.py`, `server/tests/render/cities/build_models_tests.py` (17) |
| `server/src/render/srv_cut_tap.cpp` | R13 + `server/tests/extras/run_extras_test.py`, `server/tests/render/cut_tap/build_cut_tap_tests.py` (15) |
| `server/src/render/srv_liveterrain_exefield.cpp` | R13 + `server/tests/render/liveterrain/exe_field/test_exefield_build.py`, `liveterrain/test_liveterrain_build.py` (15) |
| `srv_decal_tap.cpp`, `srv_ice_tap.cpp`, `srv_overlay_tap.cpp`, `srv_shadow_tap.cpp`, `srv_unit_shadow.cpp`, `srv_water_tap.cpp` (all `server/src/render/`) | R13 + that tap's own test builder (`decal/build_decal_tests.py`, `ice/build_ice_tests.py`, `overlay/test_reader_build.py`, `shadow/build_shadow_tests.py`, `shadow/build_unit_shadow.py`, `water/build_water_tests.py`) (14) |
| `srv_liveterrain.cpp`, `srv_liveterrain_c5r.cpp`, `srv_liveterrain_icewaves.cpp`, `srv_liveterrain_derived.cpp`, `srv_liveterrain_pages.cpp` | R13 + the matching `server/tests/render/liveterrain/` builder (`test_liveterrain_build.py`, `derived/test_derived_build.py`, `pages/test_pages_build.py`) (14) |
| the rest of `server/src/render/` (`srv_render_host.cpp`, `srv_scene_bundle.cpp`, `srv_scene_recorder.cpp`, `srv_farmfields.cpp`, `srv_fow_*`, `srv_water_scene.cpp`, ...) | R13 (13) |
| `shell/frame/ring_sink.cpp` | R13 minus `test_render_host.py`, + `shell/frame/build.cmd` (13) |
| `server/src/units/srv_units_fights.cpp` | U6 + `server/tests/effects/build_trigger_scene.py`, `run_effect_routes_test.py`, `run_trigger_emitter_test.py`, `server/tests/units/run_units_fights_test.py` (10) |
| `server/src/effects/srv_trigger_emitter.cpp`, `srv_trigger_logsink.cpp` | U6 + `build_trigger_scene.py`, `run_effect_routes_test.py`, `run_trigger_emitter_test.py` (9) |
| `server/src/effects/srv_effect_routes.cpp` | U6 + `build_trigger_scene.py`, `run_effect_routes_test.py` (8) |
| `server/src/effects/srv_fight_geometry.cpp`, `srv_trigger_tap.cpp` | U6 + `build_trigger_scene.py` (7) |
| `server/src/units/srv_units_queue.cpp`, `_records.cpp`, `_seed.cpp` | U6 + the matching `server/tests/units/run_units_*_test.py` (7) |
| `srv_trigger_stream.cpp`, `srv_units_anim.cpp`, `srv_units_core.cpp`, `srv_units_mode.cpp`, `srv_units_scene.cpp` | U6 (6) |
| `render/src/common/status.cpp` | `run_trigger_emitter_test.py`, `run_extras_test.py`, `cities/build_models_tests.py`, `cut_tap/build_cut_tap_tests.py`, `decal/build_decal_tests.py`, `ice/build_ice_tests.py`, the three `liveterrain/` builders (9) |
| `ipc/src/marshal.cpp` | `proxy/build_proxy.py`, `proxy/tests/inout/test_inout.py`, `proxy/tools/test_proxy_rt.py`, `server/build_server.py`, `server/tests/run_rt_ring_test.py` (5) |
| `ipc/src/channel.cpp`, `common.cpp`, `endpoint.cpp`, `ring.cpp`, `wait.cpp` | `proxy/build_proxy.py`, `proxy/tools/test_proxy_rt.py`, `server/build_server.py` (3) |
| `server/src/render/cities/srv_models_mode.cpp`, `landmarks/srv_model_geom.cpp`, `srv_model_tap.cpp`, `srv_models_scene.cpp` | `server/build_server.py`, `cities/build_models_tests.py`, `scenebundle/build_scenebundle_tests.py`, `test_render_host.py`, `build_sceneshot.py` (5; `srv_model_layer_c5r.cpp` the same without `build_models_tests.py`) |
| `shell/composite/farm_guard.cpp` | `proxy/build_proxy.py`, `proxy/tests/test_farm_guard_proxy.py`, `shell/build.cmd` (3) |
| `server/src/extras/*.cpp` | `server/build_server.py`, `server/tests/extras/run_extras_test.py`, `build_sceneshot.py` (3); `server/src/extras/units/*.cpp`: `server/build_server.py`, `build_units_extras_tests.py`, `build_sceneshot.py` (3) |
| `proxy/src/proxy_combat_hold.cpp`, `proxy_effect_player.cpp`, `proxy_sound_exec.cpp` | `proxy/build_proxy.py`, its own `proxy/tests/<name>/test_<name>.py`, `proxy/tests/route_s/test_route_s.py` (3) |
| `proxy/src/proxy_rt.cpp`, `proxy_session.cpp`, `proxy_shell.cpp`, `proxy_update.cpp`, `proxy_queue_guard.cpp`, `proxy_pacing.cpp`, ... | `proxy/build_proxy.py`, `proxy/tools/test_proxy_rt.py` (2) |
| a `server/src/` file with its own `server/tests/*/run_*_test.py` (`srv_rt.cpp`, `srv_vfs.cpp`, `srv_crashdump.cpp`, `srv_hidden_embark.cpp`, `srv_main.cpp`, `remote/*.cpp`, `srv_guard.cpp`) | `server/build_server.py` + that test (2-3) |
| `render/src/**`, `models/**` (CMake) | every builder that links `c5render.lib` or `c5r_models.lib`: R13, U6, the `server/tests/render/*` builders, `build_trigger_scene.py`, `run_effect_routes_test.py`, `run_all.py`, `shell/build.cmd` |

TRAP: `build_trigger_scene.py --asan` on main needs `CL=-wd4456` in `<out>/vcvars-env.json` (the builder runs cl in
that cached environment; `/wd4456` passed from MSYS becomes a path) · 0796 W178 (2026-11-26) · `[L]`.

Each builder links with `python -I <builder> --out <dir> [--asan] [--c5render <dir>]` through `quietlock.py run` · `[C]`
(static scan, NOT VERIFIED by a link today).

A worked pass for a change to `server/src/render/srv_render_scene.cpp` with `server/src/extras/srv_extras.h` and `server/src/extras/srv_extras.cpp`
(mc-red, 2026-11-22 09:56-10:33 `[L]`, every build through `quietlock.py run --who <t>`, `C5R_GAME_DIR`/`C5R_EXE`/`C5_GAME_DIR` = a
4390913 copy outside the pool, `C5_SCENE_ASSET_ROOT` unset for the builders): (1) `run_all.py ... --only fow-scene --only
overlay-scene` (6 min); (2) `run_all.py ... --only render-host --only shadow-ice-scene --only water-scene --only forest-scene
--only scenebundle-programs --only scenebundle-py` (builds rt, shadow, water, forest, sb; 30 min); (3) `server/build_server.py`
(as in the pass above, 167 s), `build_units_tests.py` + `test_scene_units.exe`, `build_units_extras_tests.py` +
`test_units_extras.exe`, `server/tests/extras/run_extras_test.py --out <dir>`, `build_unitfeed.py`,
`farmfields/test_farmfields_build.py` (each `--c5render <a run_all out>/rt/c5render`; 8 min together),
`server/tests/effects/build_trigger_scene.py` (the header reaches it through `srv_units_scene.cpp`), and
`build_sceneshot.py --c5render <(2)'s out>/rt/c5render --common-objs <(2)'s out>/sb/obj/common --smoke`. Two `run_all.py`
runs may go side by side with the builders: the lock queues the builds. Do not edit the worktree while a run_all build step
runs: it compiles the files as they are when it reaches them (a mid-build edit spoiled mc-red's first "before" run); stop a
run with a `<out>/STOP` file, it ends after the current step.

A worked pass for a change to `render/src/liveterrain/`, `server/src/render/srv_liveterrain*.cpp` and
`server/src/remote/srv_remote_feed.cpp` (epic 0615's second request, `[L]` 2026-11-21 21:46-22:00, LINKS9 OK in 15 min on
a busy workstation; each line through `python -I perf/quietlock/quietlock.py run --who <t> --what "<step>" -- ...`, one
after the other; `GAME` = an install outside the pool, `W` = your link folder on C:):
```
export C5R_GAME_DIR="$GAME" PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
python -I render/build.py --test --build-dir "$W/render" --game-dir "$GAME"          # render ctest whole (108 tests)
python -I server/tests/units/build_units_tests.py --out "$W/units" --c5render "$W/render"
python -I server/tools/unitfeed/build_unitfeed.py --out "$W/unitfeed" --c5render "$W/render"
python -I server/tests/render/farmfields/test_farmfields_build.py --out "$W/farm" --c5render "$W/render"
python -I server/tests/render/liveterrain/test_liveterrain_build.py --out "$W/lt"
C5_LIVETERRAIN_BUILD="$W/lt" python -I -m pytest -q -p no:cacheprovider server/tests/render/liveterrain/test_liveterrain.py server/tests/render/liveterrain/test_icewaves.py
python -I server/tests/render/liveterrain/derived/test_derived_build.py --out "$W/derived" --c5render "$W/render"
C5_4904_DERIVED_BUILD="$W/derived" python -I -m pytest -q -p no:cacheprovider server/tests/render/liveterrain/derived/test_derived.py
python -I server/tests/render/liveterrain/exe_field/test_exefield_build.py --out "$W/ef"
C5_EXEFIELD_BUILD="$W/ef" python -I -m pytest -q -p no:cacheprovider server/tests/render/liveterrain/exe_field/test_exefield.py
python -I server/tests/render/liveterrain/pages/test_pages_build.py --out "$W/pages"
C5_0587_PAGES_BUILD="$W/pages" python -I -m pytest -q -p no:cacheprovider server/tests/render/liveterrain/pages/test_pages.py
python -I server/tests/feed/run_remote_feed_test.py --out "$W/feed"                   # srv_remote_feed.cpp
python -I -X utf8 server/build_server.py --src <src> --deps <deps> --config release --out "$W/srv"
C5_SHELL_BUILD="$(cygpath -w $W/shell)" cmd //c "shell\\build.cmd"
python -I -m pytest -q -p no:cacheprovider tools/harness/tests/test_safety_static.py
```
The 0.1b pins' `<src>`/`<deps>` were the kept copies under `C:/c5-64bit-work/keep/pins-0.1b/` · the script of that pass
(with a no-new-step cut time) is `C:/c5-64bit-work/0615/lead9/links9.sh`, outside git · 0615 (2026-11-21) · `[L]`.

---------------------------------------------------------------------------------------------------------------------
From Git Bash, shell/build.cmd through quietlock (4940, 2026-11-23, `[L]`): `MSYS_NO_PATHCONV=1 C5_SHELL_BUILD=<win dir> python -I
perf/quietlock/quietlock.py run --who <t> --what ... -- cmd /c "shell\build.cmd"`. Trap: the `cmd //c` form under quietlock opens
an interactive cmd that exits 0 WITHOUT building.
Trap (5050 W74, 2026-11-22, `[L]`): never edit a bash script while it runs in the background; bash reads the file as it goes.
Trap (0681, MC review 2026-11-22, `[L]`): a scan of build_server.py's source list misses test builders. Search every test script
for the changed file's name: `test_exe_crash_report.py` and `test_dll_fatal_report.py` compile `srv_exe_crash.cpp` on their own;
from Git Bash they need `C:/Program Files (x86)/Microsoft Visual Studio/Installer` on PATH for vswhere.
Every builder and rule A after a change to `server/src/units/srv_units_scene.{h,cpp}` or `render/src/extras/units/` (4983,
2026-11-23, `[L]`, ~100 min): `bash server/evidence/0455/4983/allb_4983.sh 1|2|3`.
For `server/src/units/srv_units_anim.{h,cpp}` with `srv_units_scene.cpp` (0685, 2026-11-24, `[L]`, phases 1-2 in 30 min, the
render ctest 11 min of it): `bash server/evidence/0685/allb0685.sh 1|2|3|4` (4983's phases plus 4: the units tests under
ASan with their own ASan c5render). Run the units exe directly too, `C5R_GAME_DIR=<install> C5R_EXE=<install>/CivilizationV_DX11.exe
<W>/units/bin/test_scene_units.exe [case ...]` (no case: every case; the pytest wrapper hides which case failed).
Trap (0685, 2026-11-24, `[L]`): two tests fail now and then under builder load, on code nobody changed: render ctest
`c5r_bakeresidency_test` (`visible_first_then_rings_and_gray_counter`: `bakes` 2 expected, or other timing checks; 3 of 3
alone passed, `C5R_EXE` set) and `test_scene_units feed_out_of_order` (`live_feed_no_race` drew 1 batch, needs 2; 1 of 6 full
runs). Re-run them alone before calling a change red; unit-combat-server.md 18.29.1 (5057, 2026-11-23) saw both.
Trap (5052 T, 2026-11-23, `[L]`): `test_scene_units --asan` needs its own ASan c5render: `build_units_tests.py --out DIR --asan`
WITHOUT `--c5render` (with the plain render lib the link fails LNK2038 annotate_string/annotate_vector). `srv_units_scene.h`
reaches every render unit through `srv_render_scene.h`: a change there needs the R13 builders and `build_trigger_scene.py`.
A worked builder pass for a default change in `combat.h` + `srv_units_*.h` + `srv_render_host.cpp` (4743 W81, 2026-11-23, `[L]`,
about 1 h 50 min on a busy workstation): `server/evidence/0042/4743/w81/scripts/allb81.sh <b|q|t|r|1|2|3|lw|sh|a>`, `allb81e.sh e`
(like-for-like identity), `allb81f.sh f1-f3` after a merge. Trap: the `--exe-base` arms of run_phase_scenes (`--expect-fail`),
run_names_scenes and walk/reform/city need a base from before their own knob's default change; against today's main they compare
main with main: compare the branch with `--no-4743` against main with `cmp_logs.py` instead. Trap: unitfeed's
`test_replay_one_command` (since 5052 T) needs `C5R_EXE`; its pytest needs `UNITFEED_BIN` and `C5_UNITS_DB`. Trap: a Python edit of
a `.sh` written through a bash here-document can leave a literal backslash-n: run `bash -n` and grep the new line.
Every builder after a change to `srv_units_core`, `srv_units_scene` or the units extras (4986, 2026-11-23, `[L]`): `bash
server/evidence/0455/4986/allb_4986.sh 1|2|3|4`.
Every builder after a change to `server/src/render/landmarks/srv_models_scene.h` (it reaches every server render unit
through `srv_render_scene.h`, so R13 + `cities/build_models_tests.py`), `srv_models_scene.cpp` or `srv_model_layer_c5r.cpp`
(0709, 2026-11-23 14:30-16:41, `[L]`): `bash C:/c5-64bit-work/0709/impl/allb0709.sh 1|2|3|4` from the worktree root
(outside git: copy it, edit `OWNER`/`W`): 1 = `run_all.py --only render-host --only scenebundle-programs --only
scenebundle-py --only fow-scene --only overlay-scene --only shadow-ice-scene --only water-scene --only forest-scene`
(builds rt, sb, fow, ovl, shadow, water, forest; 46 min on a busy workstation); 2 = `server/build_server.py` on the 0.1b
pins (158 s) and units, units extras, unitfeed, farmfields, sceneshot (`--common-objs <1>/ra/sb/obj/common`) with
`--c5render <1>/ra/rt/c5render` (4 min); 3 = rule A with `C5_SCENE_LIBRARY` on the C: copy (17 bundles, 13 min);
4 = `run_all.py --asan --only render-host --only scenebundle-py --only fow-scene` (36 min; `test_recorder_host.py` cases flake under ASan with "1 dumped frame(s) of 2", a different case each run). Each step through `quietlock.py run`
and not while desk 0136's last leading BARRIER post is a START (the script's `barrier()` waits). Plus
`build_models_tests.py --out <d>` and `--asan`, then `C5_MODELS_BUILD=<d> [C5_MODELS_ASAN=1] python -I -m pytest -q -p
no:cacheprovider server/tests/render/cities/test_models.py` (31 tests, 40 s). Trap `[L]`: `reader_torn` and
`feed_model_load_begin` failed once each under load (timing cases) and passed on every rerun alone.
A worked builder pass for a default change of a trigger-stream knob (srv_trigger_stream.h + srv_units_scene.{h,cpp} +
srv_render_host.cpp + trigger_scene.cpp + unitfeed.cpp; 5052 part D, 2026-11-23, `[L]`, about 70 min plus 32 min after a merge):
`server/evidence/0042/5052/w89/scripts/allb89.sh rl|b|q|r|1|2|3|lw|t`, `allb89m.sh m` after a merge of main. Phase b builds the
base trigger_scene and unitfeed in a detached worktree of main with `--c5render` of the same lib; phase r compares every runner's
default arm with main byte for byte (cmp_logs.py); uf_identity89.py does the same for unitfeed. Trap: run_names_scenes cases aw/gw
pass only `--striker-weapon`, so once a knob defaults on they no longer equal main's aw/gw: pair them by meaning (cmp_names89.py).
Trap: `python -I -c "..."` in double quotes with Markdown backticks inside runs the backticks as shell commands and drops the code
spans silently: write such edits as a script file.
Every builder, rule A and the five unit-extras scene tests after a change to render/src/units/placement.cpp,
render/src/unitanim/motion.cpp, render/src/extras/units/ or the units seam (srv_units_scene.*, srv_units_core.*, server/src/extras/units/)
(4982, 2026-11-23, `[L]`): from the worktree root `bash server/evidence/0455/4982/allb_4982.sh 1|2|3|4|5|6` (owner C5_ALLB_OWNER, work
C5_4982_ALLB). 1 = render ctest whole + build_server on the 0.1b pins, 11 min; 2 = U6 + units runners + the five unit-extras tests + farm,
unit shadow, sceneshot --smoke, 15 min; 3 = run_all render-host, scenebundle-programs, scenebundle-py, offline-equals-live, 33 min, only in
MC-B's rule A slot; 4 = the five unit-extras scene tests with phase 2's sceneshot, 17 min; 5 = run_all fow/overlay/shadow-ice/water/forest
scene suites, 13 min; 6 = phase 3 without offline-equals-live (no slot), for a relink after a main merge.

## 3. Test instances

Every workstation game launch by an account-A agent needs MC-B's grant first (a START-REQUEST on your ticket's
thread) and a claim. Offline commands (help, list, status) need neither. On oldpc and the laptop games run only as
testbed jobs (3.11). Instances live under `%USERPROFILE%/c5-instances` (C:) and `F:/c5-instances`; where each one is
now: [capacity-series.md](capacity-series.md) 10. The no-focus launch for test instances is `tools/harness/cli.py`
(the library form of `civ_bg.py`; the `civ_bg.py` copy under `research/sources/` does not run from this repository).

### 3.1 I want to see, claim, hand on, renew or release an instance (the pool)
```
python -I tools/harness/pool.py list [--json]
python -I tools/harness/pool.py claim --owner <you> --name <inst> --hours 6 --ticket <t> --address <queue address> --note "<what>"
python -I tools/harness/pool.py claim --owner <you> --ticket <t> [--profile mods548|modpack548] [--exe-build 4390913|26577625] [--length short|long]
python -I tools/harness/pool.py transfer <inst> --by <current owner> --ticket <t> --address <addr> [--to <new owner>]
python -I tools/harness/pool.py renew <inst> --owner <you> [--hours 12]
python -I tools/harness/pool.py release <inst> --owner <you>
python -I tools/harness/pool.py holder "<path>"          # exit 4 when the path lies in a claimed instance
```
A claim on an instance whose folder was removed (or retired): its owner clears it with the same
`pool.py release <inst> --owner <claim owner>`, which releases the claim file alone and prints one `notice: the folder of
<inst> (<dir>) no longer exists: its claim is released` line on stderr (exit 0); it refuses another owner, and refuses
while a process runs from the claim's folder when that folder still exists · `reap` never takes such a claim before its
lease lapses (an unplugged F: looks the same as a removed folder) · 0803, 2026-11-26, tested offline by
`tools/harness/tests/test_pool.py` `test_0803_*` (before 0803 `release` refused with "no instance <inst> under ...").
Run pool queries alone, not inside a chain (a `list` hung 2+ min in one, 4652) · about 2 s · claims in
`<pool dir>/claims/<name>.json`; `claim` prints the name, then the root · the ticket must be open and the address
live, else exit 2; `release` refuses while the game runs · 0643, 2026-11-21 (`tools/release/evidence/0643/s20_m43_run.sh`)
· `[L]` list and help 2026-11-21 · [tools/harness/README.md](../../tools/harness/README.md) "the pool (issue 9014)",
[run-and-drive-the-game.md](run-and-drive-the-game.md) 17.2.
A script that checks one claim reads it with `pool.read_claim` and `is_expired`, not `pool.py list` (4983, 2026-11-23, `[L]`:
the list took 3 min 10 s under load).
Trap (0698, 2026-11-23, `[L]`): `ticketq.py wait --for <your dispatcher's address>` takes the dispatcher's own entries (each entry
goes to the first reader): an implementer never waits on its dispatcher's address.

### 3.2 I want to check, provision or change an instance (`tools/instances/provision.py`)
```
python -I tools/instances/provision.py verify <inst> --quick                 # before and after a run
python -I tools/instances/provision.py verify <inst> --full                  # 56-76 s on C:
python -I tools/instances/provision.py set-dll <inst> --dll <path>/CvGameCore_Expansion2.dll
python -I tools/instances/provision.py set-dll <inst> --previous             # a run's restore step (not --original)
python -I tools/instances/provision.py set-proxy <inst> --bin <build>/bin --proxy <build>/proxy/bin/CvGameCore_Expansion2.dll
python -I tools/instances/provision.py set-proxy <inst> --original [--runs-to <new folder outside the instance>]
python -I tools/instances/provision.py create <name> --like <existing inst>   # coordinators; 8.2 GB, 106-123 s
python -I tools/instances/provision.py list
```
Changes refuse while any process runs from the instance · 0643, 0646 (2026-11-21) · `[L]` help ·
[tools/instances/README.md](../../tools/instances/README.md), [headless-and-parallel-testing.md](headless-and-parallel-testing.md) 3.8.
Stale SQLite journals (front end never idle): `python -I tools/harness/sqlite_journal.py scan --instance <inst>`,
then `clean --instance <inst> --owner <you> --backup-dir <dir>` · [run-and-drive-the-game.md](run-and-drive-the-game.md) 11.4.
Back up and restore files by hash: `python -I tools/harness/backup.py backup --out <dir> --instance <inst> [files ...]`,
`verify <dir>`, `restore <dir>` · `[L]` help.

**An instance on Vox Populi 5.5 (profile `mods55`)** (0766, 2026-11-25, `[L]`):
```
python -I tools/instances/vp_release_tree.py build --tag Release-5.5 --out C:/c5-64bit-work/keep/vp-release/vp-Release-5.5   # once; ~1 min
python -I tools/instances/vp_release_tree.py check C:/c5-64bit-work/keep/vp-release/vp-Release-5.5 --sha256 bc4c091a769ee248bc112d5e40c3d2a18905234f0bfe4cc26d4b9f22559d9918
python -I tools/instances/provision.py --root <root> create <name> --like c5h-new-a --profile mods55 --dll mod --exe-build 26577625
python -I tools/instances/provision.py set-dll <name> --dll C:/c5-64bit-work/keep/vp-release/dlls/vp55-ia/CvGameCore_Expansion2.dll
```
Run `create` through `quietlock.py run --who <you> --what "<what>" -- ...`: 8.2 GB, 134 s on C:, 324 s on F:. The 5.4.8
pin is refused on a mods55 instance (`--dll mod` = VP 5.5's own DLL, no Lua channel). The first launch activates the
mods (about 3-4 min). Do not put a game instance on F: while F: is saturated (games did not start on 2026-11-25, 0 %
idle): headless-and-parallel-testing.md 6.5. Compare a tree with an installed form: `vp_release_tree.py compare <tree>
--user "<My Games>" --install <game folder>` (exit 1 on a difference) · [tools/instances/README.md](../../tools/instances/README.md).

**VP 5.5 in the modpack form: the modpack, the instance (profile `modpack55`) and its turn-19 save** (0779, 2026-11-26, `[L]`):
```
M=research/06-tooling-and-workflows/evidence/2600/make_modpack55.py      # C5_POOL_OWNER = the claim's owner; a claimed mods55 instance with the -ia DLL
python -I $M check-mods <inst> ; python -I $M install-mm <inst>
python -I tools/harness/frontend.py -i <inst> prepare --mods cp,vp,eui --load REL55-TINY_0008 ; python -I tools/harness/cli.py -i <inst> launch   # then ingame / dismiss (3.5)
python -I $M base <inst> ; python -I $M copy <inst> ; python -I $M fill <inst>
python -I tools/harness/cli.py -i <inst> stop --human-turn-hook tools.harness.turns:stop_readiness
python -I $M harvest <inst> C:/c5-64bit-work/keep/vp-release/modpack55/VP_MODPACK ; python -I $M cleanup <inst>   # then the manifest's SHA-256 into provision.py
python -I tools/instances/provision.py create c5h-55-mpk --like c5h-new-mpk --profile modpack55 --exe-build 26577625 --dll C:/c5-64bit-work/keep/vp-release/dlls/vp55-ia/CvGameCore_Expansion2.dll --user-src <folder with the 3 ini files, outside the instances root> --min-free-gb 40
python -I -X utf8 tools/instances/evidence/0779/drive_t19.py prepare <inst> ; python -I tools/harness/cli.py -i <inst> launch
python -I -X utf8 tools/instances/evidence/0779/drive_t19.py play <inst> --to 20 --out <dir>      # AutoSave_0019 is written when turn 20 begins
python -I -X utf8 tools/instances/evidence/0779/drive_t19.py keep <inst> --to 19 --name MPK55-STD-T0019 --out <dir>
python -I tools/harness/frontend.py -i <inst> prepare --mods "" --load MPK55-STD-T0019 --restore-first   # a load in the modpack form
```
Modpack about 5 min in game (CreateMPMP 4 s, fill 24 s); create 93 s on C: (run it through `quietlock.py run`); new
Standard game to the load screen about 1.5 min; 20 turns about 3.5 min. Traps: `--user-src` inside the instances root is
refused; `--like` an instance whose manifest records saves from the owner's folder (c5h-55-c) re-reads them, so like
c5h-new-mpk (none); a failed `prepare` followed by `launch` starts the stock menu and no game (stop it at the menu) ·
[installer-notes.md](installer-notes.md) 14.1a, [headless-and-parallel-testing.md](headless-and-parallel-testing.md) 6.5.

### 3.3 I want to lay the product layout out and restore it by hash (`lay.py`)
There is no `lay.py` under `tools/`; the one in use is an evidence script (0600's copy adds `cfgextra`, `pack`):
```
python -I -X utf8 research/02-exe-internals/evidence/4967/lay.py prep <inst> --build <B> --work C:/c5-64bit-work/<t>/pp --composite-dll <composite>/bin/shell_composite.dll --composite-ini <shell build>/bin/shell_composite.ini
python -I -X utf8 research/02-exe-internals/evidence/4967/lay.py helper <inst> --work C:/c5-64bit-work/<t>/pp on
python -I -X utf8 research/02-exe-internals/evidence/4967/lay.py collect <inst> --work C:/c5-64bit-work/<t>/pp --label logs
python -I -X utf8 research/02-exe-internals/evidence/4967/lay.py restore <inst> --work C:/c5-64bit-work/<t>/pp
python -I -X utf8 server/evidence/0600/lay.py cfgextra <inst> --work <W> --file <F>     # vpproxy.ini server_cfg_extra, TEST ONLY
```
B = a `build_server.py --out` folder; a NEW work folder per prep. `prep` backs up by SHA-256 and refuses a running or
already proxied instance; `helper on` = the installer's config.ini form; `restore` runs every step even after a failure
and ends with `provision.py verify` · seconds · 4914 smoke, 0637 (2026-11-21) · `[L]` help ·
[new-exe-build-26577625.md](../02-exe-internals/new-exe-build-26577625.md) C runs, [live-terrain.md](../05-graphics-composition/live-terrain.md).
One test-only server.cfg line by hand: `python proxy/tools/live/set_ini.py "<mods>/(1) Community Patch/vpproxy.ini" server_cfg_extra=C:/c5-64bit-work/<t>/extra.cfg`,
and `--unset server_cfg_extra` before the restore · `[C]` [tools/harness/README.md](../../tools/harness/README.md).
A layout folder for the live-terrain smoke drivers (`server/evidence/0615/4914/smoke_4914.sh` with `PREP=lay`; epic 0615's
t8/t12/t13 shape) from a `build_server.py --out` folder `S` and a shell build `SH`, checked by hash:
```
L=C:/c5-64bit-work/<t>/t<n>/layout; mkdir -p "$L/bin" "$L/proxy/bin" "$L/composite/bin"
cp "$S"/bin/* "$L/bin/"; cp "$S"/proxy/bin/* "$L/proxy/bin/"; cp "$SH/bin/shell_composite.dll" "$L/composite/bin/"
(cd "$L" && sha256sum bin/*.dll bin/*.exe proxy/bin/* composite/bin/*.dll) > "$L/SHA256.txt"
```
The smoke's prep also wants `shell_composite.pdb` and `shell_composite.ini` from `$SH/bin` (W9's t13 copy stopped at
the prep without the pdb, 2026-11-21 23:41): copy the whole `$SH/bin` folder's composite files, not only the dll.
Record the code head in `$L/HEAD.txt`; the user copies it and runs `sha256sum -c SHA256.txt` · seconds · 0615 t13
(2026-11-21) · `[L]` · [live-terrain.md](../05-graphics-composition/live-terrain.md) 11.1.

A proxy or composite copied into a laid layout by hand (to test another build) must be put back to the recorded one
(`<B>/proxy/bin/CvGameCore_Expansion2.dll`) before `restore`: otherwise `provision.py set-proxy --original` refuses ("not the
recorded proxy") while the backup step still writes the original DLL back; recovery: copy the recorded proxy in and run
`restore` again (then `restore ok`) · 0655, 2026-11-21 · `[L]`. A layout from another `--build`: a new work folder.
`lay.py` (both copies, `server/evidence/0600/` and `research/02-exe-internals/evidence/4967/`) prep and restore exit 4 'refused: ...
have not ended ... Nothing was changed' while a process of the instance runs: wait for the stop to return, then run it again (0700,
2026-11-23, `[L]`). (Replaces the 2026-11-23 W84 recovery of a restore run while the EXE still lived.)
TRAP (0720, 2026-11-23, `[L]`): `lay.py prep` without `--composite-dll` plus `helper on` (ServerRenderer = 1, D33) makes the proxy's
boot fail by design (0624) and the EXE then crashes on the NULL context (K30, ticket 0721): always pass the composite on the product route.

### 3.4 I want the installer-laid layout on a test instance, and its removal
```
python -I research/06-tooling-and-workflows/evidence/2600/run_installer.py <installer .exe> --dir "<inst root>/<inst>/Documents/My Games/Sid Meier's Civilization 5" --gamedir "<inst root>/<inst>/install" --action install --procroot "<inst root>/<inst>" --log C:/c5-64bit-work/<t>/1-install.log
python -I research/06-tooling-and-workflows/evidence/2600/run_installer.py <installer .exe> --dir "<My Games>" --gamedir "<game>" --action remove --procroot "<inst root>/<inst>" --log C:/c5-64bit-work/<t>/3-remove.log
```
Before: `provision.py set-dll <inst> --dll <the tag's 32-bit DLL>` and a tree list with
`research/06-tooling-and-workflows/evidence/2600/treehash.py list`; after removal `treehash.py diff`, `set-dll --previous`,
`verify --quick`. The whole recipe as a driver: `tools/release/evidence/0643/s20_m43_run.sh <inst> check|install|swap|hi|unswap|remove`
(reads the instance root from `pool.py list`; do not copy 0626's driver, which hard-codes `F:/c5-instances`) ·
install 8-25 s, removal 3-12 s · exit codes of the installer ([tools/installer/README.md](../../tools/installer/README.md)) ·
0643, 2026-11-21 · `[I]`, options `[L]` help · [release-builds.md](release-builds.md) 7.5-7.8.
The owner's-build layout by installer on one instance and its removal (0683, 2026-11-22, `[L]`):
`bash server/evidence/0683/drv_0683.sh <inst> install|remove`.
TRAP (0716, 2026-11-23, `[L]`): an options file edited after the installer laid it (`set_opts.py` or by hand) is LEFT by the removal,
with `VP64/install/backup/<sha>` and `record.txt` (as designed); put it back from that backup by its SHA-256 and remove `VP64/` before
the tree diff. Patch arm driver: `bash server/evidence/0716/drv_0716_patch.sh <inst> install|remove` (the beta 3 release installer).

### 3.4a I want an instance back after a killed `hi_journey` tier stage (installed layout left on it)
When the stage's process dies (shell timeout, restart) its removal never runs. Everything needed is in the take:
`<work>/hi_journey/installer/<installer .exe>`, `installcheck/mg-0-before.tsv`, `kept/` (`*.bak` named
`<file>.<sha12>.bak`, `dll-before/<sha16>/CvGameCore_Expansion2.dll`) and `kept-saves/` if a start save existed before.
No game may run from the instance (`tasklist`); then, with `C5_JOB_OWNER`/`C5_POOL_OWNER` = the claim's owner:
```
python -I research/06-tooling-and-workflows/evidence/2600/treehash.py list "<My Games>" <work>/hi_journey/installcheck/mg-1b-at-recovery.tsv
python -I tools/harness/frontend.py -i <inst> status       # MainMenu.lua "patched" = the stage's prepare is still in place
rm "<My Games>/ModdedSaves/single/"{HI-4506-SAVED,HI-4506-BASIC-T0,HI-4506-LARGE-SCENE}.Civ5Save   # start saves (none in mg-0) and made saves
cp -p <work>/hi_journey/kept/GraphicsSettingsDX11.ini.<sha12>.bak "<My Games>/GraphicsSettingsDX11.ini"   # same for UserSettings.ini; sha256sum = the name's sha12
python -I tools/harness/frontend.py -i <inst> restore
python -I research/06-tooling-and-workflows/evidence/2600/run_installer.py <work>/hi_journey/installer/<installer .exe> --dir "<My Games>" --gamedir "<inst root>/<inst>/install" --action remove --procroot "<inst root>/<inst>" --log <work>/hi_journey/installcheck/3-remove.log
python -I research/06-tooling-and-workflows/evidence/2600/treehash.py list "<My Games>" <work>/hi_journey/installcheck/mg-2-removed.tsv
python -I tools/instances/provision.py set-dll <inst> --dll <work>/hi_journey/kept/dll-before/<sha16>/CvGameCore_Expansion2.dll
python -I tools/instances/provision.py verify <inst> --quick
python -I tools/harness/pool.py release <inst> --owner <claim owner>
```
Compare mg-0 with mg-2 as the stage does (`tree_diff` in `tools/release/tiers/stages/hi_journey.py`: rows under
`Logs\ cache\ ModdedSaves\ Saves\ ModUserData\` are run outputs, anything else is a difference); vpproxy.ini needs no
step (the removal deletes it; check its hash against `kept/vpproxy.ini.<sha12>.bak` first). No game launch, so it is
allowed while the workstation is closed to game runs · about 3 min · c5h-new-a, 0671, 2026-11-22 15:40-15:44: removal
exit 0, 0 differences outside the run outputs, verify exit 0 · `[L]` · [validation-tiers.md](validation-tiers.md) 19.

### 3.5 I want to launch, load a save, and get into the game
```
python -I tools/harness/frontend.py -i <inst> prepare --mods cp,vp,eui --load <SAVE NAME without extension>
python -I tools/harness/cli.py -i <inst> launch
python -I tools/harness/frontend.py -i <inst> ingame          # exit 0 in game, 1 not yet, 2 channel silent, 8 a box is open
python -I tools/harness/frontend.py -i <inst> dismiss         # close the load screen through the channel, once
```
Needs the claim, MC-B's grant, the harness binaries (section 0), the save as a copy with its own name in the
instance's `ModdedSaves/single` (never the owner's saves). One `prepare` per launch: a used load token is refused (exit 2)
unless `--reuse-load-token`. `cli.py stop` and a failed launch restore the front end themselves (`frontend.py restore`
by hand). Launch options (`[L]` help): `--mode cloak|offscreen`, `--timing` (uncapped; otherwise 30 fps),
`--audible`, `--keep-frontend`, `--assert-policy`, `--env NAME=VALUE`. A new game instead of a save:
`research/02-exe-internals/evidence/4967/c_prepare.py <inst> new` · launch to window 18-29 s, to in game on Huge t350
132-182 s on C: · `<inst>/run/harness_state.json`, `run/harness.log`, the guard log (read it after every real-EXE
launch, COMMON 14c) · 0626 play phase, 0635 (2026-11-21) · `[L]` help · [tools/harness/README.md](../../tools/harness/README.md)
"Core (1103)", [run-and-drive-the-game.md](run-and-drive-the-game.md) 11-12, 15.
Status and checks: `cli.py -i <inst> status [--json]`, `leftovers` (exit 7 if anything is left), `watch 30` (exit 5 if a
window took the focus), `framecap`.
A launch can pass a variable to server.exe (4983, 2026-11-22, `[L]`):
`python -I tools/harness/cli.py -i <inst> launch --env NAME=VALUE` (the proxy starts server.exe with the game's own
environment).
One Huge load with the product layout, then stop and collect (0690, 2026-11-23, `[L]`, ~7 min per load): after `lay.py` prep and
the helper on, with C5_JOB_OWNER and C5_POOL_OWNER set: `bash shell/evidence/0690/live_0690.sh <n> [inst] [save]`.
A forced D33 failure for failure-popup tests (0682, 2026-11-22, `[L]`): `python -I server/evidence/0600/lay.py prep <inst> --build
<build_server out> --work W --composite-dll <shell>/bin/shell_composite.dll --composite-ini <shell>/bin/shell_composite.ini`, then
`lay.py helper <inst> --work W on`, then `lay.py pack <inst> --work W --pack <pack.py out>/vp64pack` (pack=1 is needed for the
report and the popup), then `lay.py cfgextra <inst> --work W --file F` with F holding `render_test_init_fail=device`; the popup
comes about 70 s after launch. Remove the key with `python -I proxy/tools/live/set_ini.py <vpproxy.ini> --unset server_cfg_extra`.
The switch files' SHA-256 and the helper.log lines: `python -I -X utf8 server/evidence/0682/snap.py <inst> --label L --out F`.
A live pass of the stop (0700, 2026-11-23, `[L]`): `bash research/06-tooling-and-workflows/evidence/0700/live_stop_0700.sh
prep|run|ingame|end <inst>`.
A live check of a unit extra on the product path (look arms, orders staged by Lua, E1, end, stock) (4986, 2026-11-23, `[L]`): `bash
server/evidence/0455/4986/smoke_4986.sh src|build|dry|check|prep|arm|e1|statscmp|end|stock` (`C5_SMOKE_TREE` swaps in another
layout).
Live smoke of a unit extra on the 4980 kit with E1 (4982, 2026-11-23, `[L]`, about 26 min for the E1 pair, two arms and the stock arm):
`bash server/evidence/0455/4982/smoke_4982.sh src|build|dry|check|prep|e1|statscmp|arm|end|stock [INST] [ARM]` (a copy of 4986's; claim
first; an extra's counts are in the run's render.log ` | x shown=... foot=...` line, collected by lay.py collect under
C5_SMOKE_WORK/pp/<label>/runs-Logs-VP64/).
A main-thread stall probe from a second process, no input (0720, 2026-11-23, `[L]`): `python -I shell/evidence/0720/hangprobe.py sample
--pid <game pid> --seconds S --out F.csv` (WM_NULL SendMessageTimeout 200 ms every 250 ms + IsHungAppWindow; prints the unanswered
spans); `hangprobe.py activate --pid P on|off` posts WM_ACTIVATE so the EXE runs its hook install / unhook. The live kbhook check and its
stock arm: `B=<build_server out> SH=<shell build> bash shell/evidence/0720/live_0720.sh prep|patch|stock|end <inst>` (about 6 min per
arm on a Tiny save).
A live terrain check that needs a natural wonder the EXE does not know (0613, 2026-11-24, `[C]` the driver; live results in
[live-terrain.md](../05-graphics-composition/live-terrain.md) 8.13.5): launch with `cli.py -i <inst> launch --env
C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS=FEATURE_ULURU,FEATURE_KILIMANJARO` (TEST ONLY: the server's producer takes those stock
wonders as unknown; the mod set has none of its own). The whole check, four loads with the option on and off:
`bash server/evidence/0613/live_0613.sh build|dry|check|prep|run|end|compare [inst]` (0717's form; owner `C5_0613_OWNER`).
`[Extras] FixUnknownWonderBase = 0` on an instance after `lay.py prep` (restored by hash by `lay.py restore`): the `run` step's
in-place edit of `<user>/VP64Options.ini` (the commented default line made `FixUnknownWonderBase = 0`, or an `[Extras]` block appended).

### 3.5b I want a new game with a chosen civilisation, then a save of it (0766, 2026-11-25, `[L]`)
```
C5_FRONTEND_ASSETS=<a copy of 0580's fe-assets: the skill's MainMenu.lua + human=> python -I -X utf8 -c "import sys; sys.path.insert(0,'.'); from tools.harness import frontend; from tools.harness.instance import Instance; frontend.prepare(Instance.load('<inst>'), ('cp','vp','eui'), None, new_game='map=Continents;size=WORLDSIZE_TINY;ais=3;minors=-1;speed=GAMESPEED_STANDARD;handicap=HANDICAP_PRINCE;human=CIVILIZATION_POLYNESIA', restore_first=True)"
python -I tools/harness/cli.py -i <inst> launch ; python -I tools/harness/frontend.py -i <inst> dismiss
python -I tools/harness/lua.py --instance <inst> --state InGame -f tools/instances/evidence/0766/found.lua     # capital where the settler stands
python -I tools/harness/lua.py --instance <inst> --state InGame -f tools/instances/evidence/0766/choose.lua    # before EVERY turns.py end
python -I tools/harness/turns.py end --instance <inst> ; python -I tools/harness/turns.py wait --instance <inst> <turn before>
python -I tools/harness/lua.py --instance <inst> --state InGame -f <save.lua with @NAME@ replaced>              # file in ModdedSaves/single
python -I tools/harness/lua.py --instance <inst> --state InGame -f tools/instances/evidence/0766/info.lua      # turn, civ, majors, units
```
Needs an -ia DLL (the Lua channel), the claim, C5_HARNESS_BIN (the main checkout's `tools/harness/native/bin` when the
worktree has no build). `human=` exists only in 0580's copy of MainMenu.lua (`C:/c5-64bit-work/0580/fe-assets/`).
Without `choose.lua` a forced turn end with an empty city queue raises the -ia DLL's continuable assert
CvPlayer.cpp:34626 (a box: D155). New game to the load screen about 3 min on C:; 8 Tiny turns about 2 min ·
[headless-and-parallel-testing.md](headless-and-parallel-testing.md) 6.5.
The -ia DLL of Release-5.5 itself: runbook 1.9 in a worktree of the fork at Release-5.5 with
`git apply tools/d10/evidence/0766/ia-b0bce8c2f-0766.patch` and a version stub (or a take, which picks the patch by
base, IA_PATCHES row 0766); about 7 min under load.

### 3.6 I want to play turns, run Lua, take a screenshot, send keys
```
python -I tools/harness/turns.py end --instance <inst>
python -I tools/harness/turns.py wait --instance <inst> <turn_from> [--timeout T] [--stall S]
python -I tools/harness/lua.py --instance <inst> "return Game.GetGameTurn()"
python -I tools/harness/lua.py --instance <inst> --state InGame -f chunk.lua
python -I tools/harness/cli.py -i <inst> shot C:/c5-64bit-work/<t>/shots/x.png --scale 0.5 --what "DIAGNOSTIC: <what>" --result pass|fail|info --issue <t>
python -I tools/harness/cli.py -i <inst> key F6 ; python -I tools/harness/cli.py -i <inst> click 0.5 0.5
```
`turns.py end` exit 0 forced, 5 blocked, 7 not in game, 8 a box; `wait` 0 advanced, 2 stall, 3 timeout, 6 needs a
human. N turns with the leader screens and a shot at the end: `research/02-exe-internals/evidence/4967/c_drive.py <inst> --turns 10 --out <dir>`.
Lua answers only in a loaded game; never send NO_/NUM_ values · an AI turn on Huge t350 30-34 s, Tiny 4-5 s ·
0042/4778, 0626 (2026-11-21) · `[L]` help · [run-and-drive-the-game.md](run-and-drive-the-game.md) 3,
[headless-and-parallel-testing.md](headless-and-parallel-testing.md) 6.3. Account B may run `frontend.py dismiss`
and `turns.py` on test instances (D177).
Trap (0683, 2026-11-22, `[L]`): UI.LookAt does not move the camera to an UNREVEALED plot, nor does a minimap click there; use
`server/evidence/0683/look_near.lua` (nearest revealed plot, a one-shot LocalMachineAppUpdate handler). Observer on/off as a player
does it: `python -I tools/harness/lua.py --instance <inst> --state InGame -f server/evidence/0683/mt_observer.lua`, read the result
with state.lua; the 'off' toggle misses while no major is turn-active: retry until `Game.GetAIAutoPlay()` reads 0.
Reveal, then un-reveal, a block of plots and read what the EXE's minimap keeps (0552, 2026-11-24, `[L]` on c5h-a, build 4390913;
claim owner impl-0552 in the driver, edit it for another owner): `bash server/evidence/0552/drv_0552.sh <inst> snap|new|reveal|
unreveal|cells 4390913 <label>|shot <label> <result> "<what>"|save <name>|put <name>|load <name>|stop|install|remove` (install/remove =
the beta 3 release installer; `inputs` first fetches the VP 5.4.8 tag DLL from the fork clone and the installer, by hash); the reader
alone, read only: `python -I server/evidence/0552/mm_cells.py --pid <game pid> --build 4390913 [--block X0 Y0 W H]` (another build:
`--addrs SLOT_VA LIST_VA` from the local address map) · reveal/unreveal 5 s each, the reader 2 s · research 05
[observer-switch-minimap.md](../05-graphics-composition/observer-switch-minimap.md) 6. Trap: `saves_snap.py snap` refuses a second
snap in the same work folder; `put` uses the first snap, and the driver's `stop` restores from it.
Quick Combat / Quick Movement switched as the in-game options menu does, read back by PreGame (0718, 2026-11-23, `[L]`; claim owner
in `C5_POOL_OWNER`): `python -I server/evidence/0718/arm_0718.py quick --instance <inst> --qc 0|1 --qm 0|1`. Trap: `Game.IsOption(
GAMEOPTION_QUICK_MOVEMENT)` can read false while PreGame's is true; read PreGame. The live check of the D165 options against the quick
modes (D246): `bash server/evidence/0718/smoke_0718.sh build|dry|check|prep|run|base|end [inst]` (4986's form; about 25 min for run +
base on Tiny).
Quick options and a seat change, as a player does it (0716, 2026-11-23, `[L]` stock and beta 3): `bash server/evidence/0716/drv_0716.sh
<inst> put|launch|state <label>|obs|send <QC> <QM>|move|attack|stop`. `send` = OptionsMenu.lua's commit (Network.SendGameOptions on
the main thread; PreGame reads the new values a frame later); `move` / `attack` count the walk/teleport and RunCombatSim/EndCombatSim
events of the active player's unit; `launch` dismisses the load screen itself.

### 3.7 I want to stop the game safely
```
python -I tools/harness/cli.py -i <inst> stop --human-turn-hook tools.harness.turns:stop_readiness
python -I tools/harness/stop_nolua.py -i <inst> [--settle 60]          # a DLL without the Lua channel (cli.py picks it itself)
```
Safe only when the newest save under `Saves/` and `ModdedSaves/` is unchanged for the settle time and the human turn is
active (or the front end idle); never `taskkill`. Prefer an in-game exit through the channel first (`UI.ExitGame`,
0626's `g0626.py exit <inst>`). The server child ends by itself · settle 10 s + up to 60 s · exit 0 ok, 2 refused,
4 timeout (left running), 6 needs a coordinator, 7 orphans, 9 front end not restored · 0626, 0635 (2026-11-21) ·
`[L]` help · [tools/harness/README.md](../../tools/harness/README.md) "Stop safety (issue 1115)". A coordinator's
override (typed by a coordinator only, never from a cleanup path): `cli.py -i <inst> stop --coordinator-override "<who> <time>: <reason>"`.

TRAP (0673, 2026-11-22): a save made by an AI-autoplay run (for example `C5H-0167-TINY_0008`: the active player is an
observer, `Game.GetAIAutoPlay()` 2147483455) keeps playing turns after the load, one autosave every few seconds, so
`stop` never finds a settled save (it ran into a 200 s timeout). Stop the turns first, then stop:
`python -I tools/harness/lua.py --instance <inst> "Game.SetAIAutoPlay(0) return Game.GetAIAutoPlay()"` (answers 0;
the turn then stays put) · `[L]`.

TRAP (0679, 2026-11-22): a stock arm with Vox Populi's own released DLL has NO Lua channel: `frontend.py ingame` and
`dismiss` never answer ("the channel is dead ... no process had created the events"), the load screen stays up, and an
AI-autoplay save cannot be stopped from Lua once dismissed (a safe stop would then never settle). Stop on the load screen
with `stop_nolua.py -i <inst> --settle 60` (60 s is its minimum), then `frontend.py -i <inst> restore` (stop_nolua left
the front end prepared: `verify --quick` fails until restored). Use a save without autoplay for stock arms, driven by
mouse and keyboard only · `[L]`.
`cli.py stop` after its TerminateProcess waits until the EXE has ENDED (up to 600 s, `stop_end_wait` every 15 s). Exit 4 with a
line starting `LOUD:` means it has not ended: run the same stop again later, which resumes the wait without the gates. Never
taskkill (0700, 2026-11-23, `[L]`).

### 3.8 I found a box (D155, COMMON rule 29): check, close, stop, collect
```
python -I tools/harness/dialogwatch.py marker --instance <inst>      # exit 8 when a box is open; each turn you hold a game
python -I tools/harness/cli.py -i <inst> status --json               # the box's title, text, buttons
python -I tools/harness/cli.py -i <inst> dismiss --button OK --fatal --requested-by "<owner>: <why>"
python -I tools/harness/cli.py -i <inst> stop --after-fatal
python -I tools/harness/cli.py -i <inst> dismiss --button "Close program" --fatal --requested-by "<owner>: <why>"   # WER box
python -I tools/harness/cli.py -i <inst> dismiss --button Close --patch-popup --requested-by "<owner>: <why>"     # the patch's own popup
python -I tools/harness/crash.py collect --instance <inst> --since <launch epoch seconds> [--pid PID]
python -I tools/harness/dialogwatch.py ledger                        # signatures with their bug (read; --fill writes)
```
The watcher is started by every launch; do not start it by hand. Any other box: a coordinator decides. `crash.py
collect` copies into `<run_dir>/crash/<stamp>_pid<pid>/` and writes a full dump with `cdb -pv` while a box holds the
game · `--fatal`/`--after-fatal` no run found 2026-11-11..21; `marker` 0643 (2026-11-21) · `[L]` help ·
[run-and-drive-the-game.md](run-and-drive-the-game.md) 17, [crash-ledger-audit.md](crash-ledger-audit.md),
[known-crash-causes.md](known-crash-causes.md).
A crash at exit with no box (0678, 2026-11-22, `[L]`): before the exit, start
`python -I shell/evidence/0164/cdb_watch.py start --pid <game pid> --out <F: folder>`; it writes a full dump (about 1.5 GB,
about 20 s) when the process faults. Write it to F:, not C:.

### 3.9 I want to hear the game's sounds (raise the Effects volume for a sound check)
No tool raises a volume: `audio.py` only mutes and measures, and every instance has `Effects.Volume = 0`. Launch with
`--audible` (no mute watch) if a person must hear it; raise the knob in game through the channel (nothing is saved):
```
python -I tools/harness/lua.py --instance <inst> --state InGame "local id = GetVolumeKnobIDFromName('USER_VOLUME_SFX') local b = GetVolumeKnobValue(id) SetVolumeKnobValue(id, 1) return id, b, GetVolumeKnobValue(id)"
python -I tools/harness/audio.py meter --out C:/c5-64bit-work/<t>/meter.tsv --seconds 90 --exe-under <inst root>/<inst>/install
```
The chunk is `VOL_LUA` of `server/evidence/0042/4734/pair_4734.py`, sent by the drivers through `lua.run_lua`; this CLI
form is NOT VERIFIED · 0042/4779 (2026-11-21) `[I]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 10.8,
[exe-audio.md](../02-exe-internals/exe-audio.md) 12-13, [silent-start.md](silent-start.md).

### 3.10 I want to drive by mouse and keyboard (human-input testing) or replay a journey
`-i` and `--by` go BEFORE the subcommand:
```
python -I tools/hi/hi.py -i <inst> --by <addr> session start --ticket <t> --route product|stock
python -I tools/hi/hi.py -i <inst> --by <addr> step <ACTION ...> ; python -I tools/hi/hi.py -i <inst> --by <addr> session end
python -I tools/hi/hi.py -i <inst> --by <addr> replay tools/hi/journeys/release-basic.jsonl --route product --ticket <t>
python -I tools/hi/judge.py -i <inst> state
```
Part of a journey on a game you launched and loaded yourself (cand4's reload, 2026-11-23 `[L]`): `replay <trace> --route
product --ticket <your claim's ticket> --from N --until M` with NO `session start` before it (the replay opens its own:
exit 2 "a session is active"); without `--ticket` it takes the trace's ticket and refuses a claim of another (exit 2);
it checks the trace's checkpoints from `--from` on (exit 6 when the save is not the journey's: replay a copy of the
trace without its `"t": "checkpoint"` lines). Worked form: `server/evidence/0673/reload/reload_check.sh <inst> prep|run|end`
(`TRACE=<copy>`; release-basic steps 52-63 = save, load that save, wait 30 s; renderer-in-server-design.md 3.2.2).
The D162 release journeys through the take's runner (claims the instance itself; release your claim first):
```
C5_RELEASE_HI_INSTANCE=<inst> C5_RELEASE_HI_TICKET=<t> C5_RELEASE_HI_ADDRESS=<addr> C5_RELEASE_HI_BY=<owner> python -I tools/release/take_release.py Release-5.4.8 --tier validated --only hi_journey --work <work folder with state.json>
```
(installer-laid instance: `C5_RELEASE_HI_INSTALLED=<inst> C5_RELEASE_HI_ROUTES=product C5_RELEASE_HI_JOURNEYS=release-basic,release-large`) ·
After ticket 0671 the command above, with neither `C5_RELEASE_HI_INSTALLED` nor `C5_RELEASE_HI_ROUTES` set, plays the
tier's way (installer test build from the take, install on the clean instance, product route with the composite,
removal, stock route fresh on the stage-laid layout; `C5_RELEASE_HI_STAGE_LAID=1` = the old layout, diagnostic) and
needs a 26577625 instance; unset `C5_POOL_OWNER` and `C5_RELEASE_INSTANCE` in its shell (0671's form:
`C:/c5-64bit-work/0671/scripts/run_live.sh`, run from the worktree root). Start it with the tool's background mode, never as a
foreground call under a shell timeout: 0671's first live run died 12 min in with no traceback and left the game and the
installed layout on the instance (2026-11-22 14:42, `[I]`; recovery: 3.4a) ·
both journeys both routes about 40 min; installed, product route about 20 min · `<work>/hi_journey/report.md` ·
0643, 2026-11-21 `[I]`; by hand 4609, 2026-11-18 · `[L]` help · [human-input-testing.md](human-input-testing.md),
[docs/briefs/hi-tester.md](../../docs/briefs/hi-tester.md), [tools/release/README.md](../../tools/release/README.md) 5a.
The journey set is chosen by the take's VP release (0770): `Release-5.5` plays the index entries with `"vp": "5.5"`
(their own traces `*-55.jsonl` and start saves), `Release-5.4.8` the entries without `vp`; force one with
`C5_RELEASE_HI_VP=5.5` (a `--base-commit` take whose tag names no version takes it from its descriptor's tag) ·
`[C]` 2026-11-25 · [human-input-testing.md](human-input-testing.md) 8.12.
One stock replay of a journey by hand on a game you launched to the main menu (the start save the newest file of
`ModdedSaves/single`, the made saves absent, cp,vp,eui prepared): `python -I tools/hi/hi.py -i <inst> --by <addr> replay
tools/hi/journeys/<journey>.jsonl --route stock --ticket <t> --out <dir on a disk that is not saturated>` · 7.5-10 min
on C:, two to three times longer with the output on a busy F: (0770, 2026-11-25, `[L]`) · re-recording a journey on a
new save (the old trace's steps issued through `hi.py step` in an open session, map steps by hand after `judge.py
plot-pixel X Y`): [human-input-testing.md](human-input-testing.md) 8.6, 8.12.

Is the laptop's desktop fit for the real-input lane (no lock screen shown, input desktop "Default")? Read-only, no game,
no input, about 1 s; book the laptop first (desk 0136):
```
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --slots 1 --name "<t> lock-state" --timeout 300 -- "%TESTBED_PYTHON%" tools/hi/realinput.py lock-state
python -I -X utf8 tools/testbed/tb.py --host laptop logs <JOB> --tail 120
```
`desktop.lock_ui` False and `refusal_without_game` null = the lane would not refuse for the lock; `session.lock_windows`
lists the LockApp/LogonUI windows (visible, cloaked). A resident LockApp.exe alone is not a lock screen (0732). Exit 2 on
the workstation (the fence) · 0732, 2026-11-24 `[L]` (job 20261124-234104-a00a) · [human-input-testing.md](human-input-testing.md) 10.2, 10.5.

### 3.11 I want the headless save-identity test (D10) or a headless game
```
python -I tools/d10/run_d10.py --out C:/c5-64bit-work/<t>/d10
python -I tools/d10/run_d10.py --out <new dir> --x86-dll <dll> --srv-bin <build>/bin --jobs 8
python -I tools/d10/run_d10.py --out <new dir> --games tiny --allow-subset
```
Defaults from the `release` keys of `deps/i0.lock` and `deps/dll.lock`; needs the civ5-headless checkout
(`%USERPROFILE%/Documents/civ5-headless`); gated by the quiet lock · tiny 150 s; standard + huge 25.5 min at
`--jobs 10` · `<out>/report.txt` · 4813, 2026-11-20 · `[L]` help · [d10-save-identity-test.md](d10-save-identity-test.md).
One save re-saved by both sides: `python -I tools/humanplay/resave.py <save> --out C:/c5-64bit-work/<t>/resave --srv-bin <build>/bin`.
A single c5h game (from the civ5-headless checkout): [headless-and-parallel-testing.md](headless-and-parallel-testing.md) 1.
A long start save on a new VP release, headless with the 32-bit DLL, loadable by the client (0771, 2026-11-25, `[L]`):
database `cd ~/Documents/civ5-headless && ./c5h.cmd dbcache --snapshot cache/db/snapshot/4DF730138BA1F0EE/snapshot.db
--mods-dir <release tree>/user/MODS --mod <CP>:<v> --mod <VP>:<v> --mod <EUI>:1 --dll <DLL>` (12 s); a game folder WITHOUT
VT and the modpack (a copy of the toolkit folder: an instance's install has VT and the header then lists 16 DLC, not
13); then `DLL=... DB=... LOC=... GAMEDIR=... sh tools/hi/evidence/0771/scripts/c5h_run.sh NAME SPEC TURNS` (quietlock,
below-normal priority; a 43-slot spec through `scripts/resolve43.py`; put the run folders on C: with `RUNS=` and pass
`--stall-seconds 3600`: on a saturated F: two boots stalled 15 min) · Large T200 28-37 min, Huge T350 35 min (run folder on
C:) to 74 min (on a busy F:) · `<run>/saves/Final_<turn>*.Civ5Save` (player 0 human) · a huge reference must then be
played to the save test's last turn (`scripts/huge_chain_runs.sh`: 350 -> 400 about 20 min): one 5.5 game ended at 394 · Trap: `cmd //c c5h.cmd` from Git Bash fails on a DLL
path with parentheses; the driver calls `python -m civ5headless` · [human-input-testing.md](human-input-testing.md) 8.11.
Identity job against the 0.1b pin (4808, 2026-11-23, `[L]`): first a pin layout, `python -I -X utf8 server/build_server.py --src
<keep src> --deps <keep deps> --config release --no-proxy --out F:/c5-64bit-work/<t>/pin-layout` (169 s), then
`idjob.py --layout-bin <that>/bin`. Trap: idjob's default layout is still batch 5's.
A DLL package on against off (0730, 2026-11-25, `[L]`): two copies of one layout `bin`, the package's DLL and PDB put
into one; then per arm, one after the other,
`python -I tools/d10/run_d10.py --out <w>/d10-<arm> --srv-bin <w>/<arm>/bin --order-exception none --d18-x86 yes --d18-x64 yes --jobs 4`
(26 min each on the workstation; `perf/evidence/5038/run_d10_0730.sh`). Across the two runs compare statistics, not
save files: `python -I tools/d10/statscmp.py <w>/d10-on/<game>/x64-direct/cache/stats.db <w>/d10-off/<game>/x64-direct/cache/stats.db`.
New-game saves of two runs differ even with one DLL (per-game GUIDs), and a resave's pregame holds the run folder's
path; only the huge input's resaves compare (`tools/d10/savecmp.py`, body equal under exception (a)).
run_d10 does not run on oldpc as it stands (c5h from the home folder's checkout, without `--db`: quiet-testbed.md 14.3).
A headless x64 game on a map of any size (Tectonic with a forced size factor, the 5.5 pin; 0594, 2026-11-26, `[L]`): the
layout `bash research/07-architecture-decisions/evidence/0594/build_layout.sh` (550 s), the map
`python -I tools/stressmap/tectonic_stress.py --game-dir "<Release-5.5 game dir>" --factor F --out F:/c5-64bit-work/<t>/maps`
(floor(150 F) x floor(94 F) plots), then `bash research/07-architecture-decisions/evidence/0594/run_one.sh F 50 <timeout s>`
(`C5_RELEASE=Release-5.5` inside; memory sampled every 5 s; 75 s at 20,160 plots, 10 min map generation at 502,095); a
scratch DLL built from a local fork branch: `setup_fork.sh`, edit, `kb_wide.sh --config release` (546 s) and `LAYOUT=... SUFFIX=...`
on run_one.sh · past 32,767 plots the pin's DLL crashes (tactical AI) · [idea-0594-massive-maps-stage1.md](../07-architecture-decisions/idea-0594-massive-maps-stage1.md) 9.

### 3.12 I want to run something on oldpc or the laptop (the testbed queue)
Runs there go only through the testbed queue, with a `headless@H`, `game-instance@H` (`--headed`) or `quiet@H`
(`--quiet --expect-min N`) queue place; commit first (the job runs your HEAD). Never start anything over plain SSH.
```
python -I -X utf8 tools/testbed/tb.py --host oldpc status
python -I -X utf8 tools/testbed/tb.py --host <H> submit --name "<t> <what>" --timeout 1800 --file <x64 build>/bin=x64/bin -- "%TESTBED_PYTHON%" <script in the repo> <args>
python -I -X utf8 tools/testbed/tb.py --host <H> wait <JOB> --timeout 170        # re-arm in a loop (COMMON 21)
python -I -X utf8 tools/testbed/tb.py --host <H> logs <JOB> --tail 50
python -I -X utf8 tools/testbed/tb.py --host <H> fetch <JOB> --out C:/c5-64bit-work/<t>/<H>-<JOB> [--include "<glob>" | --all]
python -I tools/visualcheck/visualcheck.py pull --host <H>                       # before the hand-back
```
Import-check your driver first (`python -I -c "import <name>"` from its folder or its `--dry-run`): a driver that dies
on import after an hour's queue cost 12 min + 98 testbed min (4553). Grep `job.log` for "refused" (the laptop's commit
floor). Reorder with `tb.py move JOB --before JOB2`. oldpc runs Python 3.8 (`%TESTBED_PYTHON%`); `%C5H_PYTHON%` is 3.12 ·
shipping builds 21-24 s · 0042/4778, 2026-11-21 (`server/evidence/0042/4778/submit_4778.sh`) · `[L]` help and status ·
[quiet-testbed.md](quiet-testbed.md), [tools/testbed/README.md](../../tools/testbed/README.md),
[tools/testbed/RECIPE-queue-place.md](../../tools/testbed/RECIPE-queue-place.md). Ready-made jobs: `tools/testbed/jobs/`
(`headless_series.py`, `product_path.py`, `live_session.py` with `tools/testbed/live.py`).

A job on the host's **VP 5.5 copy** instead of its 5.4.8 slot instance (0802, 2026-11-26): name the copy and the 5.5
saves; without these two the job runs on 5.4.8 as before. Copies: `tb1-55` (laptop, oldpc), `tb2-55` (oldpc); roots
laptop `C:/c5-testbed`, oldpc `E:/c5-testbed`.
```
python -I -X utf8 tools/testbed/tb.py --host <H> submit --headed ... --env TESTBED_SAVES=<root>/base/saves55 -- \
  "%TESTBED_PYTHON%" tools/testbed/jobs/live_session.py --instance tb1-55 --load REL55-TINY_0008 --idle 900   # or capture_job.py / product_path.py --instance tb1-55
HOST=oldpc SAVE=VP55-HUGE_0350-B OUT=F:/c5-64bit-work/<t>/loads bash server/evidence/0802/stockload0802.sh   # one stock load, read, stop, restore
VP=5.5 HOST=laptop SHIP=... OUT=... bash server/evidence/0790/lap0790.sh                                     # 0790's flash check on tb1-55
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed ... --env TESTBED_SAVES=C:/c5-testbed/base/saves55 -- \
  "%TESTBED_PYTHON%" tools/hi/realinput.py job --ticket <t> --instance tb1-55 --load HI-0766-BASIC55-T0 --idle 900   # an HI journey's laptop job on 5.5
python -I tools/testbed/commission.py --host <H> set-dll tb2-55 --dll <root>/base/vp-release/dlls/vp55-ia-m43/CvGameCore_Expansion2.dll   # 43 civs on a copy (seals again)
```
5.5 saves on the hosts (`base/saves55`): REL55-TINY_0008, VP55-HUGE_0350-B, HI-0766-BASIC55-T0, HI-0766-BASIC55-M43-T0,
HI-4506-LARGE-SCENE-55, HI-0580-LARGE-M43-SCENE-55; the 43-civ -ia DLL is `<root>/base/vp-release/dlls/vp55-ia-m43`.
Making a copy: [tools/testbed/COMMISSIONING.md](../../tools/testbed/COMMISSIONING.md) section 6 (about 2.5 min a copy) ·
TRAP: two 5.5 jobs at once must name different copies (the runner's slot mapping knows only the 5.4.8 instances) ·
TRAP: each copy's first game raised Windows' firewall alert for its new EXE path (2026-11-26), which stays over the game
until the owner answers it: grabs record it and the real-input lane refuses points under it; check a first grab before a
picture round or a journey · stock load about 4-5 min a job · [quiet-testbed.md](quiet-testbed.md) 34.
A live game on oldpc with one mod Lua file swapped for a candidate (backup and restore by SHA-256 inside the job), and a
scripted Lua-channel dump of an interface panel's controls in both forms (0750, 2026-11-25, `[L]`): `perf/evidence/0750/README.md`
(`tp0750_job.py --form eager|deferred` submitted `--headed` with `--file <checkout>/tools/harness/native/bin=native` (that folder is created by `tools/harness/native/build.bat`, section 0), the
vp-dll-dev assets as `frontend-assets` and the save as `saves/<NAME>.Civ5Save`; `live.py ready`; `drive0750.py`; `live.py stop`;
`compare0750.py`) · in game about 2 min on M, the 20 steps about 15 min (a `lua` call is 3-5 s through the transport) ·
traps: the channel runs on the game-core thread, so interface actions go through a main-thread `LocalMachineAppUpdate`
job; `lua.py` prints a string result in Lua's `%q` form; `rawget`/`rawequal` are nil in a context's environment
([ui-lua-lock-holds.md](../03-dll-engine-boundary/ui-lua-lock-holds.md) 7.6.5).

An import-slot probe in a live oldpc game, per-frame counts and an in-process A/B with every frame logged, uncapped and
with `WaitForVSync = 0` for the job only (5041, 2026-11-25, `[L]`; a pattern for any EXE import worth counting per frame):
```
cmd //c "$(cygpath -w "$PWD/perf/evidence/5041/build.bat")" 'C:\c5-64bit-work\<t>\probe'     # through quietlock run; seconds
python -I -X utf8 tools/testbed/tb.py --host oldpc submit --headed [--quiet --expect-min 20] --name "<t> ..." --timeout 3600 \
  --file <checkout>/tools/harness/native/bin=native --file <vp-dll-dev skill>/assets=frontend-assets \
  --file C:/c5-64bit-work/<t>/probe/anchor_probe.dll=a5041/anchor_probe.dll \
  --file F:/c5-64bit-work/0438/saves/TS0438-H-T300.Civ5Save=saves/TS0438-H-T300.Civ5Save -- \
  "%TESTBED_PYTHON%" perf/evidence/5041/anchor_5041.py job --load TS0438-H-T300 --idle 900 [--vsync-off] [--script perf/evidence/5041/script_quiet.json]
python -I -X utf8 tools/testbed/live.py --host oldpc send <JOB> '{"op": "a5041", "args": ["--site", "122B8B", "--cls", "9C 40 2 38", "--fps", "0", "--ab-reps", "2"], "timeout": 900}'
python -I -X utf8 tools/testbed/tb.py --host oldpc fetch <JOB> --out C:/c5-64bit-work/<t>/oldpc-<JOB> --include "out/live/*"
python -I perf/evidence/5041/report_5041.py C:/c5-64bit-work/<t>/oldpc-<JOB>/out/live
```
In game about 2.7 min on the H save; the quiet script about 12 min in all. `--fps 0` (the harness cap's `set_fps`, put
back after) alone is not uncapped on oldpc: `WaitForVSync = 1` holds a window on screen at 60 Hz (smoke job
20261125-111247-bd64, every frame 16.667 ms); `--vsync-off` on the job sets 0 and restores the file by SHA-256 before
live_session's restore. `bg_helper32 inject` of the same DLL path a second time in one game only counts up (the slots
stay hooked once). TRAP: a probe that writes no "switch" mark lets a window's label run into the next block's settle;
the report filters such frames by the frame's own state · [camera-and-picking.md](../02-exe-internals/camera-and-picking.md) 12.3.

Real input (SendInput) on the laptop: before planning a series, read the lock state in a live session; `lock_ui: true`
(a lock window of `LockApp.exe`/`LogonUI.exe` in the session) refuses every real input even when a grab shows the game (5042, 2026-11-24 22:53 `[L]`;
`camera-and-picking.md` 11.3a); a resident `LockApp.exe` without a window reads `lock_ui: false` and the lane runs (0732; 5042's
series 2026-11-25 07:19, job 20261125-071644-7c6c `[L]`). 5042's wheel series: the submit is in `perf/evidence/5042/wheel_5042.py`'s
docstring (without `--script`, `--idle 900`), then the ops `w5042` with `"args": ["--lane", "real"]` and `["--lane", "posted"]` (about
200 s each), `stop`; table: `python -I perf/evidence/5042/report_5042.py <fetch>/out/live/w5042-real/wheel_5042.json --probe-tsv
<fetch>/out/live/w5042-probe/wheel_probe.tsv` (the taken deltas per arm). The session below is realinput.py's job (any driver built on `RI.job_main`, e.g.
`perf/evidence/5042/wheel_5042.py job`), which serves the op `realinput`:
```
python -I -X utf8 tools/testbed/live.py --host laptop ready <JOB>
python -I -X utf8 tools/testbed/live.py --host laptop --out C:/c5-64bit-work/<t>/live-<JOB> shot <JOB> --method grab --scale 0.5 --what "<probe frame>" --result info --issue <t>
python -I -X utf8 tools/testbed/live.py --host laptop send <JOB> '{"op": "realinput", "args": ["state"], "timeout": 60}'   # read-only: lock_ui, input_desktop, game_foreground
python -I -X utf8 tools/testbed/live.py --host laptop stop <JOB>
```

### 3.13 I want 4779's live fight sessions (route S cases on a Tiny or huge save), two instances at once
ONE work root per instance: `run_4779.py` keeps the armed options, the server.cfg extra and the pictures in
`C5_4779_ROOT`, so two games under one root read each other's options (0042 W44, 2026-11-21 22:13: a 26577625 game
took an old-build session's cases and its Miles logger). The driver now records instance + EXE build in the root at
first use and refuses another (`LOUD: 4779 root guard`, exit 2), and injects the Miles logger only into build 4390913
(`LOUD: 4779 miles guard`). `--miles` only on an old-build instance; `--evidence` writes the unit dump and timeline to
`C5_4779_BIG` (put it on C: while any game runs from F:).
```
export C5_POOL_OWNER=<addr> C5_4779_WHO=<addr>
python -I tools/harness/pool.py claim --owner <addr> --name <inst> --hours 2 --ticket <t> --address <addr>
C5_4779_ROOT=C:/c5-64bit-work/<t>/s4779-<inst> C5_4779_BIG=C:/c5-64bit-work/<t>/big INSTANCE=<inst> sh server/evidence/0042/4779/chain_4779.sh <S1..S16> <layout> <tag> [--miles <folder with mss_log.dll>] [--evidence]
python -I server/evidence/0042/4779/check_4779.py --out <root>/live-<S>-<tag>/out-<arm>-<S>-<tag> --pics <root>/pics/<arm>-<S>-<tag> --arm p [--ref <stock verdict-4779.json>]
python -I server/evidence/0042/4779/sounds_4779.py --install <inst install> --side route <mss_log.tsv> <verdict> <probe.json> --side stock ...   # one-shots per kind
python -I tools/instances/provision.py verify <inst> --full      # after anything went wrong, before the next use
```
`run_4779.py plan` lists the sessions · Tiny session 10-15 min, launch checks about 2 min · chain exit 0 ok, 2 prep,
4 run, 7 mode, 8 a box (left for its owner), 9 a grant check · 0042 W44 (2026-11-21) `[L]` ·
[unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 15.5, 15.12.
Reform cases (W55, 2026-11-22): `run_4779.py` session S17 = melee, rf_gwi, rf_crossbow, rf_cannon, rf_lancer, ranged on
c5h-b; the rf_ cases turn a unit by `Unit:RotateFacingDirectionClockwise` (a Lua-made unit reads facing 7 = no heading =
no reform); `C5_4779_ISSUE=<t>` sets the pictures ticket; driver `server/evidence/0042/4745/w55/run_s17.sh [SID TAG]`,
`EXTRA=--evidence` for the per-figure timeline · 6-7 min per launch on c5h-b · `[L]` · unit-combat-server.md 18.19.
City melee cases (W63, 2026-11-22, `[L]`): `run_4779.py` session S19 = melee, cy_lancer, cy_gwi, cy_sword, cy_knight,
cy_rifle, cy_lancer_b, cy_gwi_b, wh_sword_warrior on c5h-b; a cy_ case attacks the nearest city-state capital at war and
keeps it from capture by the DLL prediction (defence buildings, then attacker damage); driver
`sh server/evidence/0042/4748/w63/run_s19.sh [SID TAG]` (`C5_4779_ISSUE=4748`, `--miles` a copied mss_log.dll folder);
collect `python -I -X utf8 server/evidence/0042/4748/w63/collect_w63.py <root> <table json> <sounds json>` (redacts user
and instance user-folder paths) · about 8 min per launch, build 4 min · unit-combat-server.md 18.21.
Knob arms of a 4779 session (W70, 2026-11-22, `[L]`): `C5_4779_EXTRA_CFG="key=1;key2=1"` puts TEST keys into server.cfg
(the proxy LOUD-logs each); with `EXTRA=--evidence` also add `render_test_units_dump_max=30000` (the default 4000 changed
frames does not cover 7 cases). Driver `sh server/evidence/0042/5050/w70/run_s22.sh S22|S23 TAG` (`KNOBS=1`,
`EXTRA=--evidence`). After each launch move `<msslog>/mss_log.tsv` into its own folder before the next launch (no START
split needed): `sh server/evidence/0042/5050/w70/analyse_w70.sh SID ARM TAG`. Live phase times of route S:
`python -I -X utf8 server/evidence/0042/5050/w70/timeline_w70.py <big>/anim-<S>-<tag>/server.c5anim --json OUT`; the unit
dump's scene time does not advance live; the timeline's frame lines map a dump frame id (field 5) to its wall time
(field 4): `dumpseq_w70.py --anim`. `pool.py release` takes the name positionally:
`python -I tools/harness/pool.py release --owner <addr> <inst>` · unit-combat-server.md 18.23.1.

4779 sessions on a test machine (W60b, 2026-11-22, `[L]`): `sh server/evidence/0042/4742/w60b/submit_tb4779.sh dev|s18|pair [TAG]` (`LAYOUT=<layout with SHA256.txt>`, `MSSLOG=<folder with mss_log.dll>`; build 4390913 only, never the Miles logger on 26577625); the job is `tbjob_4779.py --session S13|S18 [--arms p,u0] [--cases a,b] --tag T --miles --check-exe-stamp` (`--dry-run` first); fetch with `tb.py fetch JOB --out DIR` (`--include` does not filter: 1.2 GB with bursts); table: `python -I server/evidence/0042/4742/w60b/table_4779.py <out>/s4779/live/out-<arm>-<tag> <out>/s4779/pics/<arm>-<tag>`; resid/tier from the last units trigger 1s line (no totals line after a harness stop). Dev job 15 min, S18 10 min, the pair 12 min. `run_4779.py` itself cannot run on a test machine (pool claims, c5h prep).
Per-case counts of a 4779 session (W79, 2026-11-23, `[L]`; driver `sh server/evidence/0042/charge_any_w79/run_s26.sh SID TAG`,
`CHARGE=1` puts `render_test_units_charge_any=1` into server.cfg, `EXTRA=--evidence` adds the dump bound): every fight with
both units' damage before -> after, AI turns included, from the Lua listener (run.log's readlog lines are cut at ~300
characters; probe.json's `lualog` is whole): `python -I server/evidence/0042/charge_any_w79/fights_w79.py <root>/pics/<arm>-<S>-<tag>`;
one-shot stems and the run-charge vox (`runchrgvox*`) per case window, also when split_w64 finds one position group:
`python -I -X utf8 server/evidence/0042/charge_any_w79/stems_w79.py --install <inst install> <mss_log.tsv> <out>/verdict-4779.json <pics>/probe.json`;
route S's 1120 / 1140 / 1100 requests per case (`charge`, `attack_charge`, `attack` in the timeline):
`python -I -X utf8 server/evidence/0042/charge_any_w79/timeline_w79.py <big>/anim-<S>-<tag>/server.c5anim <pics>/probe.json`.
The same save and case order give the same DLL dice in every arm (S27-S30): put the cases you compare in the same order.
Edit a running driver script only by a new file: `sh` reads it while it runs (W79's edit broke the script's last lines).
The S18 product/stock pair with Miles on oldpc (4747 W87, 2026-11-23, `[L]`, 18 min plus a 2.4 GB fetch of about 13 min):
`VPDEV_ASSETS=<vp-dll-dev assets folder> sh server/evidence/0042/4747/w87/submit_w87.sh dry|s18pair [TAG]` (LAYOUT, MSSLOG; wraps
`tbjob_4779.py --session S18 --arms p,u0 --miles --check-exe-stamp`). The arm folders are `out/s4779/live/out-<arm>-<arm>-<tag>` and
`out/s4779/pics/<arm>-<arm>-<tag>` (the arm twice). After the fetch: `sh server/evidence/0042/4747/w87/analyse_w87.sh <fetched job
folder> <tag>` (per-arm tables, the first shot heard, per-kind p/u0). Trap: `tb.py fetch --include` re-downloads the whole job; a
plain fetch leaves only the two meter.tsv on the host. Trap: the job record and run.json carry the testbed instance's user-folder
paths, which the privacy grep flags: redact them before committing.
Stock combat simulations with figure positions every 33 ms, the sampler beside a 4779 launch (5060 W91, 2026-11-23, `[L]`, build
4390913 only): `python -I -X utf8 server/evidence/0042/5060/w91/simphase91.py --instance INST --out F:/c5-64bit-work/T/sim.jsonl --seconds
2700 --wait-pid 300` (waits for the harness game process and ends when it exits). One prep, two arm u0 launches with the sampler, one
restore: `sh server/evidence/0042/5060/w91/run_w91.sh TAG` (claim first; LAST_START=HHMM bounds the second launch). Offsets per type
and kind: `stock_offset_w91.py --sim S1 --sim S2 --cases a,b,... --mounted c,d --json OUT --fights OUT.csv [--no-pos]`; route S
against it: `table_w91.py --stock OUT --route-frames <w90 route-frames.csv> --route-index <w90 runs/main/index.json> --db DB_COPY`;
phase times: `phases52.py --csv` per launch, then `phases_w91.py --csv P1 --cases ... --csv P2 --cases ... --mounted ...`. A 4779
arm u0 Tiny launch with 12 melee cases takes 9 min.
Melee hit reactions per unit from a named Miles log (5055 W97, 2026-11-23, `[L]`; each impact paired with the death vox at the
same position within 0.1 s, plus the beds): `python -I -X utf8 server/evidence/0042/5055/w97/scripts/melee_w97.py
<miles>/mss_log.named.tsv [--cases a,b] [--json OUT]`; `server/evidence/0042/5055/w97/scripts/analyse_w97.sh SID ARM TAG` runs
table, split, stems and it after each launch. Trap: `pool.py claim` refuses a done ticket: claim a follow-up live check under the
epic id (`--ticket 0042`).
Builds of main for a live same-dice pair, with a quietlock-refusal retry (5059 (c), 2026-11-25, `[L]`, about 9 min):
`sh server/evidence/0042/5059/w147/build_w147.sh h s sh l` (harness, server 1.2, shell 1.5, layout). A 4743 same-dice triple
with one server.cfg knob arm: `sh server/evidence/0042/5059/w147/run_w147.sh TAG "u p v0"` (waits for the quiet lock before each
launch, retries a refused launch under a new tag; about 11 min an arm), then `python -I server/evidence/0042/5059/w147/vox_w147.py
A=split.json B=split.json --want-equal B` (exit 1 when victory vox differ). Trap: under load `pool.py list` takes about 55 s.
Relink after a change to `render/include/c5r/unitanim/combat.h` (a CombatMotion member; 0743, 2026-11-25, `[L]`): from the
worktree, `C5_ALLB_OWNER=<addr> C5_0743_ALLB=<own folder> bash server/evidence/0042/0743/w140/allb140.sh 1`, then `... 2` (phase 1
builds the render tree with `C5_0743_RENDER_BUILD=1`, 3-10 min; phase 2 15-25 min; logs in `<folder>/allb.log`); rule A is phase 3
(about 41 min) and runs only in MC-B's rule A turn. A live stock-vs-product frame strip of a fight on a c5h instance: claim
through `pool.py claim`, build a layout with `server/evidence/0042/0743/w140/mklayout_w140.sh [SRC layout] [server bin] [OUT]`, then
`sh server/evidence/0042/0743/w140/run_strip_w140.sh <tag> [u p]` (about 11 min an arm, restore by hash in the chain); reader
`python -I server/evidence/0042/0743/w140/strip_w140.py --pics <root>/pics/<arm>-<SID>-<tag> --label <case>_burst --json J --montage M`,
table `python -I server/evidence/0042/0743/w140/table_w140.py <dir>`; pictures DIAGNOSTIC outside git. Trap: one projection per case
measures the camera pan, not the figures. Trap: a CMake render build tree names its build folder and its SOURCE worktree in
CMakeCache, so a tree copied from another folder, or a build folder reused from another worktree, refuses to configure ('is
different', 'does not match the source'): delete the render trees (render, srv/render, ra/rt/c5render) and build fresh.
The 5061 defender strip (5061, 2026-11-25, `[L]`): `STRIP_OUT=<own folder> STRIP_WHO=<address> sh
server/evidence/0042/5061/w143/run_strip_w143.sh <tag> [u p p0]` (p0 = the product with `render_units_defender_step=0`); layout
and claim as the 0743 strip above; about 12 min an arm. Which library bundles hold fights (5061, 2026-11-25, `[L]`, seconds):
`python -I server/evidence/0042/5061/scan_combats.py <library>/bundles` (counts the feed.bin COMBATS records per bundle; the
scene.json manifests name no fights).
The same figures across two heads (0042, W122/W166, `[L]`): run each trigger_scene runner into tag-named work folders on both
heads, then `python -I server/evidence/0042/5067/w122/scripts/cmp_logs.py DIR_A DIR_B` (prints `RESULT same n/m`). Under load the
scene-bundle `sceneshot_test` needs a long timeout: `build_scenebundle_tests.py --out DIR --c5render RB --run --only sceneshot_test`
now gives it 7200 s by default; add `--timeout-scale 2` beside heavy jobs (3600 s was not enough beside 9 heavy jobs on
2026-11-25; a timeout is NOT A RESULT, reported as `TIMEOUT`/`NO VERDICT`, exit 4; `run_all.py` forwards `--sb-timeout` and
`--sb-timeout-scale` since ticket 0800, 2.3; replaced "`--timeout 7200`; run_all forwards no `--timeout`"). Trap: a `build_trigger_scene.py --out` folder reused after its source worktree was removed and re-added
fails with only `FAILED: render/build.py --target c5render` (the CMake cache names the old source folder): give a new main
reference worktree a fresh `--out`. Lines from lead-0042 (#16774), 2026-11-26.
Live 4779 runs on an old-build instance with `--miles`: the work folder's `msslog/` must hold 4797's `mss_log.dll` (copy it
from a previous worker's `msslog/`, local only, never committed); without it the arm stops at its options step, before any
launch. The 4779 chain refuses an F: instance while F: latency is over 50 ms (rc 9, before any launch): on a busy F: ask MC-B
for a C: instance. `trigger_scene` does not create its `--out` folder: `mkdir -p` it first, or it exits 5 at the trigger log
(`triggers.log: Io: cannot create`). `trigger_scene --clip-times` prints every animation of both sides' members with the
length the core reads (`clips:`). Lines from lead-0042 (#17363, 5074's W174), 2026-11-26.

### 3.14 I want a live land/water check with the land/water step OFF (`C5_RENDER_LIVE_LANDWATER=0`)
```
C5_RENDER_LIVE_LANDWATER=0 python -I render/evidence/0615/4912/scenes_4912.py reload -i <inst> --save <save> --log C:/c5-64bit-work/<t>/reload.log [--no-stop]
python -I server/evidence/0615/4914/wait_producer.py --instance <inst> --since <epoch before the reload> --timeout 180 --out C:/c5-64bit-work/<t>/producer.jsonl   # exit 1 = made without the step: expected here
python -I tools/scenario/mods/liveterrain/lt.py -i <inst> --out C:/c5-64bit-work/<t>/setup.json setup
python -I tools/scenario/mods/liveterrain/lt.py -i <inst> --out C:/c5-64bit-work/<t>/lw.json apply lw      # then wl, coast_land, 20 s apart
python -I tools/harness/frontend.py -i <inst> ingame                                                       # exit 8 = a box (3.8)
```
The variable reaches server.exe through the harness launch (server.log: "land/water changes wait for the reload
(C5_RENDER_LIVE_LANDWATER=0)"); expect "map seed not read (C5_RENDER_LIVE_LANDWATER=0 ...)", each change "wait for
reload: Water", no Ice_Reload · a Tiny load about 3 min, the changes 1 min · the instance's
`Logs/VP64/run/<folder>/server.log` · 0615 W9, 2026-11-21 (c5h-b, t13) · `[L]` 2026-11-21 ·
[live-terrain.md](../05-graphics-composition/live-terrain.md) 11.4 ("The c5h-b smokes"), 8.17's switch-off.

A look check of a live terrain change at a close camera (zoom 11.0) is also made after a zoom far out and back in: a
near pair the old rule could not re-bake kept its old colours (0667); `server/evidence/0667/diag_job_0667.py` does both
in one oldpc job. Ordinary changes for a look check (road, farm, forest chop, pillage) by Lua through the harness channel:
`server/evidence/0667/ordinary_job_0667.py` (every id checked before the call, the game never saved) · 0667
(2026-11-22, oldpc) · `[L]` · [live-terrain.md](../05-graphics-composition/live-terrain.md) 11.11.

### 3.15 I want a save on an instance only for one run (put in, then taken out by hash)
```
python -I server/evidence/0615/4914/saves_snap.py snap    --instance <inst> --work C:/c5-64bit-work/<t>/pp
python -I server/evidence/0615/4914/saves_snap.py put     --instance <inst> --work C:/c5-64bit-work/<t>/pp --file <local>.Civ5Save --as <C5H-name> --sha256 <hex>
python -I server/evidence/0615/4914/saves_snap.py restore --instance <inst> --work C:/c5-64bit-work/<t>/pp     # after the run's end
```
`put` never overwrites; `restore` moves every save the run made (and the put copy) to `<work>/saves_moved/` and puts
back by hash any save the run overwrote (the loads overwrite `AutoSave_*`) · seconds · 0615 W8/W9, 2026-11-21 ·
`[L]` 2026-11-21 · [live-terrain.md](../05-graphics-composition/live-terrain.md) 11.2.

### 3.16 I want to prove an instance is back as I claimed it, before I release it
```
python -I render/evidence/0615/4915/restore_check.py C:/c5-64bit-work/<t>/pp/std-backup/manifest.json [C:/c5-64bit-work/<t>/stock/backup/<inst>/manifest.json]
python -I tools/instances/provision.py verify <inst>        # rc 0
```
The first manifest is the backup of the session's first prep (`run_3308.py prep`; `runner.py prepare` writes the
second): every file must be hash-equal now, or absent if it was absent then; exit 1 lists the differences · seconds ·
0615 W9, 2026-11-21 (c5h-a, c5h-b) · `[L]` 2026-11-21 · [live-terrain.md](../05-graphics-composition/live-terrain.md) 11.4 "Release".

### 3.17 I want the logs of every process of a long session (the harness keeps only the newest 10 run folders)
`cp -r "<user dir>/Logs/VP64/run/<folder>" C:/c5-64bit-work/<t>/runs/` after each process ends (a folder still in use
cannot be copied whole) · seconds · a 4-hour I3 session lost its first reload processes' logs this way (0615 W9,
2026-11-21) · `[L]` · [live-terrain.md](../05-graphics-composition/live-terrain.md) 11.4.

### 3.18 I want the farm buildings off (bisecting a report) or the farm area event timed
In `[composite]` of `shell_composite.ini` beside the proxy (the installer lays one), right under the `[composite]` line,
or as the environment variable of the EXE:
```
farm_end_post=0        # VPSHELL_FARM_END_POST=0: no farm-end post, no farm building with ServerFarmFields = 1
farm_area_probe=1      # VPSHELL_FARM_AREA_PROBE=1: DIAGNOSTIC, the builder object's area listener timed per call
```
Read by the proxy every second; vpproxy.log `farm_guard: proxy keys: ... farm_end_post=0 (ini) ...` and per load
`farm-end posts N (...)`, `area probe: N calls ...` (every value in `farm-area-calls.log` in the run folder) · 0655
(2026-11-21) · `[L]` · [farm-builder-guard.md](../02-exe-internals/farm-builder-guard.md) 13.3, 13.4.
The farm manager's pending list (4390913 only, read only): `python -I shell/evidence/0655/farm_pending.py <game pid>` · `[L]`.
Where the EXE put each farm building against its field curves (4390913 only, read only; exit 0 when every record sits on
the placer's rule): `python -I shell/evidence/0657/farm_records.py <game pid> [--rows N]` · about 3 s on the huge save ·
0657 (2026-11-22, a stock load on c5h-b) · `[L]` · [cities-and-landmarks.md](../05-graphics-composition/cities-and-landmarks.md) 2.6.2.
A headless Ghidra read of build 4390913 (decompile, references, a listing), read only, on your own copy of the project:
`cp -r "$USERPROFILE/tools/ghidra-projects/civ5/." C:/c5-64bit-work/<t>/ghidra/` (249 MB, seconds), a `Dec.java` post
script (copy one from a scratch folder that has it, e.g. `C:/c5-64bit-work/9078/scripts/`; items `d:ADDR`, `r:ADDR`,
`x:ADDR:N`, `o:HEX` without `0x`), then `JAVA_HOME="$USERPROFILE/tools/temurin-jdk-21.0.13+9" cmd //c
"%USERPROFILE%\tools\ghidra-12.1.4\support\analyzeHeadless.bat" "C:\c5-64bit-work\<t>\ghidra" civ5 -process
CivilizationV_DX11.exe -readOnly -noanalysis -scriptPath "C:\c5-64bit-work\<t>\scripts" -postScript Dec.java <out.txt>
<items...>` · about 25 s for ten functions · output never committed (D83) · 0657 (2026-11-22) `[L]` ·
[reverse-engineering-setup.md](reverse-engineering-setup.md) 2.
The same read of build 26577625 (there is no analysed project of it): import a scratch copy of the golden EXE into a new
local project WITHOUT analysis, then decompile single functions (Dec.java's `d:` creates the function at the address):
`cp F:/c5-64bit-work/golden/civ5-steam-build26577625-ws/CivilizationV_DX11.exe C:/c5-64bit-work/<t>/g26/bin/`, then
`cmd //c "%USERPROFILE%\tools\ghidra-12.1.4\support\analyzeHeadless.bat" "C:\c5-64bit-work\<t>\g26\proj" civ5n -import
"C:\c5-64bit-work\<t>\g26\bin\CivilizationV_DX11.exe" -noanalysis` (JAVA_HOME as above; about 1 min, through
`quietlock.py run`), then the `-process CivilizationV_DX11.exe -noanalysis -scriptPath ... -postScript Dec.java <out> d:<VA>`
line on `civ5n` (about 30 s). The VA comes from `tools/exebuild/addr_match.py` (`match_code`) and stays local (D202). Without
analysis the decompile has no callee names and some `unaff_` registers, but offsets and constants read as usual. Scripts
of 0727: `C:/c5-64bit-work/0727/g26/import.sh`, `dec.sh` (scratch) · 0727 (2026-11-25) `[L]` ·
[observer-switch-minimap.md](../05-graphics-composition/observer-switch-minimap.md) 6.6.
Whether a function of build 4390913 is byte-equal in build 26577625 (an addr_match carry is only a 16-160 byte match): see
`proxy/tests/minimap_fog/layout_check.py` (whole-function masked compare and the relocated operands; prints verdicts only) ·
0727 (2026-11-25) `[L]`.

---------------------------------------------------------------------------------------------------------------------

### 3.19 I want the release's installer-laid layout at a chosen graphics setting, with a composite swapped in by hash
```
export ROOT0804=C:/Users/<you>/c5-instances                                # skips the slow pool list in every phase
bash shell/evidence/0804/drv_0804.sh <inst> install                         # the release installer (EXE0804= to change), player DLL set first
LEADERAA=1 bash shell/evidence/0804/drv_0804.sh <inst> msaa 8               # GraphicsSettingsDX11.ini backed up by SHA-256 once, then MSAASamples (+ AllowLeaderAA)
bash shell/evidence/0804/drv_0804.sh <inst> composite <dll>                 # composite + its manifest rewritten (proxy/tools/shell_manifest.py write), both by hash
PLAY=1 bash shell/evidence/0804/arm_0804.sh <inst> <tag> <save> <seconds>   # launch, trace + .shot triggers, pans/zooms, safe stop, collect, summary
bash shell/evidence/0804/accept_0804.sh <inst> <save> <seconds> 8:1 8:0 4:1 2:1 1:1   # a series of arms, "samples:AllowLeaderAA"
bash shell/evidence/0804/drv_0804.sh <inst> compositeback|msaarestore|remove # each by hash; remove = the installer's removal + tree diff + verify
```
An arm takes about 7 min plus its play. TRAPS: the harness refuses `FullScreen = 1` (`launch.check_launchable`), so
exclusive full screen cannot be armed on any machine; `cli.py -i <inst> wheel X Y -- -1` is refused ("ticks required"):
zoom out with `cli.py -i <inst> wheel X Y -1` and check each step's exit code; a graphics report needs the player's whole
`GraphicsSettingsDX11.ini` (MSAA reproduced only with `AllowLeaderAA = 1` as well). `research/02-exe-internals/evidence/4967/lay.py` does not
lay the modpack form (it writes the helper into `MODS/(1) Community Patch`); use a mods-form instance for it ·
0804 impl-0804 (2026-11-26, c5h-55-c, EXE 26577625 and 4390913) · `[L]` ·
[shared-texture-and-compositor.md](../05-graphics-composition/shared-texture-and-compositor.md) 16.

## 4. Capture

Rules: an ACCEPTANCE picture only from the full shipped system, everything else labelled `DIAGNOSTIC:` (D112); close
EXE popups before a world-renderer picture (D114); name the asset set (D94); never commit pictures (COMMON 27).
[visual-checks.md](visual-checks.md).

### 4.1 I want a validation screenshot in Visual Checks
From a harness game: `cli.py shot` (3.6) records it itself (`--no-record` only for a picture that validates nothing).
A picture made another way (RenderDoc, sceneshot, a crop):
```
python -I tools/visualcheck/visualcheck.py record PICTURE.png --what "DIAGNOSTIC: ..." --result info|pass|fail --issue <t> --instance <inst> [--reference STOCK.png --ref-how same|approximate] [--comparison DIFF.png]
python -I tools/visualcheck/visualcheck.py amend <vc-id> --what "..." --result pass       # or --image C:/x.png, --source C:/... (not /c/)
# TRAP (4985, 2026-11-22): `amend --source <folder>` rewrites EVERY entry recorded from that folder; amend one entry by its id
python -I tools/visualcheck/visualcheck.py list --issue <t> --result undescribed          # before the hand-back: must print nothing
python -I tools/visualcheck/visualcheck.py pull --host oldpc                              # test machines' pictures
```
An acceptance pair: `--what "ACCEPTANCE: <build>, <settings>: ..."` with the stock picture of the same save and camera
as `--reference ... --ref-how same` · about 0.3 s · the store `D:/c5-64bit-work/visual-checks/` (allowed on D:) ·
many pictures: copy the `rec()` function of `server/evidence/0455/4980/s09_vc.sh` (4980, 2026-11-20) · `[L]` help,
`where`, `list` · [tools/visualcheck/README.md](../../tools/visualcheck/README.md).

### 4.2 I want the scene player (sceneshot) and a replay of a bundle
```
python -I perf/quietlock/quietlock.py run --who <t> --what "sceneshot build" -- python -I server/tools/sceneshot/build_sceneshot.py --out C:/c5-64bit-work/<t>/ss [--asan] [--smoke]
C:/c5-64bit-work/<t>/ss/bin/sceneshot.exe --bundle C:/c5-64bit-work/scene-library/bundles/<id> --dumped --compare --tolerance 1:1 --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt --out C:/c5-64bit-work/<t>/shots
```
Build time NOT VERIFIED; a Tiny replay 7-12 s for 8 shots · `<out>/<frame>.png`, `sceneshot.json`; exit codes in the
README · lead-0042, 2026-11-21 · `[L]` build help, player `[C]` ·
[server/tools/sceneshot/README.md](../../server/tools/sceneshot/README.md), [offline-scene-harness.md](offline-scene-harness.md) 16, 34.
A frozen asset root (the source instance must be free): `python -I server/tools/sceneshot/assetroot.py make --from <inst> --out C:/c5-64bit-work/<t>/asset-roots/<inst>-<date>`,
then `verify <root>`; the shared one is `C:/c5-64bit-work/scene-asset-root/mods548-vt` (`C5_SCENE_ASSET_ROOT`).
Pixels with equal depth but different colour between an off and an on run are the shadow cast by added figures
(4983, 2026-11-22, `[L]`): `python -I server/evidence/0455/4983/shadowcheck.py <off> <on> <camera>`.
Per-frame split of the units module (5056, 2026-11-23, `[L]`, diagnostic, never merged): `git apply server/evidence/0042/5056/diag5056-v2.diff`,
build sceneshot, run with `C5_DIAG_5056=1` (one diag5056 line per drawScene on stderr), then
`python -I server/evidence/0042/5056/diag_stats.py ERR.log --skip 20`. A/B: `bash server/evidence/0042/5056/bench5056.sh BASE NEW OUT 6`,
then `bench_stats.py OUT` (a HUGE_0350 run takes 75 s).
A bundle frame that has no named camera (4983, 2026-11-23, `[L]`): `sceneshot --frame <id>`; example driver
`server/evidence/0455/4983/frames_4983.sh`.

### 4.2a I want offline road-layout preview sheets (the road generator in a bundle's decal member, then sceneshot)
```
python render/build.py --config Release --target c5r_roadgen --build-dir C:/c5-64bit-work/<t>/render-build
sh render/evidence/0454/4940/shots.sh --part 1|1b [--record]     # sheets to <work>/previews or <work>/previews1b
c5r_roadgen --plots P.bin --style line --fillet 22 --merge 22 --connect all --junction cross --jitter N   # the generator alone; N 0..7
python -I render/evidence/0454/4940/linesdiagram.py OUT.png X0,Y0,X1,Y1 LABEL=LINES.bin [LABEL=LINES.bin ...]
```
`shots.sh` runs `previews.py` per scene (dense, tiny, huge) through `quietlock.py run`: plots from the bundle
(`render/evidence/0454/4933/plotsbin.py`, `tinyplots.py`, `render/evidence/0454/4940/denseplots.py`), the working copy and
cameras (`4933/bundlecams.py prep`, `aim`, `use`), `c5r_roadgen ... --dtap stock.dtap --out V.dtap` per variant, one
sceneshot per case and variant, a 3 x 2 sheet per case; `--record` puts each sheet into Visual Checks. Edit the paths at
its top (work `F:/c5-64bit-work/4940`, `bin/` holds copies of `c5r_roadgen.exe` and a sceneshot build, `--install` a
Goldberg copy's install, `--mods` a MODS folder) · needs PIL and the scene library on F: · a huge shot about 2.3 min, a
Tiny one about 40 s; the roadgen step under 1 s · sheets in `<work>/previews/`, a log per scene `<work>/<scene>/previews.log` ·
4940, 2026-11-22 · `[L]` · stops before 01:38-04:00 and on `<work>/STOP`, resumes with `--resume` ·
[docs/extras/roads.md](../../docs/extras/roads.md) 4e, [extras.md](../05-graphics-composition/extras.md) X3.10.
Part 2's sheets (4940, 2026-11-22, `[L]`, 24 min): `sh render/evidence/0454/4940/shots.sh --part 2 [--record]` makes 3x3 sheets
with a legend in F:/c5-64bit-work/4940/previews2 (reuses parts 1 and 1b's shots, 4 more per case; Huge limited to rc11, rc13,
rc14).

### 4.3 I want a checked working copy of the scene library on C:
```
python -I -X utf8 tools/scenelib/scenelib.py sync
python -I -X utf8 tools/scenelib/scenelib.py check [--deep]
```
Rule A runs read the C: copy, never the F: master concurrently; whoever writes the master (a recording, a baseline)
runs `sync` afterwards · first copy 7.81 GB in 55 s, refresh 13 s; check 0.85 s · last line `scenelib: OK` ·
0611 (2026-11-20) · `[L]` help · [d-drive-load.md](d-drive-load.md) 12.3.

### 4.4 I want every bundle and camera with a report (scenerun), here or on oldpc
```
python -I perf/quietlock/quietlock.py run --who <t> --what "scenerun" -- python -I server/tools/scenerun/scenerun.py --library C:/c5-64bit-work/scene-library run --player C:/c5-64bit-work/<t>/ss/bin/sceneshot.exe --out C:/c5-64bit-work/<t>/run --issue <t> --no-baseline-write
python -I server/tools/scenerun/scenerun.py report <run dir>
```
`--library` goes BEFORE the subcommand; on the C: copy run `scenelib.py check` first and pass `--no-baseline-write`;
fidelity (`stock/`) only on the F: master. On oldpc: `scenerun.py copy --host oldpc ...` then a `tb.py submit` (the
README's form) · library pass 52-69 s without pictures · 0614, 0617 (2026-11-20) · `[L]` help ·
[server/tools/scenerun/README.md](../../server/tools/scenerun/README.md), [offline-scene-harness.md](offline-scene-harness.md) 17.
Trap (5056, 2026-11-23, `[L]`): scenerun without `--asset-root` fails a bundle (exit 9, Install) when its recorded install is a pool
instance claimed at that moment: rerun those bundles with `sceneshot --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt`.

### 4.5 I want to record a scene bundle from a live game
Set `C5_RECORDER_OWNER=<t>` first: the recorder overwrites `C5_POOL_OWNER`, `C5_JOB_OWNER`, `C5_QUIETLOCK_WHO` with
it and defaults to "3904". No `--help` (it crashes):
```
export C5_RECORDER_OWNER=<t>
python -I server/tests/render/scenebundle/recorder_live.py prep --save tiny --instance <inst> --work C:/c5-64bit-work/<t>/rec --bin <layout>/bin --proxy <build>/proxy/bin/CvGameCore_Expansion2.dll --composite <shell build> --bundle F:/c5-64bit-work/scene-library/bundles/<save>_<yyyymmdd-hhmmss>_<machine> --after 900 --frames 8 --dump all
python -I server/tests/render/scenebundle/recorder_live.py run --save tiny --instance <inst> --work C:/c5-64bit-work/<t>/rec --checker <test build>/recorder_test.exe
python -I server/tests/render/scenebundle/recorder_live.py restore --save tiny --instance <inst> --work C:/c5-64bit-work/<t>/rec
```
Then `scenerun.py index` and `scenelib.py sync` · one live cycle about 2 min; Tiny bundle 281 MB · 4980 via
`server/evidence/0455/4980/rec_kit.py` (2026-11-20) · `[C]` docstring · [offline-scene-harness.md](offline-scene-harness.md)
"Commands a worker uses" 1, sections 9, 15.

A bundle recorded with `ServerUnits = 1` (the kit recipe, `rec_kit.py`) is recorded with the units' shadows on (the
default; no `server_cfg_extra`) by a server of main from 4749 on (format 10: shadow.bin carries the unit-mask-dropped
flag); the replay prints `N with the unit mask dropped (form 2)` with N > 0 and is byte-equal at 0:0. Recipe:
`bash server/evidence/0042/4749/w66/rec66.sh` (kit cases), `check66.sh` (staging, notes, 0:0 replay, feed kinds, index,
swap dry run) · 8 kit bundles 2026-11-22, two instances about 26 min, checks 6 min · 4749 W66 `[L]` ·
[offline-scene-harness.md](offline-scene-harness.md) 41. (Corrected 2026-11-22 by 4749 W66; it said: "needs
`render_units_shadow=0` in the run's `server_cfg_extra` until shadow.bin carries the unit-mask-dropped flag (ticket
4749)", 0670.)
The render-events x64 DLL the kit recipe records with (feed.bin keeps kinds 1000 x35 / 1001 x2): a pinned build is kept
at `C:/c5-64bit-work/pinned/render-events-addd57b8b/` (fork x64-render-events addd57b8b, `--config fast`, no LTO; its
README names the hash, readers and remover). To build one again, without an LTO link:
```
git clone --depth 1 --branch x64-render-events --single-branch <the fork's remote URL (the origin of the local fork clone)> C:/c5-64bit-work/<t>/fork-re
python -I perf/quietlock/quietlock.py run --who <t> --what "x64 DLL fast, render events" -- python -I tools/x64-dll/build.py --src C:/c5-64bit-work/<t>/fork-re --unlocked --config fast --stubs C:/c5-64bit-work/keep/pins-5.5/deps-x64-release/stubs --out C:/c5-64bit-work/<t>/dll-on
```
`build-summary.json` says `"render_events": "on"` · compile 190 s, link 1.2 s, 214 s in all · `--config release` is
an LTO link (needs the link grant) · 4749 W66 2026-11-22 `[L]` · `server/evidence/0042/4749/w66/build66.sh` (the
server, readers and layout too).

### 4.6 I want to read the decal tap, the model tap, or the trigger log
Live, from a running game (decal tap and cut tap, read only; start right after the launch): 
`python -I shell/evidence/0655/taps_live.py . <game pid> 400 C:/c5-64bit-work/<t>/<label>.dtap` prints the EXE's own
farm-field and farm-track records of the newest decal image and the field / margin / object cells of the newest cut grid ·
the game's pid from `python -I tools/harness/cli.py -i <inst> status --json` · 0655 (2026-11-21) · `[L]` ·
[farm-builder-guard.md](../02-exe-internals/farm-builder-guard.md) 13.6.
No standing tool reads the taps; they are bundle members (`decals/load<NNNN>.dtap`, `modeltap/frame<NNNNNN>.mtap`) read
by evidence scripts: `python -I render/evidence/0454/4930/dtap.py FILE.dtap`,
`python -I render/evidence/0166/4106/dtap_ties.py FILE.dtap X,Y` · `[C]` · [terrain-decals.md](../05-graphics-composition/terrain-decals.md) 9-10,
[city-renderer.md](../05-graphics-composition/city-renderer.md) 10-11.
The trigger log is offline only (written by `trigger_scene.exe`; no live server key writes it):
```
python -I perf/quietlock/quietlock.py run --who <t> --what "trigger scene build" -- python -I server/tests/effects/build_trigger_scene.py --out C:/c5-64bit-work/<t>/ts
python -I perf/quietlock/quietlock.py run --who <t> --what "trigger scenes" -- python -I server/tests/effects/run_trigger_scenes.py --exe C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/ts-run [--effects] [--geometry]
```
`WORK/<case>/triggers.log`, `RESULT PASS|FAIL n/m` (19 cases) · 0042 (2026-11-21) · `[L]` help ·
[unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 7.5-7.6.
`run_trigger_scenes.py` also takes `--no-turn-first` (4735), `--no-reform-first` (4745; `run_charge_scenes.py` too), `--charge-any` / `--no-charge-any` (4743 / 4737),
`--no-tier-gate`. The turn before a ranged, siege or naval fight (4735) on the same `trigger_scene.exe`:
```
python -I perf/quietlock/quietlock.py run --who <t> --what "turn scenes" -- python -I server/tests/effects/run_turn_scenes.py --exe C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/turnw [--before-only] [--csv OUT.csv]
```
`RESULT PASS 9/9` with the core's rule; `--before-only` (the rule before 4735) `FAIL 1/9` by design (since 4745 both arms
run with `--no-reform-first`: the reform replaces the turn of a reforming unit) · about 20-40 s ·
4735 (W45, W47, 2026-11-21) · `[L]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 10.11.
The reform before a fight (4745 + 4747, `CombatParams::reform_first`) on the same `trigger_scene.exe`: regression cases
R1-R4 (R4 needs a `trigger_scene.exe` built without 4745 as `--exe-base`), and the fight lengths per kind on / off:
```
python -I perf/quietlock/quietlock.py run --who <t> --what "reform scenes" -- python -I server/tests/effects/run_reform_scenes.py --exe C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe [--exe-base <base trigger_scene.exe>] --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/reformw [--before-only --cases r1,r2a] [--csv OUT.csv]
python -I perf/quietlock/quietlock.py run --who <t> --what "reform lengths" -- python -I server/tests/effects/run_reform_scenes.py --lengths --exe C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/lenw [--seeds 1,2,3] [--turns 0,1,2,3] --csv OUT.csv
```
`RESULT PASS 8/8` with `--exe-base` (4/4 without); `--before-only --cases r1,r2a` `FAIL 0/2` by design · about 20 s; the
lengths 168 runs in about 4 min · `trigger_scene.exe --dump-core` writes the core's `steps.csv`, `moves.csv`, `hits.csv`
into `--out` · 4745 (W53, 2026-11-22) · `[C]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 18.17.
Route S's melee fight by phase offline (approach end, each side's first landing, last landing, end; 13 cases x seeds) and the fight length per arm (main, `--charge-any`, `--no-turn-first`), then a live series' melee fights grouped by stock's fight type (W44's 082e arms):
```
python -I perf/quietlock/quietlock.py run --who <t> --what "melee phases" -- python -I server/evidence/0042/4745/melee_phases51.py <rb>/tests/unitanim/c5r_unitanim_combat_scene.exe <render_game_dir> <db copy> C:/c5-64bit-work/<t>/ph C:/c5-64bit-work/<t>/ph.csv 1,2,3,4,5,6 live
python -I perf/quietlock/quietlock.py run --who <t> --what "melee arms" -- python -I server/evidence/0042/4745/ts_melee51.py C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe <render_game_dir> <db copy> C:/c5-64bit-work/<t>/tsm C:/c5-64bit-work/<t>/tsm.csv
python -I server/evidence/0042/4745/types51.py
```
`--live` makes `combat_scene` agree with `trigger_scene` to the hundredth (without it the core runs on fixed clip times, 1-2 s longer) · about 1 min and 4 min · 4745 (W51, 2026-11-22) · `[C]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 18.15.
Stock's combat simulations per fight, live (phase, fight type, each figure's state and position; build 4390913 only,
`ReadProcessMemory` only), on a claimed instance in game with Quick Combat off (section 3.5; Quick Combat by
`cost_4778.py`'s chunk): install the listener, start the sampler in the background (run_in_background, bounded by
`--seconds`), play turns, stop the game (3.7), then analyse:
```
python -I tools/harness/lua.py --instance <inst> --state InGame -f server/evidence/0042/4745/listen52.lua
python -I -X utf8 server/evidence/0042/4745/simphase52.py --pid <pid from cli.py status> --out C:/c5-64bit-work/<t>/sim.jsonl --seconds 1800
bash server/evidence/0042/4745/turns52.sh <inst> 10 C:/c5-64bit-work/<t>/lua
python -I tools/harness/lua.py --instance <inst> --state InGame --json -f server/evidence/0042/4745/units52.lua > C:/c5-64bit-work/<t>/units.json
python -I -X utf8 server/evidence/0042/4745/phases52.py --sim C:/c5-64bit-work/<t>/sim.jsonl --lua C:/c5-64bit-work/<t>/lua --units C:/c5-64bit-work/<t>/units.json --csv C:/c5-64bit-work/<t>/phases.csv
```
`--probe` prints the vector once (empty `begin == end` before any fight). After the load the Huge save waits at its load
screen: `frontend.py dismiss` (3.5), or `ingame` stays exit 1. Stop the sampler before its bound with TaskStop once the
game is stopped (a single process, no children) · Huge t350: 10 AI turns 17 min, 174 shown fights, 5.6 MB · 4745 (W52,
2026-11-22) · `[L]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 18.15.1.
The charge fight held to the limit (4743, S15's Pathfinder on a barbarian Warrior, one long frame) on the same
`trigger_scene.exe`, and the probe of what a figure's machine does with a 1140 request (`unitanim play` on hand-made
timelines; the start scene from `c5r_unitanim_combat_scene --live --positions --out DIR`, its `DIR/start.c5units`):
```
python -I perf/quietlock/quietlock.py run --who <t> --what "charge scenes" -- python -I server/tests/effects/run_charge_scenes.py --exe C:/c5-64bit-work/<t>/ts/bin/trigger_scene.exe --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/charge [--no-charge-any] [--csv OUT.csv]
python -I server/evidence/0042/4743/machine_1140.py --unitanim <render build>/tools/unitanim/unitanim.exe --start DIR/start.c5units --attacker 2:1041 --game <render_game_dir> --db <db copy> --work C:/c5-64bit-work/<t>/m1140
```
33 cases (S15's Pathfinder steady + 14 long frames, two rush pairs steady + 8 long frames each) in about 50 s;
`RESULT PASS|FAIL n/m`; the probe about 5 s, one line per case A-D · at the default since 4743 (charge_any off): PASS
33/33; with `--charge-any` (4737's rule) the Pathfinder's long-frame cases FAIL without 4743's fix (0042 W46,
2026-11-21 23:13-23:19), PASS 33/33 with it (`charge_wait`, branch `w48-4743`, W48 2026-11-22 08:52) ·
`[L]` · [unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 18.13.
Since 4744 the bar has 39 cases: also `hm-kill-s1..s6`, a horseman's kill of a barbarian Warrior created at 50 damage
(5 of 10 figures drawn), steady, seeds 1-6. Default (a fight's standing figures are the drawn ones): PASS 39/39, the kills
8.03-8.40 s; `--all-held` (every held figure stands, the rule before 4744, also a flag of `trigger_scene.exe` and of
`c5r_unitanim_combat_scene`): FAIL 33/39, the six kills at 12.000 s (0042 W49, 2026-11-22 09:03) · `[L]` ·
[unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 18.16. The cavalry cases of 18.16 one by one
(trigger_scene, seeds 1-6; or `combat_scene --live --positions` with each case's summary and death times):
```
C5R_GAME_DIR=<render_game_dir> CASES="hm kn" bash server/evidence/0042/4744/cases_4744.sh <trigger_scene.exe> <out dir> [--all-held]
C5R_GAME_DIR=<render_game_dir> CASES="hm kn h1 m1" SEEDS="1 2" bash server/evidence/0042/4744/cs_4744.sh <render build> <out dir> [--all-held]
```
Every one-shot sound of a 4779 case with its time from Run and its 3D position, either arm (5057 W80, 2026-11-23, `[L]`):
`python -I -X utf8 server/evidence/0042/5057/sndline_5057.py <mss_log.named.tsv> <out>/verdict-4779.json <pics>/probe.json
kn_kill,kn_kill_b [--stems ...] [--json OUT]`. Route S's controller timeline of one fight:
`python -I -X utf8 server/evidence/0042/5057/figline_5057.py <server.c5anim> <attacker id> <defender id>`.
Figure offsets of route S against stock, offline (4779 W90, 2026-11-23, `[L]`): build the scene tool (about 1 min) `python -I
perf/quietlock/quietlock.py run --who <t> --what combat_scene -- python render/build.py --config Release --target
c5r_unitanim_combat_scene --build-dir <rb>`; then `server/evidence/0042/4779/w90/route_offset_w90.py extract --anim
S1=<big>/anim-S1-r1/server.c5anim [...] --out <work>/fights.json`, `route_offset_w90.py run --fights <work>/fights.json --with-0557
--exe <rb>/tests/unitanim/c5r_unitanim_combat_scene.exe --game <render_game_dir> --db <db copy> --work <work>/runs` (102 runs about
1 min, through quietlock), `route_offset_w90.py measure --work <work>/runs --db <db copy> --csv ... --json <work>/route-summary.json`;
stock: `stock_offset_w90.py --sim F:/c5-64bit-work/0042/evidence-4745/w52-run/sim52.jsonl.gz --phases
server/evidence/0042/4745/w52-run/phases52.csv --json <work>/stock-summary.json`; table: `table_w90.py <route> <stock> [--px 8.5]`.
Trap: c5r_unitanim_combat_scene exits 5 (cannot write summary.txt) when `--out` does not exist. The W52 sampler is the only stock
figure-position source (dumps, timelines and 0557 carry none).

### 4.7 I want a RenderDoc capture of the EXE's own drawing, and its export
```
python -I tools/harness/cli.py -i <inst> launch --preload %USERPROFILE%/tools/renderdoc-1.47/x86/renderdoc.dll --capture-dir C:/c5-64bit-work/<t>/captures
python -I tools/harness/cli.py -i <inst> capture [--frames 8]
python -I tools/harness/cli.py rdc-export <capture>.rdc C:/c5-64bit-work/<t>/exports/<name> --xml-dir C:/c5-64bit-work/<t>/xml --select terrain|water|all
```
The capture folder on C: or F:, never D: (the harness refuses it); only the allowlisted renderdoc.dll; a small save
(address space); never qrenderdoc on the desktop · 91-148 MB per capture; `rdc-export --select all` 54-180 s ·
0585 export (2026-11-20) `[I]` · `[C]` · [renderdoc-capture.md](../05-graphics-composition/renderdoc-capture.md),
[tools/harness/README.md](../../tools/harness/README.md) "Preload and RenderDoc captures".

### 4.8 I want a full memory dump of a live game (it keeps running)
`python -I tools/harness/cli.py -i <inst> dump --out F:/c5-64bit-work/<t>/dumps [--child server.exe]`, or any process:
`python tools/harness/dump.py PID OUT_DIR` (no `--help`) · a 3.6 GB EXE dump about 60 s · 3603 (2026-11-14) · `[C]` ·
[run-and-drive-the-game.md](run-and-drive-the-game.md) 14.
Read a dump offline (0678, 2026-11-22, `[L]`): `cdb.exe -z <dump> -y <pdb folder> -cf <commands file>` with `lmvm`,
`ub <addr>`, `!dh -i <module>`, `q` in the commands file. To name an offset inside a DLL that was already unloaded in the
dump: `cdb -z <that DLL> -y <its folder>`, then `.reload /f` and `ln <module>+<offset>` · known-crash-causes.md K24.

### 4.9 I want to replay bundles that live on F: only in quiet F: windows (a rule A slot)
```
f=C:/c5-64bit-work/<t>/diskwatch_$(date +%H%M%S).jsonl
python -I -X utf8 tools/diskwatch/diskwatch.py run --drive F: --out "$f" --interval 5 --minutes 1
python -I -c "import json,sys; v=[json.loads(l).get('rlat_ms') for l in open(sys.argv[1])]; v=[x for x in v if x is not None]; print(max(v) if v else -1)" "$f"
# only when that max is under 50 ms:
C:/c5-64bit-work/<t>/ss/bin/sceneshot.exe --bundle F:/<bundle> --out C:/c5-64bit-work/<t>/replay/<id> --dumped --compare --tolerance 1:1 --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt
```
Loop it per bundle (retry the window each minute, a STOP file, no start after the slot's deadline); count
`grep -c "colour EQUAL.*depth EQUAL"` against `grep -c "COMPARE frame"` in the log. A build-26577625 bundle is refused
with exit 9 (`Install`: the only install outside the pool is 4390913); `--allow-other-exe` draws it as DIAGNOSTIC,
never P1. A bundle with two water copies (a `Waves_Reload` inside the span) answers exit 8 "water clock ... refused"
or exit 10 (9.4's gap) · 1-2 min a bundle + the 1-min window · output on C: · 0615 W9 R1/R2, 2026-11-21 · `[L]` ·
[live-terrain.md](../05-graphics-composition/live-terrain.md) 11.4 "R1", "R2".


### 4.10 I want to know whether a picture is a presented frame (PrintWindow against the composite's dump)
`Session.shot` (PrintWindow) can catch the back buffer part-way through a frame: the EXE's models are then missing
while the server's layers are there. Judge models from the composite's dump (`tools/hi/capture.py`), not from
PrintWindow. Check shots against the composite's per-second counters:
`python -I server/evidence/0662/shotcheck.py <out dir> <dir with the on/off/reload shell_composite logs>` (seconds).
A live series of PrintWindow shots and back-buffer dumps at the seam views and after camera jumps:
`bash server/evidence/0662/live_0662.sh tree|check|prep|arm <inst> beta|exe|end` (about 8 min per arm; output
`C:/c5-64bit-work/0662/out1`) · 0662 (2026-11-22) · `[L]` ·
[city-renderer.md](../05-graphics-composition/city-renderer.md) 18, 18.1.

A notification jump by Lua (pans like `UI.LookAt`): `local id = Players[Game.GetActivePlayer()]:AddNotification(
NotificationTypes.NOTIFICATION_GENERIC,'t','t',x,y) UI.ActivateNotification(id)` · 0659 (2026-11-22, c5h-a) `[L]`.

### 4.11 I want a per-frame record around a camera jump (which frame a picture is)

NOT VERIFIED as a whole (proposed by impl-0663 under rule 32, taken by the MC at merge 2026-11-22; ticket 0665's live
step is its first use) `[I]`: set `lockstep_csv=1` in the instance's `shell_composite.ini` (writes
`shell_composite.lockstep.csv`, re-read each second); request back-buffer dumps in a loop by writing
`shell_composite.shot`; each dump's frame index is in the composite log line "shot: wrote ... frame N". Offline reader:
`python -I server/evidence/0663/offline_0663.py colour|dumps|lines` · 0663 (2026-11-22) ·
[fog-after-camera-jump.md](../05-graphics-composition/fog-after-camera-jump.md).
TRAP `[L, 2026-11-22, 0674's live check]`: `shell_composite.lockstep.csv` keeps appending across launches of the same
instance with no new header row, and the frame index starts again at each launch: cut the file per launch (by the
launch times, or move it away between launches) before reading it. TRAP: `tools/harness/cli.py wheel` did nothing in either
direction on a parked window (-4680, 0); `tools/hi/actions.py` `wheel` zooms (z 424 -> 572 per tick)
`[L; cause not checked]` · 0674 (2026-11-22) · [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 3.2.
Per-frame placement of PrintWindow pictures without a composite change (0668, 2026-11-23, `[L]`, an arm about 8-15 min on Tiny):
build with `bash server/evidence/0668/build_0668.sh` (about 4 min), then `bash server/evidence/0668/live_0668.sh tree|check|prep|arm
<inst> beta|betapw|pair|stock|end <inst>`; each capture is stamped with QueryPerformanceCounter and given the last lockstep CSV row
at or before it: `python -I -X utf8 server/evidence/0668/live_0668.py analyze --out <out> --arm <arm>` (classes OLD/NEW/ODD/FLAT/NOUI);
fog counters per jump: `python -I -X utf8 server/evidence/0668/counters_0668.py <render.log> <arm>/jumps.json`. Trap: PrintWindow of a
HIDDEN patched instance returns the flat scene clear (84,84,84) in 41-57 % of captures (stock 1 %): drop them (clear >= half the
picture) before judging. Trap: a driver started with a trailing `&` in a Bash call goes on invisibly and a second start is refused
at frontend prepare: use run_in_background, never `&`. On main 81baa8966 the composite's .shot trigger gives runs of
consecutive-frame dumps, not one per 500 ms.

### 4.12 I want to read a player's screen recording frame by frame (which layer moves when)
```
python -X utf8 server/evidence/0673/vidframes.py info <video.mp4>
python -X utf8 server/evidence/0673/vidframes.py extract <video.mp4> --start S --end S --out C:/c5-64bit-work/<t>/frames [--scale 0.25]
python -X utf8 server/evidence/0673/vidframes.py sheet C:/c5-64bit-work/<t>/frames --out <sheet.png> --cols 4 --first N --last M
python -X utf8 server/evidence/0673/vidframes.py track <video.mp4> --start S --end S --roi x,y,w,h --roi x,y,w,h
```
Plain `python`, not `-I` (OpenCV lives in the user site; no ffmpeg on the workstation). `frames.csv` holds each frame's
mean difference to the previous one: large values find the moments of change. Pictures stay under C:/c5-64bit-work,
never in git · seconds per few hundred frames · 0673 (2026-11-22) · `[L]` ·
[pan-coherence.md](../05-graphics-composition/pan-coherence.md) 1.

### 4.13 I want the release installer's layout with the player's options on an instance, and a live A/B of a composite setting
```
bash server/evidence/0673/drv.sh <inst> install          # set-dll to VP 5.4.8's DLL, the installer, tree lists
bash server/evidence/0673/live1.sh <inst> launch         # SAVE=<name> LAUNCHOPT=--timing for uncapped
ARMS="3400 8000 3400" bash server/evidence/0673/ab_bound.sh <inst> <tag>    # lockstep_bound_us switched live
python -X utf8 server/evidence/0673/lkcsv.py <arm>.lockstep.csv
bash server/evidence/0673/drv.sh <inst> csvoff ; bash server/evidence/0673/drv.sh <inst> remove
```
Edit the paths at the top (`EXE`, the player DLL in `$W/player/`) for another release. `drv.sh csv` turns on
`lockstep_csv`; the composite re-reads its ini on change, so `bound` acts within a second on a running game. The
shipped default of `lockstep_bound_us` is 8000 us since ticket 0675 (2026-11-22; was 3400). TRAP (ticket 0691,
2026-11-23 `[C]`): since 0691 the shipped `lockstep_mode` is `exact` (no bound, no give-up), and `lockstep_bound_us` /
`lockstep_giveup` act only with `lockstep_mode=bounded`: an A/B of the bound sets `lockstep_mode=bounded` in the same
ini write, else both arms run exact. A window
for the player's own check: `cli.py -i <inst> guard off`, `cloak off`, `park 0,0` (D237 acceptance only) · install
25 s, an A/B arm about 50 s · 0673 (2026-11-22) · `[L]` · [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 1-2.

### 4.14 I want every displayed frame of a visible game window, with time stamps
```
cmd //c "$(cygpath -w "$PWD/tools/capture/build.cmd")"            # -> tools/capture/bin/dupcap.exe (created by the command), git-ignored, x64, static CRT
tools/capture/bin/dupcap.exe --list                              # (created by the command) outputs in desktop pixels; pick K
dupcap.exe --out <new or empty dir> --seconds S [--frames N] --output K --rect x,y,w,h --start-file <flag> [--stop-file <flag>] [--max-mb M] [--format png|bmp] [--only-dirty]
python -I -X utf8 -m unittest discover -s tools/capture/tests -p test_dupcap.py
```
DXGI Desktop Duplication: one row per desktop update in `frames.csv` (QPC at acquire, `LastPresentTime`,
`AccumulatedFrames`; above 1 = frames missed, counted in the SUMMARY line), images written only after the recording.
Driver order: start dupcap, wait for `dupcap: READY`, create the start file, wait for `dupcap: start file seen,
recording`, then send inputs (row 0 = `kind=base`, the picture before them). Exit codes 0 ok, 2 usage, 3 duplication
unavailable, 4 buffer cap, 5 access lost twice. The window must be visible: on the workstation game windows stay hidden
(D63), so game recordings are for the laptop or oldpc (copy the exe; it needs nothing else). Recordings to F: or the
test machine's disk, never git. Through the quiet lock give cmd with backslashes:
`python -I perf/quietlock/quietlock.py run --who <t> --what "dupcap build" -- 'C:\Windows\System32\cmd.exe' //c "<build.cmd>"`
(`C:/Windows/System32/cmd.exe` prints "The syntax of the command is incorrect" and builds nothing) · build about 20 s,
tests 9 in about 6 s (25 s with a build) · 0673 worker-0673d (2026-11-22) · `[L]` on the workstation, NOT VERIFIED on a
game window or the laptop · [tools/capture/README.md](../../tools/capture/README.md).
*(Corrected 2026-11-22 by 4.15: dupcap recorded the game window on the laptop.)*

### 4.15 I want stock against product with the same scripted inputs, every displayed frame (laptop)
```
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --name "<t> capture probe" --timeout 600 --expect-min 2 \
    --file <dupcap.exe built by tools/capture/build.cmd>=cap/dupcap.exe -- "%TESTBED_PYTHON%" tools/capture/capture_job.py --arm stock --probe
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --name "<t> capture stock" --timeout 3000 --expect-min 25 \
    --file <dupcap.exe built by tools/capture/build.cmd>=cap/dupcap.exe --file <copy of tools/harness/native/bin>=native \
    --file <vp-dll-dev skill>/assets=frontend-assets -- "%TESTBED_PYTHON%" tools/capture/capture_job.py --arm stock --repeats 1
#   product: the same plus product_path's three --file lines (pp/bin, pp/proxy/CvGameCore_Expansion2.dll,
#   pp/composite/shell_composite.dll) and --arm product (--options overrides the [Renderer] set)
python -I -X utf8 tools/testbed/tb.py --host laptop fetch <JOB> --out F:/c5-64bit-work/<t>/cap/<arm> --max-mb 0.5
python -X utf8 server/evidence/0673/capcmp.py summary F:/c5-64bit-work/<t>/cap/<arm>
python -X utf8 server/evidence/0673/capcmp.py track <arm>/out/cap/keys0300_r1 --elem banner:x,y,w,h [--elem ...] --brief --json <f>
python -X utf8 server/evidence/0673/capcmp.py sheet <arm>/out/cap/walkfoot060_r1 --box x,y,w,h --every 2 --out <png on F:>   # walk ids before 2026-11-23: walkfoot_r1
```
TRAP `[L, 2026-11-22]`: on the laptop `DuplicateOutput` is refused (`E_ACCESSDENIED`, exit 3) while the console's display
is idle-off (10 min without input). Since 0710 (2026-11-23, `[L]`, jobs 20261123-172453-1665 and -174118-474e) `capture_job.py`
wakes it itself: one SendInput mouse move of 1 px and back through `tools/hi/realinput.py`'s fenced site, then 3 s, and holds it
for its run (SetThreadExecutionState ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED, cleared on every exit); a recording
refused anyway gets one more wake and one retry. Read capture.json `before_wake`, `wake` (sent, idle_before_s, input_desktop) and
each case's `refused_first`/`attempt`; the job log's `awake_end` line shows the clear. `--probe` (about 1 min, no game) still says
whether duplication works (exit 0) and records before and after the wake. If `wake` shows an error (SendInput refused: a locked
desktop), a job cannot wake the display: post it to the MC (quiet-testbed.md 21.3). `--max-mb 0.5` on the fetch brings back only the reduced grey frames (`out/cap`, about 100 KB
each); the full-size PNGs stay in the job's `out/raw` on the laptop. Lua results come back quoted (`"ok|..."`):
strip them. The analysis needs OpenCV: plain `python`, not `-I`. TRAP `[L]`: `tb.py cancel` on a running headed
job kills it without its restore (job `3e82`). The next job's backup then holds the prepared front end and puts it
back (verify `size MainMenu.lua`). Let a job end, or submit `capture_job.py --repair` (stops a game left behind
through the harness, front end back to the install manifest, `provision.py verify`). TRAP `[L]`: in the keys/drags
setup `UI.LookAt` sent over the Lua channel did not move the camera; recordings stay at the load's view (on TB-HUGE_0350:
the city Asyut and its selected unit, not the capital). Cause `[I, from overlay-system.md 9.2 L]`: the setup's
`UI.ClearSelectionList()` selects the next unit that needs orders and pans to it (the machine gun at Asyut), and it
calls `UI.LookAt(plot, 0)` (two arguments: did not move the camera on another save, terrain-decals.md). The keys/drags
cases keep that view on purpose (their earlier runs are compared with it). The `walk` case (rewritten 2026-11-23,
worker-0673u, for 0685) does it the knowledge base's way, once before the first walk: auto unit cycle off, every unit of
the player asleep, then the clear; per walk `UI.LookAt(B)` with one argument, 3 wheel ticks in, again, and a check by the
EXE's camera read (`camera_pixel` in `tools/hi/uimap.py`, read only): B within 12 % of the client's centre, else
`UI.SelectUnit` + the centre-on-selection key `C`. Walks get their own job (`--cases walk`): the quiet step changes the
camera of keys/drags run after it. 0685's arms after 0691's run (same grant, same SHIP; stock, then product once the
stock job has ended; 4 walks an arm with the laptop line below: foot and mounted, 60 degree bends by the pathfinder, 2
repeats):
```
EXTRA="--walk-crop 1700x1200 --walk-at 0.553,0.36 --walk-order path --walk-bends 60" MINUTES=10 bash server/evidence/0673/acc0685_laptop.sh   # in the background; the laptop line of 2026-11-23 (pan-coherence.md 5.4); SHIP, LOG (<SHIP>/../probe_loop_0685.log); exit 0 = both submitted, "STOCK <id>" / "PRODUCT <id>" in LOG
python -I -X utf8 tools/testbed/tb.py --host laptop wait <JOB> --timeout 170    # re-arm until done (load 194 s + about 5 min an arm)
python -I -X utf8 tools/testbed/tb.py --host laptop fetch <JOB> --out F:/c5-64bit-work/0673/cap/w0685-<arm>-<JOB> --max-mb 1    # the colour walk frames (out/cap/walk*, about 0.3-0.8 MB each), not out/raw
python -X utf8 server/evidence/0685/turn0685.py jobs <stock fetch> <product fetch> --tsv F:/c5-64bit-work/0673/cap/turn0685.tsv --strips F:/c5-64bit-work/0673/cap/strips0685
python -X utf8 server/evidence/0685/turn0685.py case <fetch>/out/cap/walkmounted060_r1 --csv <f> --strip <png on F:>   # one case: per frame root, axis, members, change
python -I -X utf8 tools/testbed/tb.py --host laptop fetch <JOB> --out C:/c5-64bit-work/0673/jobs_d/<arm>_raw --include "out/raw/walk*060_r1/*"   # full-size frames (2026-11-24: brought back the whole job, about 2,280 files, 3 GB, 5 min each)
python -X utf8 server/evidence/0685/zoom0685.py walkmounted060_r1 350 1000 1 --stock <stock raw fetch> --prod <product raw fetch> --out <dir>   # every-frame crops around the root, both arms: read the facing by eye (unit-server-scene.md 19.8)
ARMS=product PENV="C5_RENDER_UNITS_WALK_LOG=1" SHIP=<ship> LOG=<log> REPEATS=2 MINUTES=20 EXTRA="--walk-crop 1600x1100 --walk-order path --walk-bends 60" bash server/evidence/0673/acc0685_laptop.sh   # the product arm alone with the walk diagnostic (unit-server-scene.md 19.9); fetch with --include out/server.log, then grep "walk (0685)"
SHIP=<ship> ARM=product|stock REPEATS=3 CASES=keys,drag|zoom EXTRA="[--look-at X,Y] [--reveal] [--zoom-ticks 3 --zoom-steps 4]" bash server/evidence/0736/lap0736.sh   # one arm of every-frame pans / drags / wheel zooms (0736); --look-at uses the one-argument UI.LookAt
ARMS="stock product" PENV="C5_RENDER_UNITS_WALK_LOG=1" SHIP=<ship> LOG=<log> REPEATS=3 MINUTES=45 EXTRA="--walk-crop 1600x1100 --walk-order path --walk-bends 60" bash server/evidence/0673/acc0685_laptop.sh   # both arms, the log in the product arm (pass 4, 2026-11-25: about 8 min an arm); a plain tb.py fetch then brings back the whole job with out/raw (about 2.9 GB, 5 min)
ARMS="stock product" PENV="C5_RENDER_UNITS_WALK_LOG=1" SHIP=<ship> LOG=<log> REPEATS=2 MINUTES=45 EXTRA="--walk-kinds armour --walk-crop 1600x1100 --walk-order path --walk-bends 60,120" bash server/evidence/0673/acc0685_laptop.sh   # 0784: a spawned landship (UNIT_WWI_TANK, else UNIT_TANK) instead of foot and mounted (--walk-kinds foot,mounted,armour in any order; default foot,mounted); ids walkarmour060_r1 ...; armour only with --walk-units spawn
SHIP=<ship with purity.tsv beside bin/> OUT=<F: folder> bash server/evidence/0784/lap0784b.sh vp|product   # 0784b (2026-11-26): vp = a read-only job printing the laptop instance's VP version (vpver.py: the modinfo versions, the Community Patch DLL's SHA-256 head); product = the product armour arm alone with the purity file loaded: provision.py set-proxy copies only a fixed list into VP64\, so purity_wrap.py sets VP_PROXY_PURITY=<shipped purity.tsv>|<VP64 x64 DLL> for the job and its game; commit first (the job runs the submitting HEAD)
python -X utf8 server/evidence/0685/arrive0685.py --stock <stock fetch> --prod <product fetch> [case ...]   # leave A, A -> C and the settle at C per case from full-size frames (unit-server-scene.md 19.10): the walk's timing where turn0685's readers fail
bash server/evidence/0782/ship0782.sh                                    # 0782: a product SHIP from this checkout (shell build, server + proxy of allb0782.sh 1, purity.tsv in bin/) on F:
ARMS=product SHIP=<ship> LOG=<log> REPEATS=1 PENV="VP_PROXY_PURITY=C:/c5-testbed/jobs/<earlier product job>/bin/pp/bin/purity.tsv|C:/c5-testbed/jobs/<earlier product job>/bin/pp/bin/CvGameCore_Expansion2.dll" EXTRA="--walk-crop 1600x1100 --walk-order path --walk-bends 60" bash server/evidence/0673/acc0685_laptop.sh   # the product arm WITH the purity list (MC-B's standing check 'purity: loaded' in out/vpproxy.log): the product path's set-proxy copies only PROXY_VP64_FILES, so a purity.tsv in SHIP/bin never reaches VP64; the proxy's test variable takes '<purity.tsv>|<x64 DLL>' (the DLL whose SHA-256 the list is bound to; the file alone is refused). Use the copies an earlier product job of the same SHIP left on the laptop; check them first with: tb.py --host laptop submit --no-src --name "<t> purity file check" --timeout 120 --expect-min 1 --file <checkout>/server/evidence/0782/purity_check.py=pc.py --wait -- "%TESTBED_PYTHON%" "%TESTBED_BIN%/pc.py" <path>   (0782, 2026-11-26, [L])
python -X utf8 server/evidence/0782/legs0782.py <fetch> walkmounted060_r2 200 640 --out <png on F:> --fixed 1300,520 --w 600 --h 400 --cols 3   # 1:1 every-frame crops of the horses (the legs by eye; unit-server-scene.md 19.12); --fixed is the raw pixel centre, else the root from turn0685 + --dx/--dy
python -X utf8 server/evidence/0736/flash0736.py <fetched job> [--min 6] [--sheet <dir>]   # one-frame transients per case (a flash, a camera jump); baseline on walks: top score about 1.5
SHIP=<ship> OUT=F:/c5-64bit-work/<t>/flash bash server/evidence/0790/lap0790.sh   # 0790, in the background, inside MC-B's laptop grant: stock-rest, product-rest, stock-ai, product-ai one after the other, fetch, then prints the gate line
python -X utf8 server/evidence/0790/flash0790.py gate --stock <J1>,<J3> --product <J2>,<J4> --composite <J2>/out/shell_composite.log,<J4>/out/shell_composite.log --render <J2>/out/render.log,<J4>/out/render.log --json <f> --tsv <f>   # PASS/FAIL per move class (exit 0/1)
python -X utf8 server/evidence/0790/flash0790.py scan <fetched job or frames folder> [...] [--sheet <dir on F:>] [--frames-tsv <f>] [--jobs 6]   # per case t1 t2 st | wf ui g1; a player's video: vidframes.py extract (4.12), then scan the folder
python -X utf8 server/evidence/0790/flash0790.py counters --composite <shell_composite.log>[,...] --render <render.log>[,...] [--from HH:MM:SS --to HH:MM:SS]   # the retained-world proposal's section 9 rows 2-6; exit 1 when one is not zero
python -X utf8 -m unittest discover -s server/evidence/0790 -p test_flash0790.py   # synthetic, about 30 s
python -X utf8 server/evidence/0790/gens0790.py <fetched product job> <frames tsv of a scan>   # the identity set generations (render.log) placed on the recorded frames, with their scores
```
INTERPRETER `[L]` (lead-0673, 2026-11-26): flash0736 / flash0790 / gens0790 and their tests need OpenCV, which is installed in the user site of the default Python 3.11 (`%APPDATA%\Python\Python311\site-packages`, cv2 4.9.0): run them with plain `python` (no `-I`: `-I` drops the user site and gives `No module named 'cv2'`); run the tests with the `unittest` line above (9 tests, about 25 s); `python -m pytest` on test_flash0790.py fails at collection on this machine (NotImplementedError) 
0790's every-frame flash check (impl-0790, 2026-11-26; tools `[C]` and calibrated offline on recorded jobs, NOT yet run live
on a candidate). capture_job.py's new cases: `still` (`--still-seconds`, default 4 s at rest), `jump` (two far jumps by the
one-argument LookAt, `--jump-hold` 1.5 s each; the far plot from LUA_FAR, the farthest revealed foreign capital, or
`--jump-to X,Y`; capture.json `jump_plot`) and the switch `--ai-turn` (keys/drag/fastdrag/zoom/still/jump recorded during an
AI turn, ids `ai...`: the recording starts, `turns.end_turn`, 1 s, the inputs, `LUA_AI_ACTIVE` before and after the inputs in
the case's `ai` record (which AI players' turn it covered), then `turns.wait_turn`, `--ai-wait` 300 s; one game turn per
case). flash0790.py's scores per frame: `t1` (per 16 px tile at 320 px wide: min(dA, dB) - dAB, each tile matched in the
neighbours around the global motion, a shift and a zoom scale found by phase correlation over 11 scales, with half-pixel
copies), `t2` (two-frame rule), `st` (the settle after a cut); shown, not gated: `wf` (more than 70 % of tiles transient: a
picture from another camera), `ui` (a dark interface panel, the plot tooltip) and `g1` (flash0736's whole-frame score). Gate:
a product class FAILs when its top t1/t2/st exceeds max(stock's top, floor 6.0) + margin (`--floor 0`: the literal rule).
Calibration (TB-HUGE_0350 on the laptop, recorded 2026-11-25, tables in server/evidence/0790/calibration/): 0738's fast drags
stock (jobs 015002-2ad6, 020345-87ce) t1 1.6-3.2, t2 0.9-2.1; beta 4 product (015651-a4c2, 021037-6730) t1 0.2-3.1, t2 0.4-2.5;
0736's beta 4 keys/drags/zoom (jobs 003902-b4db, 004844-4cd3) t1 0.9-5.0 except the frames below; the owner's video of 0788
(vidframes frames, C:/c5-64bit-work/0788/vf) t1 14.4 (the star fort missing in frames 5 and 7), t2 9.1, while flash0736's g1
reads 0.36 there. FOUND `[L, recorded]`: beta 4 product, job 20261125-003902-b4db `keys1200_r1` frame 48, t1 18.8: the obelisk
landmarks, the Great Wall and farm buildings missing for one frame while terrain and smoke stay, presented 13 ms after
render.log's "the compositor applied identity set generation 12 (5548 keys)" (gens0790.py); zoom3x4_r1 frame 28 (t1 10.7,
112 ms after generation 8) is not clear by eye; 0753's smoke build (job 20261125-192003-574a) `drag1000r_r1` frame 18, t1 22.8,
the harbour piers fade for a frame, no generation near. TRAPS `[C]`: OpenCV 4.9's `phaseCorrelate` with a window writes the
window into its inputs (pass copies); the plot tooltip at a drag's start scores about 100 in both arms (hence `ui`); a hidden
workstation instance cannot be recorded (dupcap), so the check runs on the laptop and oldpc (`HOST=oldpc`); about 40 s per
110-frame case on one core (`--jobs 6`: a 32-case gate about 10 min). Counters `[L, logs]`: `no_shadow` (section 9 row 4) is
not zero after the first live minute on 0753's smoke (514, in bursts in the seconds of identity set generations) and 0719's
oldpc runs (8): the gate's counters fail on today's builds until the MC rules on row 4 · 0790 · [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 3.3.
Read `capture.json` `walk_quiet`, `walk_plots` and each case's `camera` (`method`, `verified`) first: `verified` false
means the camera read failed and the crop is the fallback (centred, 1400 x 900). TRAPS `[L]` (2026-11-23): the
laptop's queue runs two jobs at once, so two arms submitted back to back put two games at 0,0 and void both recordings
(the runner now waits for the stock job); `--walk-crop auto` at 3 zoom ticks on the laptop's 3825 x 2087 client is
about 2342 x 2056 and fills dupcap's 1,500 MB buffer after ~3 s (rc 4): use the fixed crop above; `--walk-order
waypoints` (the appended second MOVE_TO) never ran in either arm (the unit stopped on B with moves left): use `path`
with 60 degree bends. Check `walk.json` `where_after` = C's plot. TRAPS `[L]` (0685, 2026-11-24): on oldpc an
"Apple Software Update" window on the interactive desktop came up over the game a few hundred ms into every walk (both
arms of jobs 155454-bf92 / 160220-550d void; it is the owner's session: never close it remotely, tell MC-B); read the
strips before any number (and for the facing, the full-size frames through zoom0685.py: turn0685's axis reader gave stock 0-36 ms against the eye's ~2-frame turn and no reading on 6 of 6 mounted walks, 2026-11-24) (the laptop, 17:07-17:24 the same day: a Windows Settings "Power & sleep" window over
the game voided jobs 170718-4977 / 171630-a4c3 the same way; the capture probe does not see it). oldpc's Python 3.8 `time.perf_counter()` counts from the process start, not raw QPC: the
marks (about 230 s) and `frames.csv` (about 654,885 s) are on different bases; `turn0685.py` rebases frame 0 on the case's
`start_file_t` (capture.json) when they differ by more than 100 s. Stopping the harness task that runs
`acc0685_laptop.sh` did not end the script's bash (it went on and submitted probes 25 min later): find it with
PowerShell `Get-CimInstance Win32_Process | ? CommandLine -like '*acc0685_laptop*'`, stop by exact pid (its `tb.py wait`
child too), then `tb.py --host <h> cancel <job>` for what it queued. Tests:
`python -I -X utf8 -m unittest discover -s tools/capture/tests -p test_capture_job.py`,
`python -X utf8 -m unittest discover -s server/evidence/0685 -p test_turn0685.py` (synthetic walks).
0689 step 1 from a product run: `python -X utf8 server/evidence/0673/lat0689.py <render.log> <lockstep.csv>`.
F5 per recording (0691, 2026-11-23, `[L]` on last night's jobs): `python -X utf8 server/evidence/0673/caplock.py --job
<fetched job> [<job> ...] [--tsv <file on F:>]` (the share of displayed frames in a camera move whose newest present is
`exact`; also fps, waits, ready - publish, the job's `hung_ends`).
0691's laptop acceptance (worker-0673r, 2026-11-23; replaces the acc-0691 line of 02:30): a probe every 3 min, then
the product arm at once: keys + drags x 3 (lockstep_mode=exact from the load, lockstep_csv=1, pacing=1), then the `wide`
case once (zoomed out 60 s, not recorded: 0692's particle figure), then `out/pacing.json` + `pacing.txt`
(pacing_report.py on the laptop), with the TEST ONLY server.cfg line `render_pass_long_ms=8` (a `pass long` line per
pass over 8 ms, 0689): `capture_job.py --server-cfg-extra KEY=V` (repeatable, product arm) writes the lines to
`<work>/cap/server_cfg_extra.cfg`, sets vpproxy.ini `server_cfg_extra=` after product_path's prep and unsets it before
its restore (the restore-by-hash holds; `capture.json` prep.server_cfg_extra). Builds of ec19daa90 in `C:/c5-64bit-work/0673/acc-0691b/ship` (the default `SHIP`).
Submit from a checkout whose HEAD has capture_job.py's `wide` case (the job runs the submitting HEAD), inside MC-B's grant:
```
sh server/evidence/0673/acc0691_laptop.sh     # in the background; SHIP, MINUTES (30), LOG (<SHIP>/../probe_loop.log); exit 0 = product submitted, its id in LOG
python -I -X utf8 tools/testbed/tb.py --host laptop wait <JOB> --timeout 170    # re-arm until done (load 194 s + about 12 min)
python -I -X utf8 tools/testbed/tb.py --host laptop fetch <JOB> --out F:/c5-64bit-work/0673/cap/acc0691b-<JOB> --max-mb 0.5 --include out/render.log --include out/shell_composite.log --include out/shell_composite.lockstep.csv --include out/pacing.json --include out/server.log
python -X utf8 server/evidence/0673/caplock.py --job <J> --tsv F:/c5-64bit-work/0673/cap/f5-acc0691b.tsv          # J = the fetched folder; F5 per recording, hung_ends
python -X utf8 server/evidence/0673/capbatch.py exact=<J> --tsv F:/c5-64bit-work/0673/cap/batch-acc0691b.tsv      # k, d0 per element (5.2's table)
python -I -X utf8 perf/pacing/pacing_report.py --composite <J>/out/shell_composite.log --csv <J>/out/shell_composite.lockstep.csv --render <J>/out/render.log   # = the job's out/pacing.txt
python -X utf8 server/evidence/0673/lat0689.py <J>/out/render.log <J>/out/shell_composite.lockstep.csv           # 0689: wake / submit / ready - publish
python -I -X utf8 server/evidence/0692/an0692.py <J>/out/render.log --from <HH:MM:SS> --to <HH:MM:SS>           # 0692: the `wide` case's "hold" mark (from, to) in <J>/out/capture.json
```
`--max-mb 0.5` leaves render.log and the composite log on the laptop unless named (`--include`; the 2026-11-22
fetch of the r2-r3 job, `F:/c5-64bit-work/0673/cap/product-r23`, has neither). TRAP `[L]`: testbed job ids end in hex (`20261123-020725-70ac`): a script that reads `submitted <id>` with `[0-9-]*` misses
them and submits the job again. A whole arm (TB-HUGE_0350, 10 recordings) takes about
9 min of game time after the 194 s load · 0684 worker-0673f (2026-11-22) · `[L]` ·
[pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.

The walk case's product arm with the proxy's purity list loaded (0783, 2026-11-26; MC-B accepts a product pass only with
`purity: loaded` in vpproxy.log, #16715): the job runs `server/evidence/0783/purity_job.py` (shipped with the job), which
sets `VP_PROXY_PURITY=<%TESTBED_BIN%/w0783/purity.tsv>|<%TESTBED_BIN%/pp/bin/CvGameCore_Expansion2.dll>` and runs
`capture_job.py` with the job's arguments; the purity file for the SHIP's x64 DLL from
`python -I -X utf8 proxy/tools/derive_purity.py --src <pin src> --dll <deps dll> --out <dir> --commit <pin>` (seconds):
```
ROOT=<checkout whose HEAD the jobs run> SHIP=<bin/ proxy/ composite/> PURITY=<purity.tsv> OUT=F:/c5-64bit-work/<t>/lap PASSES="1 2" \
    bash server/evidence/0783/lap0783.sh     # pass 1 foot + mounted (REPEATS 3), pass 2 armour (REPEATS2 2); each stock then product
python -I -X utf8 server/evidence/0783/walklog0783.py --log <product fetch>/out/server.log --stock <stock fetch>/out/cap/<case>/walk.json
```
TRAP `[L, 2026-11-26]`: a SHIP must match the laptop's VP release (tb1 is 5.4.8 while main is on 5.5): build it from a commit
before the pin move, on `keep/pins-0.1b`.

0738's fast drags from rest, stock against product, uncapped, quick modes on (impl-0738, 2026-11-25, `[L]`; about 6.5 min a
job on TB-HUGE_0350 with 8 drags; pan-coherence.md 5.18):
```
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --name "<t> foreground check" --timeout 300 --expect-min 1 -- "%TESTBED_PYTHON%" server/evidence/0738/fg0738.py
SHIP=<a folder laid out as laptop0738.sh's header says> OUT=F:/c5-64bit-work/<t>/laptop ARMS="stock product stock product" DRAGS=8 \
    bash server/evidence/0738/laptop0738.sh      # in the background: submits each job only after the one before ended, then fetches
python -X utf8 server/evidence/0738/drag0738.py --arm stock=<J1>,<J3> --arm product=<J2>,<J4> --tsv <f> --json <f>   # first arm = base
python -X utf8 -m unittest discover -s server/evidence/0738 -p test_drag0738.py                                     # synthetic checks
"%TESTBED_PYTHON%" server/evidence/0738/fatal0738.py tb2 "<owner>: <why>"     # as a headed job: a fatal box left by a cancelled job
```
TRAPS `[L, 2026-11-25]`: submitting several headed jobs at once let the laptop's runner run two at once (two games at 0,0: all
void); TaskStop of the background Bash does not end the script (find it with `/proc/<pid>/cmdline` and `kill <pid>`); a
drag from 15 % of the width starts on EUI's left panel on this save (no camera move): `--fast-span` default 0.35,0.75; the
drag's window and guard set-up take 0.5-1.2 s before the first move (the recording now covers it).

The same fast drags on a HIDDEN workstation instance, stock against product, where dupcap cannot record (impl-0748,
2026-11-25, `[L]`; c5h-new-b, EXE 26577625; about 4.5 min a stock launch, 5 min a product launch, on the huge save, with
10 drags; pan-coherence.md 5.19). It needs MC-B's grant, the harness natives built in the worktree, and the owner's
Steam game not running:
```
NOTE="<grant>" bash server/evidence/0748/ws0748.sh claim      # INST (default c5h-new-b), W (default C:/c5-64bit-work/0748/ws: use your own)
bash server/evidence/0748/ws0748.sh begin                     # save copy C5H-0748-HUGE_0350, GraphicsSettingsDX11.ini WaitForVSync=0 (SHA-256 backup)
DRAGS=10 bash server/evidence/0748/ws0748.sh series           # in the background; ARMS="stock product stock product", N0=<label offset>,
                                                              # each arm under quietlock run --take --minutes 12 (QL_EXTRA=--overlap when the take is refused)
bash server/evidence/0748/ws0748.sh finish                    # ini back by hash, save copy out, verify --quick, leftovers, release
python -I -X utf8 server/evidence/0748/an0748.py --arm stock=<W>/s3,<W>/s5 --arm product=<W>/p2,<W>/p4 --tsv <f> --json <f>
python -I -X utf8 -m unittest discover -s server/evidence/0748 -p test_an0748.py   # synthetic, about 2 s
```
Product arms lay beta 4's cand6 layout and composite (`B`, `COMP`, `CI`), the owner's options (`OPTS`), and the pack's
`purity.tsv` beside the x64 DLL (`PUR`). Without that file a pack=0 proxy answers nothing locally (MC-B #14000). Check for
`purity: loaded` in `<W>/<label>/vpproxy.log`. The camera sampler `cam0748.py` reads the EXE's view matrix read only
(gt_reader, the local EXE table; no address printed). It writes one row per change and gives one row per EXE frame with
a new camera.

TRAPS `[L]`:
- `lua.py` without `--state InGame` runs where `PreGame` is nil.
- A `--take` is refused after its 20-minute job wait while other owners keep starting heavy jobs; account A's jobs count
  too.
- `ws0748.sh` refuses an existing label folder: give `N0` for a second series.

### 4.16 I want the lockstep's numbers on a hidden workstation instance (key pans and drags, CSV only, exact against bounded)
```
bash server/evidence/0691/live0691.sh <inst> prep                     # lay.py prep (B=, SH=, INI= the composite ini) + helper on, the reporter's options, lockstep_csv=1
bash server/evidence/0691/live0691.sh <inst> ini lockstep_mode=bounded   # the LOAD must run bounded (TRAP below)
ROOT=<instance root> ARMS="bounded exact" bash server/evidence/0691/live0691.sh <inst> run <LABEL> [--timing]
bash server/evidence/0691/live0691.sh <inst> end                      # collect, restore by hash, leftovers
python -I -X utf8 server/evidence/0691/an0691.py --marks <W>/marks.tsv --run <LABEL>-exact=<W>/<LABEL>.lockstep.csv=<W>/<LABEL>.composite.log [--run ...] [--tsv <f>]
```
`run` = prepare, launch (hidden, guard on), dismiss, autoplay off, then per arm `lockstep_mode` set live in the laid ini
and `drive0691.py` (0684's inputs: LEFT/RIGHT held 150/300/600/1200 ms, drags 40 % <-> 65 % over 300 and 1000 ms, 3
repeats, each input's QPC in `marks.tsv`), bounded again, the safe stop, the CSV moved out of Logs (one header per
launch). W = `W0691` (default `C:/c5-64bit-work/0673/live-0691`); `ROOT` skips `pool.py list` (2 min with the pool
busy). A Tiny launch with two arms about 6.5 min · 0691 worker-0673k (2026-11-23, c5h-c, EXE 4390913) · `[L]`.
TRAP `[L]`: with `lockstep_mode=exact` in the ini at launch the first map frame of the load deadlocks (the server holds
it for the feed's plot table, which later EXE frames bring); 30 s later the hung route ends the game (exit 3 in a test
session). Load bounded, switch live. (Builds before the hold report only: from ticket 0691's hold report, design
3.2.1, 2026-11-23, the composite skips a held frame; live 04:42: the load with exact passes, so `ini lockstep_mode=exact` before `run` is fine on such builds.) TRAP `[L]`: TaskStop (or killing the shell) of a background Bash call does not end
a Git Bash script it started: the script went on and drove a second launch's game in parallel. Find it by command line
(`Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match '<script>' }`) and end that tree by its pid ·
[pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.7.

### 4.17 I want the fog particles' live cost in a wide view (render.log `fog 1s` + `pass 1s`, the pacing lines) on a hidden instance
```
python -I server/evidence/0615/4914/saves_snap.py snap --instance <inst> --work <W>/saves     # before prep; restore after end
B=<build_server --out> SH=<shell build> bash server/evidence/0692/live0692.sh <inst> prep    # 0691's prep (W0692, default C:/c5-64bit-work/0673/live-0692)
ROOT=<instance root> [SAVE=<name>] [REPEATS=1] [HOLD=60] bash server/evidence/0692/live0692.sh <inst> run <LABEL> --timing [--env C5_RENDER_PARTICLE_THREADS=1]
bash server/evidence/0692/live0692.sh <inst> end                                                # 0691's end: collect, restore by hash, leftovers
python -I -X utf8 server/evidence/0692/an0692.py <W>/<LABEL>.render.log --from HH:MM:SS --to HH:MM:SS   # the hold's window ("hold 60 s from" in live0692.log)
python -I -X utf8 server/evidence/0691/an0691.py --marks <W>/marks.tsv --run <LABEL>=<W>/<LABEL>.lockstep.csv=<W>/<LABEL>.composite.log   # the run label = the marks' tag
python -I -X utf8 perf/pacing/pacing_report.py --composite <W>/<LABEL>.composite.log --csv <W>/<LABEL>.lockstep.csv --render <W>/<LABEL>.render.log
```
`run` = prepare (default save `C5H-4773-TINY_0018`, on the mods548 instances), launch, dismiss, autoplay off, 0691's key
and drag series (`REPEATS`), then `wide0692.py`: `actions.wheel` -30 at the client's centre (negative = zoomed out),
3 s settle, `HOLD` s still, both as QPC rows in `marks.tsv` (series `wheel`, `hold`); the safe stop; CSV, composite log,
render.log and vpproxy.log cut to W. `--env` reaches server.exe (the EXE's environment is inherited: the 1-thread
control's particle cpu doubled). One Tiny launch about 6 min (load 2.5-4 min on a loaded workstation). The particle
call is inside the `draw` stage of `pass 1s` (`afterWorld`), not `fog` (the mask) · 0692 worker-0673p (2026-11-23,
c5h-new-a, EXE 26577625) · `[L]` · [fog-of-war.md](../05-graphics-composition/fog-of-war.md) 12.6.

### 4.18 I want the server's pass split by stage and the publish-to-ready time per frame, in camera moves and at rest (read only)
```
python -I -X utf8 server/evidence/0689/split0689.py --log <W>/<LABEL>.render.log --csv <W>/<LABEL>.lockstep.csv --marks <W>/marks.tsv --tag <LABEL> [--slow-ms 8] [--json OUT]
python -I -X utf8 server/evidence/0689/split0689.py --csv <shell_composite.lockstep.csv>     # a run before 0688 (CSV only, e.g. the laptop's)
```
Needs a server with 0688 (`pass 1s:` / `pass long:` lines) for the stage tables; without `--marks` the whole run counts
(the load included). Prints per class (camera moving, still, the marks' `hold`): the CSV's wake / draw / ready / insertion
/ wait percentiles and the share of server frames over 6.06 / 8 / 16.7 / 33.3 ms; per stage ms per drawn frame, share of
the pass, per-second mean and maximum percentiles; every `pass long:` with its class and the composite's present around
it; slow frames named by stage. `pass 1s:` has per-second means and maxima only: for per-pass stage percentiles run the
game with the test-only server.cfg line `render_pass_long_ms=8` (3.3 `cfgextra`) · 0689 worker-0673q (2026-11-23) ·
`[L]` · [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.8.
Per second, the residency's bakes, pre-draw bakes, idle slices, yields, the near pairs' up-res and moving frames beside
pass 1s's predraw / idle / wake maxima (read only, any render.log with 0688's lines):
`python -I -X utf8 server/evidence/0703/persec0703.py <render.log> [--min-predraw MS] [--all]` · seconds · 0703, 2026-11-23 ·
`[L]` · terrain-bake-residency.md 8.1.

### 4.19 I want the server's `terrain` stage split into its parts (live logs, then offline per draw)
```
python -I server/evidence/0707/terrain_join.py <render.log> [-v]     # per second: stage, the terrain draw's cpu/gpu, unit shadow apply, rest
python -I server/evidence/0707/terrain_fit.py <render.log>           # the terrain draw's CPU = fixed + us per tile drawn
git worktree add --detach C:/c5-64bit-work/<t>/wt HEAD && git -C C:/c5-64bit-work/<t>/wt apply server/evidence/0707/diag0707.diff
python -I perf/quietlock/quietlock.py run --who <t> --what "diag sceneshot" -- python -I C:/c5-64bit-work/<t>/wt/server/tools/sceneshot/build_sceneshot.py --out C:/c5-64bit-work/<t>/ss
bash server/evidence/0707/bench0707.sh C:/c5-64bit-work/<t>/ss/bin/sceneshot.exe C:/c5-64bit-work/<t>/b RUNS REPEAT [1]   # 1 = GPU timestamps waited for
python -I -X utf8 server/evidence/0707/diag0707_stats.py C:/c5-64bit-work/<t>/b/run*/err.log --skip-rep0 [--by-frame]
```
The diff is diagnostic only (never merged): `C5_DIAG_0707=1` prints one `diag0707` line per scene draw (stages under a
pass timer, the terrain draw's parts and API counts, the units' `beginFrame` parts); `C5_DIAG_0707_REPEAT=N` draws each
shot's frame N more times after its comparison (the shots stay byte-equal). Edit `--who` in `bench0707.sh` and run it
from the repository root. The `terrain` stage of these lines includes the diagnostic cull replay (`dp_cullonly`):
subtract it. Offline the units' frame is begun under `units` (no unit-shadow merge from a version-9 bundle); live under
`terrain`. Build 7 min from nothing, a relink with `--c5render <ss>/c5render --common-objs <ss>/obj/common` 66 s; a
HUGE_0350 run with 30 repeats about 60 s · 0707 worker-0673w (2026-11-23) · `[L]` ·
[pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.12.

### 4.20 I want the server's model taps over a live drag span, and which instances its frames miss (the one-Present-old tap)
```
bash server/evidence/0709/rec0709.sh claim                              # pool claim (INST=c5h-c default, owner lead-0673; edit for yours)
bash server/evidence/0709/rec0709.sh prep                               # lay.py prep (B=, SH= acc-0691b defaults; NEW W0709/pp) + helper on, the reporter's options, lockstep_csv=1
AFTER=3000 FRAMES=600 bash server/evidence/0709/rec0709.sh run <TAG>    # TEST-ONLY recorder keys (RECROOT/<TAG> on F:), launch hidden, dismiss, autoplay off, drive0709.py drags, safe stop
bash server/evidence/0709/rec0709.sh end                                # server_cfg_extra unset, collect, restore by hash, verify --quick, leftovers, release
<cityscene.exe> texid <instance install> .dds > <W>/texid_all.tsv     # the t0 identities -> texture names (any render build's cityscene tool, e.g. C:/c5-64bit-work/<t>/render/tools/cityscene/cityscene.exe)
python -I server/evidence/0709/holes.py <RECROOT>/<TAG>[/.partial] <W>/holes.json --texid <W>/texid_all.tsv
python -I server/evidence/0709/entering.py <RECROOT>/<TAG>[/.partial] --texid <W>/texid_all.tsv
python -I server/evidence/0709/mtap.py <bundle>/modeltap/frame000000.mtap      # one line per tap: present, draws, set generation, targets
```
`holes.py` prints per member pair the camera shift (px), the visible dropped instances and the holes (instances of tap
P inside the view at camera P that tap P-1 lacks), split absent / passed in P-1, first entry / re-entry, farm / other,
by leading-edge distance and by camera speed; `entering.py` the entering instances covered by tap P-1 against the holes,
with their off-screen distance at P-1. One huge launch with 240 s of drags about 12 min; analysis about 20 s.
TRAP `[L]`: with a unit selected the overlay animates and the recorder writes 22.4 MB per frame: under exact lockstep
the game drops from about 19 to 4-5 frames per second inside the span (the camera moves 4-5 times farther per frame),
and 600 frames did not finish in 240 s of drags, leaving an INCOMPLETE bundle under `<TAG>/.partial` (its taps are whole).
The published count at the first drag on the huge save was about 320 and rose about 19 per second while dragging (pick
`AFTER` from that; `drive0709.py --render-log` prints it every 10 s) · 0709 worker-0673x (2026-11-23, c5h-c, EXE
4390913) · `[L]` · [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.13.
Since the 0709 fix (5.14) the server also draws the re-entering instances from an older tap; the TEST switch to turn
that off for an A/B is the environment variable `C5_RENDER_MODELS_KEEP=0` in the harness launch's environment
(it reaches server.exe like 3.14's variable; server.log: "models: the keep of instances the EXE culled ... is OFF by
C5_RENDER_MODELS_KEEP"; the `models:` line's `keep_on=0`). `holes.py` reads only the taps and counts the same holes either
way; the rule replayed on a bundle's taps in order: `python -I C:/c5-64bit-work/0709/impl/keep_sim.py [MAXAGE] [CULLEDBY]`
(outside git; edit its bundle path; `holes_kept` against `holes`, `kept_not_in_B_onscreen` must be 0) · 0709 impl-0709
(2026-11-23) · `[C]` · pan-coherence.md 5.14.
The A/B in one go and the picture proof (lead-0673, 2026-11-23):
```
INST=<inst> W0709=C:/c5-64bit-work/<t>/rec B=<build_server out> FRAMES=150 bash server/evidence/0709/rec0709.sh claim   # then prep, run keep1,
C5_RENDER_MODELS_KEEP=0 INST=<inst> W0709=... B=... FRAMES=150 bash server/evidence/0709/rec0709.sh run keep0          # (same env for all phases) end
<ss>/bin/sceneshot.exe --bundle <RECROOT>/keep0 --span <first_id>:<first_id+149> --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt --out <W>/k0-on
C5_RENDER_MODELS_KEEP=0 <ss>/bin/sceneshot.exe --bundle <RECROOT>/keep0 --span <first_id>:<first_id+149> --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt --out <W>/k0-off
python -I server/evidence/0709/cmp_onoff.py <W>/k0-on <W>/k0-off       # the frames on/off differ in, and the changed box
```
`first_id` is `scene.json` `frames.first_id`; the replay must start at the span's first frame (the keep needs the
earlier sightings). Two launches with 240 s of drags each about 22 min; each 150-frame replay with pictures 3-5 min
(about 300 MB of PNG + depth). Expect: exactly the hole pairs' P frames differ, inside a leading-edge band · c5h-a
(EXE 4390913) · `[L]` + `[C]` · pan-coherence.md 5.14.1.
The owner's eye check of a build on one visible instance (D237 acceptance: guard and cloak off, parked at 0,0; lead-0673,
2026-11-23):
```
export W237=C:/c5-64bit-work/<t>/eye B=<build_server out> SH=<shell build> STOCK_INST=<inst> PROD_INST=<inst>
python -I tools/harness/pool.py claim --owner <you> --name <inst> --hours 1 --ticket <t> --note "<grant>"
bash server/evidence/0673/d237/d237.sh prep            # lay.py prep (NEW $W237/pp-d237) + helper on + the reporter's [Renderer] set
bash server/evidence/0673/d237/d237.sh launch product  # prepare, launch --timing (uncapped), in game, dismiss, autoplay off, guard/cloak off, park
bash server/evidence/0673/d237/d237.sh stop product    # the harness's safe stop
bash server/evidence/0673/d237/d237.sh end             # lockstep CSV out, collect, restore by hash, verify --quick, leftovers, release
```
TRAP: if the player closes the game from inside it, the harness's stop never runs and the front end stays prepared:
`end` then fails (`restore exit 7`, verify: "front end left patched"). Run `python -I tools/harness/frontend.py -i <inst> restore`,
then `end` again. With one instance (STOCK_INST = PROD_INST) the second release prints an error; harmless · c5h-a (EXE
4390913) · `[L]` · pan-coherence.md 5.14.1.
The landmark vis listener on the EXE's views list and its box, read only (lead-0673, 2026-11-24): on a claimed instance in
game (stock is enough: frontend prepare, launch, ingame, dismiss as above; hidden, guard on), optionally with drags beside it:
```
python -I -X utf8 server/evidence/0709/listread0709.py --pid <pid from cli.py status> [--samples 400] [--gap-ms 5]
python -I -X utf8 server/evidence/0709/drive0709.py --instance <inst> --marks <W>/marks.tsv --until-file <W>/never --max-s 40 --tag lr   # in the background, for a moving camera
```
Prints the module base (the EXE loads relocated; every VA is rebased), the channel, every node of the list (THE NODE: object
0xDB13D8, function 0x6025F0) and the system box against the snapshot box over the samples, with the count of distinct
view-0 boxes. Exit 0 when the node is found · about 2 s still, 25 s for 4000 samples · build 4390913 only · c5h-a ·
`[L]` · landmark-renderer.md 7.7.

### 4.21 I want stock against product pictures of an interface element on one hidden workstation instance (a garrisoned city's flag)
```
python -I tools/harness/pool.py claim --owner <t> --name <inst> --hours 1 --ticket <t> --note "<why> (MC-B #<grant>)"
cp "<inst root>/<inst>/Documents/My Games/Sid Meier's Civilization 5/ModdedSaves/single/C5H-VP8P-HUGE_0350.Civ5Save" "<same folder>/C5H-<t>-HUGE_0350.Civ5Save"
SAVE=C5H-<t>-HUGE_0350 N=3 bash server/evidence/0673/g0679.sh <inst> run stock      # the instance's own DLL, nothing laid
bash server/evidence/0673/g0679.sh <inst> prep                                      # lay.py prep + helper on + the reporter's [Renderer] set
SAVE=C5H-<t>-HUGE_0350 N=3 bash server/evidence/0673/g0679.sh <inst> run product
bash server/evidence/0673/g0679.sh <inst> end                                       # collect, restore by hash, verify --quick, leftovers, release
```
`run` prepares the save, launches hidden and frame-capped, dismisses, sets autoplay off, then for k = 1..N runs
`garrison_look.lua` (the k-th visible garrisoned city, the active player's first; UI.LookAt on the main thread) and takes a
full-size shot to `$W679/shots/<arm>-<k>.png` (default `C:/c5-64bit-work/0673/g0679`), then the safe stop. Env: `B`, `SH`
the product build (default 0709's allb/srv and acc-0691b's shell build), `W679`. About 4 min per arm on the huge save, 10 min
in all with prep and end · c5h-new-a, build 26577625 (0679, 2026-11-24) · `[L]` · city-renderer.md 20. Compare the same
crop of both arms; the world under the flag differs (the server draws it), the interface must not.
TRAP: a shot taken 8 s after the LookAt can catch a frame without the interface (stock, Memphis): take N >= 3 and
check each shot has its interface before comparing.

---------------------------------------------------------------------------------------------------------------------


### 4.22 I want an A/B of a server environment switch on a hidden workstation instance (same build, the switch per launch)
0689's keys + drags + the wide zoom-out, uncapped (0703, 2026-11-23, `[L]`, about 7 min a launch on Tiny):
```
python -I tools/harness/pool.py claim --owner <t> --name <inst> --hours 2 --ticket <id> --address <addr> --note "<what>"
python -I server/evidence/0615/4914/saves_snap.py snap --instance <inst> --work <W>/saves
W0703=<W> OWNER=<t> INST=<inst> B=<build_server --out> SH=<shell build> ARMS="0 1 0 1" LAST_START=HH:MM bash server/evidence/0703/ab0703_ws.sh   # background
python -I server/evidence/0615/4914/saves_snap.py restore --instance <inst> --work <W>/saves
python -I -X utf8 server/evidence/0703/ab0703.py --dir <W> --marks <W>/marks.tsv fit0-1 fit1-2 fit0-3 fit1-4 [--list-ms 5] [--json OUT]
```
Each launch is prep, one launch per arm with `--env C5_RENDER_BAKE_FIT=<arm>`, end (restore by hash + verify). Out:
`<W>/<label>.{render,composite,vpproxy}.log`, `.lockstep.csv`, `marks.tsv`; ab0703.py prints per arm and pooled wake and ready -
publish by class and input kind (terrain-bake-residency.md 8.4). TRAP `[L]`: a `shell_composite.lockstep.csv` left in the
instance's Logs by an earlier run makes `run` exit 4 after prep; move it aside (not delete) and start again with a NEW W (prep
refuses an existing work folder). TRAP `[L]`: `C5H-4773-TINY_0018` is only on c5h-new-a; elsewhere pass `SAVE=C5H-0167-TINY_0008`.
On the laptop (inside MC-B's laptop grant): `SHIP=<a folder with bin/, proxy/ and composite/> ARMS="0 1"
[CASES=keys,drag,wide] [REPEATS=1] LOG=<file> bash server/evidence/0703/laptop0703.sh` (tb.py submit --env per arm; `ARMS="0 1 0 1 0 1"`
for PERFORMANCE.md's interleaved series); `bin` = `<build_server out>/bin`, `proxy/` = `<out>/proxy/bin/CvGameCore_Expansion2.dll`,
`composite/` = `<shell build>/bin/shell_composite.dll`; dupcap first: `MSYS_NO_PATHCONV=1 python -I perf/quietlock/quietlock.py run
--who <t> --what dupcap -- cmd /c "tools\capture\build.cmd"` (its bin folder is git-ignored, created by the command).
A server.cfg key A/B (not an environment switch), the drags + wide hold + city view windows of 0713, with the units' job and
record-ahead numbers per window (0753, 2026-11-25; scripts `[C]`, the series itself `[I]` until it has run): workstation
`INST=c5h-new-a B=<build_server out> SH=<shell build> W=<new dir> [ARMS="0 1 1 0 0 1 1 0 0 1"] [LAST_START=HH:MM] [SMOKE=1]
bash server/evidence/0753/ws_series0753.sh all` (the arm is `render_record_ahead=<arm>` through `vpproxy.ini
server_cfg_extra`, issue 0307; each launch under `quietlock run --take`, `SMOKE=1` registers without the hold for a
functional check), then `python -I -X utf8 server/evidence/0753/mk_jobs0753.py W` and `python -I -X utf8
server/evidence/0753/series0753.py W/jobs` (per window: ready - publish p50/p90, the composite's wait, busy, the join wait,
job_ms, record / execute ms, the `units` / `draw` / `draw_end` stages; ABBA pairs with 95 % t intervals and the ticket's
bar line). Laptop: `SHIP=<bin/ with the x64 DLL the purity list is bound to, <bin>/proxy/CvGameCore_Expansion2.dll,
<bin>/composite/shell_composite.dll, purity.tsv> OUT=<dir> bash server/evidence/0753/laptop_series0753.sh` (job0753.py =
capture_job.py with the pack's purity.tsv beside the x64 DLL; all jobs submitted at once, then fetched), then
`series0753.py OUT`. The x64 DLL of beta 4's pack (4c5da20e, cand6) is the one its purity.tsv names; a server.exe of
another build of the same pin runs with it (0753's SHIP: cand6's bin with the branch's server.exe).

### 4.23 I want the EXE's side of an exact-lockstep frame (head / E / wait / tail, E against S, the model capture per present)
From one run's lockstep CSV and composite log (0715, 2026-11-23, `[L]`, about 5 s): `python -I -X utf8 shell/evidence/0715/split0715.py
--csv <Logs>/shell_composite.lockstep.csv --log <Logs>/shell_composite.log --marks <marks.tsv> [--json OUT]` · frame-cost-exe-side.md 1.
Stock-like against product arms with the UpdateViews probe, key-off and server-slowed arms, in a quiet hour (`[L]`): build with
`shell/evidence/0715/m1-diag.diff` applied (diagnostic only, never merged), then `bash shell/evidence/0715/m1arms.sh claim|prep|arms|end`
(12 arms about 60 min; `ARMS=` picks; `<W>/STOP` ends after the running arm), then `python -I -X utf8 shell/evidence/0715/an0715.py <W>`
· frame-cost-exe-side.md 2-3. Read-back speed of mapped D3D11 dynamic buffers on a machine: `shell/evidence/0715/mapread/build.cmd`
through `quietlock.py run`, then `mapread.exe 300` (on a test machine: `tb.py submit --headed --no-src --file <exe>=mr/mapread.exe --
"%TESTBED_BIN%/mr/mapread.exe" 300`) · frame-cost-exe-side.md 1.6.
The copy's parts after 0719 (tail read, direct maps, cached bytes) from launch folders' composite logs, no game (0757, 2026-11-25, `[L]`,
seconds): `python -I -X utf8 shell/evidence/0757/split0757.py <launch dir> ...`. Read speed of write-combined mapped memory by
instruction (memcpy, SSE2, SSE4.1 / AVX2 streaming loads) and a worker reading while the map is open: `shell/evidence/0757/tailread/build.cmd`
through `quietlock.py run` (`cmd //c "shell\\evidence\\0757\\tailread\\build.cmd"`), then `tailread.exe 300 [-1|<adapter index>] [<buffer KB>]`
(about 10 s; on a test machine `tb.py --host <H> submit --quiet --headed --no-src --expect-min 1 --timeout 300 --file <exe>=tr/tailread.exe --
"%TESTBED_BIN%/tr/tailread.exe" 300 <adapter>`; the output is in the job's `job.log`) · frame-cost-exe-side.md 7.
The same table on a release candidate with NO build (0754, 2026-11-25, `[L]`): the stock-like arm is the product
layout with `[Renderer] ServerRenderer = 0`, the stock config.ini, and the candidate's composite loaded by
`crashlogs\shell.helper` (path of a copy outside MODS, its ini `mode=passthrough camera=0`, taps and hooks off,
`lockstep_probe=1`): `bash shell/evidence/0754/ws0754.sh claim|begin|series|finish` (9 launches about 6 min each,
Latin square; `ORDER=` resumes), then `python -I -X utf8 shell/evidence/0754/an0754.py <W>`; the laptop at rest
`OUT=<F:...> bash shell/evidence/0754/series0754.sh`, then `an0754l.py <OUT> --stock5043 <5043 jobs>` ·
frame-cost-exe-side.md 6, `shell/evidence/0754/README.md`.

### 4.24 I want stock's own fights recorded frame by frame (laptop), and videos made from them (retimed variants)
```
python -I -X utf8 tools/testbed/tb.py --host laptop submit --headed --name "<t> fights stock" --timeout 3000 --expect-min 25 \
    --file <worktree>/tools/capture/bin/dupcap.exe=cap/dupcap.exe --file <worktree>/tools/harness/native/bin=native \
    --file <vp-dll-dev skill>/assets=frontend-assets --file <a TB-TINY_0008 copy>=saves/TB-TINY_0008.Civ5Save -- \
    "%TESTBED_PYTHON%" server/evidence/0785/fight_capture_job.py --save TB-TINY_0008 --uncapped --instance tb1 [--fights melee,ranged,...]
python -I -X utf8 tools/testbed/tb.py --host laptop fetch <JOB> --out F:/c5-64bit-work/<t>/<JOB> --all
python -X utf8 server/evidence/0785/make_videos_0785.py info  --cap <fetched>/out/cap
python -X utf8 server/evidence/0785/make_videos_0785.py sheet --cap <cap> --case melee --out <sheet.jpg> --from 0 --to 8 --step 0.25
python -X utf8 server/evidence/0785/make_videos_0785.py video --cap <cap> --cuts <cuts.json> --out <folder> [--variants stock,V1,V2,V3]
```
A headed testbed job on capture_job's prep, launch, dupcap and restore by SHA-256 (4.15): five fights spawned by Lua
on a save copy (melee, ranged, mounted, city, gunmelee), Quick Combat sent off, a listener for RunCombatSim /
EndCombatSim / the damage events; colour JPEG frames (`--reduce 1.6`), the full PNGs deleted on the machine. TRAP: the
laptop has no `TB-TINY_0008` in `%TESTBED_SAVES%` (job 20261125-231805-9f49: FileNotFoundError at the save copy,
restored): ship it as `saves/` (a copy is `C5H-3600-TINY_0008` of c5h-c, sha256 b2ceb663...). The video maker needs
plain `python` (OpenCV); the cut points come from the sheets by eye; MP4s to the owner's deliverables folder, never git ·
about 15-25 min a job · 0785 (2026-11-25) · [quick-combat-extra.md](../05-graphics-composition/quick-combat-extra.md) 8.
The prototype's offline fight lengths (trigger_scene with `--quick-rate R [--quick-skip-lineup]`, branch
`impl-0785-quick-combat` only): `python -I server/evidence/0785/quick_scenes_0785.py --exe <ts>/bin/trigger_scene.exe
--game <pure_vp install> --db F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --work <dir> --csv <csv>` through
`quietlock.py run` (120 runs about 14 min on a loaded workstation) · quick-combat-extra.md 9.

### 4.25 I want a player's save with the player's extra mods on a hidden instance, small drags in an AI turn, and the model frames the server lacks (per frame)
```
bash server/evidence/0788/rep0788.sh claim                                     # INST (c5h-a), W (C:/c5-64bit-work/0788/ws: use your own)
SAVE_SRC=<copy on F: with SHA256.txt beside it> SAVE=<own name> bash server/evidence/0788/rep0788.sh begin
KEEP_DIFF=1 bash server/evidence/0788/mods0788.sh add                          # the player's extra mods (SRC=<the MC's copy>) into the instance's own MODS
PTAG=<a> B=<layout with bin/ and proxy/bin/> bash server/evidence/0788/rep0788.sh prep
C5_DIAG_0788=1 C5_DIAG_0788_MIN=10 PTAG=<a> SAVE=<own name> MODS="cp,vp,eui,<substrings of the extra mods>" LAUNCHOPT=--timing PRE_S=30 MAX_S=180 \
    bash server/evidence/0788/rep0788.sh run <TAG>                             # in the background; logs to W/<TAG>/
grep "diag0788: frame" <W>/<TAG>/render.log                                   # frames with >= MIN missing instances, by cause
bash server/evidence/0788/mods0788.sh remove ; bash server/evidence/0788/rep0788.sh end
```
A save made with mods whose `AffectsSavedGames` is 0 loads only with them (else the server's `save mods: ... game data
mismatch` and exit 3: `../03-dll-engine-boundary/save-mod-mismatch.md`); workers never read the player's folders, the MC
copies them. The `diag0788` lines need a server built with `server/evidence/0788/diag0788.diff` applied (diagnostic only,
never merged; runbook 1.2 in a scratch worktree), `C5_DIAG_0788=1` reaches server.exe through the launch's environment.
TRAPS `[L]`: the scene recorder on a late huge save stalls the load past `lockstep_hung_ms` (30 s: the game ends, exit 3) and
ends its span at the end turn (a redraw of one frame id), so it cannot record an AI turn; the human-turn poll of
`drive0788.py --until-human` (Lua `IsTurnActive`) once ended after 9 s (`UNTIL_HUMAN=0` drags for MAX_S). A launch with
load about 2.5-3 min, an AI turn of the 0788 save 25-45 s · 0788 impl-0788 (2026-11-26, c5h-a, EXE 4390913) · `[L]` ·
[lategame-model-flash.md](../05-graphics-composition/lategame-model-flash.md) 2.
### 4.26 I want the model store's rule replayed offline, its counters read, or a 5.4.8 layout with a branch's model server
```
python -I server/evidence/0789/store_sim.py <bundle with modeltap/> [--mode store|keep|none] [--refute K] [--cut N] [--flip N]   # holes and ghosts per tap pair; --cut N: every Nth tap empty, --flip N: every Nth tap without 30 % of its draws
python -I server/evidence/0789/storelines.py <render.log> [--all]           # the 'models store:' lines: per-second deltas, the seconds the residual-flash counter 'missed' rose, totals
python -I server/evidence/0789/modelsstage.py <render.log> [<render.log> ...]   # the 'pass 1s' models stage (max/mean ms per second), runs side by side: an indication on a shared machine
OUT=<dir on F:> bash server/evidence/0789/sizes.sh                          # sizeof the store's entry types (clang-cl x64), for the memory ceiling
bash server/evidence/0789/src548c.sh                                        # unpack 482ce16 + the branch's model files into C:/c5-64bit-work/0789/src548 (T, D in the script)
bash server/evidence/0789/mklayout0789.sh build                             # build_server.py --no-proxy from src548 on the 0.1b pins
bash server/evidence/0789/mklayout0789.sh layout                            # cand7's 5.4.8 layout with that server.exe/.pdb
bash server/evidence/0789/live0789.sh claim|begin|prep|run|end              # the live pass (0788's rep0788.sh phases, 4.25) with that layout; mods0789.sh as mods0788.sh
```
The offline replay uses the keys seen DROPPED in place of the published identity sets (a bundle has none): it differs
from live only in hand-over frames. The live pass compares arms by server.cfg `render_models_store=0|1` on the same save
and drive; `missed` counts the owner's flash in the server, `known` what the store held · 0789 impl-0789 (2026-11-26,
c5h-c, EXE 4390913) · `[C]` offline, `[L]` live · [model-store.md](../05-graphics-composition/model-store.md) 2, 4.
### 4.27 I want stock against product pictures of bridges at river crossings
```
python -I tools/harness/pool.py claim --owner <t> --name <inst> --hours 2 --ticket <t> --note "0806 bridges"   # a hidden game instance (D226)
cp <the huge T350 save> <instance's saves>/C5H-<t>-HUGE_0350.Civ5Save        # a copy with its own name, never the owner's save
bash server/evidence/0806/b0806.sh <instance> run stock                     # then: prep; run product; opts Section.Key=V; run <label>; collect <label>; end
python -I -X utf8 server/evidence/0806/cmp0806.py <W>/shots <out> stock product roads   # joins per crossing, side by side
```
About 4 min per arm. A stock picture is no reference for one bridge: the EXE lays routes and bends bridges at random per
session, so compare joins within one session; one arm's shot may come back grey (a capture artefact, take it again) · 0806
impl-0806 (2026-11-26, c5h-new-a, EXE 4390913) · `[L]` · [city-renderer.md](../05-graphics-composition/city-renderer.md) 13.11.

### 4.28 I want a live bridge check of RoadLayout on main's build
```
python -I tools/harness/pool.py claim --owner <t> --name <inst> --hours 2 --ticket <t> --note "bridges"   # a mods55 instance (c5h-55-*)
cp <instance saves>/VP55-HUGE_0350-B.Civ5Save <instance saves>/C5H-<t>-HUGE_0350.Civ5Save   # a copy with its own name
bash server/evidence/0808/b0808.sh <inst> prep                              # then: run base; opts Extras.EnableVisualAdditions=1 Extras.RoadLayout=1;
                                                                            # N=10 ... run roads; collect roads; end
```
About 8 min per arm. Main's server build carries the VP 5.5 DLL: a mods548 instance stops in 10 s on
`CvDatabaseUtility.cpp:350`. TRAPS: build `tools/harness/native` first in a fresh worktree; a launch that dies leaves the
front end patched: `python -I tools/harness/frontend.py -i <inst> restore` before the lay restore (else exit 7); a driver
still looping on a dead instance interleaves its lines into the same log · 0808 impl-0808 (2026-11-26, c5h-55-c, EXE
26577625) · `[L]` · [extras.md](../05-graphics-composition/extras.md) X0.2.

## 5. Measurement

A claim of speed follows [PERFORMANCE.md](../../docs/briefs/PERFORMANCE.md): grade A = at least 6 interleaved runs per
arm (12 for under 1 %), quiet lock held, builds of the current pins named on the command line (COMMON 18), timing
for a claim on the testbed (COMMON 23); a workstation number outside an MC window is "an indication". Every number
is `<id>@<version>` of `perf/ledger/registry.json`.

### 5.1 I want to run a long command under the quiet-machine rules, hold the lock, or end my stale job
```
python -I perf/quietlock/quietlock.py run --who <t> --what "<text>" -- <absolute command ...>
python -I perf/quietlock/quietlock.py wait
python -I perf/quietlock/quietlock.py run --take --who <t> --what "timing" [--minutes N] [--job-wait MIN | --overlap] -- <command>
python -I perf/quietlock/quietlock.py release --who <t>
python -I perf/quietlock/quietlock.py check --quiet --who <t>              # exit 0 QUIET, 1 NOT QUIET; before EACH arm
C5_JOB_OWNER=<t> python -I perf/quietlock/quietlock.py stop --pid <pid>
python perf/quietlock/linkplace.py status
```
`run` refuses (exit 2) while another owner holds the lock, exit 3 below 8 GB free commit; it checks once at start.
`run --take` also refuses at once (exit 2, "the queue is not empty and it is not your turn") while another waiter is
queued: first `quietlock.py queue --who <t> --what "<text>"`, then `quietlock.py wait --who <t> --timeout <seconds>`,
then the take (0754, 2026-11-25, `[L]`).
A hold lasts at most 30 min; `--overlap` takes at once and marks the hold DISTURBED. `check` is not purely read-only
(it clears stale waiters) · 0626, 0658 (2026-11-21) · `[L]` help · [perf/README.md](../../perf/README.md) "quietlock",
COMMON rules 16, 23.

### 5.2 I want seconds per AI turn (turn benchmark)
```
python perf/turnbench/turnbench.py --out C:/c5-64bit-work/<t>/tb-huge --game huge --x64-bin <pin layout>/bin --x86-dll <i0 pin>/CvGameCore_Expansion2.dll [--runs 6]
python perf/turnbench/turnbench.py --out <dir> --game huge --arm base=x64:<bin> --arm fix=x64:<bin> --arms base,fix
python perf/turnbench/turnbench.py --out <dir>/out --game huge --turns 355 --summarize-only
```
Takes the quiet lock itself; on the workstation only in an MC window (claims go to oldpc, 5.3) · Huge one run x64
about 60 s, x86 about 85 s; 6 rounds of both about 30 min · `<out>/turnbench.txt`, `turnbench.json` (`--ledger-root`
writes ledger records) · 0504/4809 (2026-11-20) · `[L]` help · [perf/README.md](../../perf/README.md) "turnbench",
[turn-time-baselines.md](../08-performance/turn-time-baselines.md).

### 5.3 I want a timing series on oldpc (quiet testbed), or a D178 pair of two DLLs
```
python tools/testbed/tb.py --host oldpc submit --quiet --expect-min <N> --name <t>-series --file <pin layout>/bin=x64/bin --file <i0 pin>/CvGameCore_Expansion2.dll=x86/CvGameCore_Expansion2.dll -- "%TESTBED_PYTHON%" tools/testbed/jobs/headless_series.py --arms x64-off,x86-off --runs 6 --rotate --to-turn 355 --x64-bin "%TESTBED_BIN%/x64/bin" --x86-dll "%TESTBED_BIN%/x86/CvGameCore_Expansion2.dll"
python -I perf/speedkit/pair.py --a pin=<pin DLL> --b on=<define-on DLL> --scenario M|H --runs 3 --turns 15 --issue <t> [--dry-run]
python -I perf/tbjobs/analyze.py C:/c5-64bit-work/<t>/oldpc-<JOB>/out --ref <arm>
```
`--quiet` needs `--expect-min N` (refused without, 0481); then `tb.py wait/fetch` (3.12). `perf/tbjobs/series.py`
is the per-arm form with `%C5H_PYTHON%` · 0042/4778 (2026-11-21), pair 4807 (2026-11-19) · `[L]` help ·
[tools/testbed/README.md](../../tools/testbed/README.md), [speedup-test-kit.md](../08-performance/speedup-test-kit.md).
The instruments' own cost on oldpc (0688, 2026-11-23, `[L]`, ~3 min): `tb.py --host oldpc submit --quiet --expect-min 10 ... --
"%TESTBED_PYTHON%" perf/pacing/bench_pacing.py --rounds 24 --exe-x86 ... --exe-x64 ...`.
Human seat against observer frame split on oldpc (5030, 2026-11-22, `[L]`, ~4.5 min per M run, 7.5 per H run; arm S4f's layout
from `bash perf/evidence/5030/build.sh`): `python -I -X utf8 perf/turnstudy/runner/submit.py run --host oldpc --scenario M|H --arms
S4f:f5030_human,S4f:f5030_observer --runs 3 --order williams --turns 10 --quiet --expect-min 50 --grade D --series-id <id> --issue
<t> --keep-going --no-rerun --timeout 5400`; analysis: `python -I -X utf8 perf/evidence/5030/framesplit.py <fetched job>/out --out
<dir>` (seconds).
Interface Lua per file and function with a human seat on oldpc (5034, 2026-11-25, `[L]`; ~6 min per M run of 12 turns, the
smoke's 3 turns 4 min): build the scratch layout with `bash perf/evidence/5034/build.sh` from a checkout of branch
`scratch-5034-luafile` (server+proxy 146 s, composite 4 min through the quiet lock; the harness natives need
`tools/harness/native/build.bat` in the submitting worktree, or the submit refuses), `python -I perf/evidence/5034/mk_arm.py`,
commit, then `python -I -X utf8 perf/evidence/5034/ts5034.py submit run --host oldpc --scenario M|H --arms S4x5034:x5034_human
--runs 3 --turns 12|6 --quiet --expect-min 25|35 --grade D --series-id <id> --issue <t> --keep-going --no-rerun --timeout 3600`
(`--dry-run` first: it prints the runner's own job line; the wrapper swaps in `ts5034.py series` only at the real submit);
fetch with `tb.py --host oldpc fetch <JOB> --out <dir> --all`; reader `python -I perf/evidence/5034/analyze.py <dir>/out/runs/<run>
[...] --json <file> --top 40` (per file and file:linedefined, self ms per turn interval, the hidden part; EXE sites as VA at 0x400000).
Decal loader before/after on oldpc, quiet and under disk load (0642, 2026-11-23, `[L]`, ~70 min): `tb.py --host oldpc submit --headed
--quiet --expect-min 100 --file <ship>/before=before --file <ship>/after=after -- "%TESTBED_PYTHON%" render/evidence/0642/job_0642.py
--arms before,after --conds quiet,load --rounds 2 --turns 3`; analysis `job_0642.py analyse <fetched out>`. `tb.py fetch` reads a
running job partly. A before-arm server build from a git-archive tree needs `deps/local-settings.ini` copied in.
Layer texture and style reads in bakes, before/after on oldpc (0698, 2026-11-23, `[L]`, ~48 min for 8 runs): build both arms with
build_server.py (the before arm from `git archive <main sha>` unpacked with `tar --force-local -xf ... -C ...`), ship them in
product_path's layout, then `python -I -X utf8 tools/testbed/tb.py --host oldpc submit --headed --quiet --expect-min 80 --name
"<id> ..." --timeout 14400 --file <ship>/before=before --file <ship>/after=after -- "%TESTBED_PYTHON%" render/evidence/0698/job_0698.py
--arms before,after --conds quiet,load --rounds 2 --turns 3`; fetch with `--include "*/render.log"`; then `python -I
render/evidence/0698/job_0698.py analyse <fetch>/out` (writes summary_0698.txt; `lines_capped` says when the first-use counts are lower
bounds). Trap: the residency's `bake_cpu_max_ms` on the terrain 1 s line is a running maximum since the field: use the run's last
value as its longest bake. Trap: Git Bash's tar reads `C:/...` as a remote host: use `tar --force-local`.

### 5.4 I want per-turn memory of a live game, or a release acceptance run into the ledger
```
python perf/memsampler/memsampler.py run --out C:/c5-64bit-work/<t>/mem-<name> --instance <inst> [--server] --autosave-kind post
python perf/memsampler/memsampler.py analyze <dir>
python -I perf/profilekit/acceptance.py run <inst> --save <save> --client "Stock 32-bit" --map "<map>" --config stock-32 --client-build "<build name>" --out F:/c5-64bit-work/<t>/runs/<name> [--turns 10] [--issue <t>]
python -I perf/profilekit/acceptance.py record F:/c5-64bit-work/<t>/runs/<name>
```
The sampler sits beside a running game and needs no lock. The acceptance run (D151: 10 turns of memory, one ACCEPTANCE
picture per turn) needs the instance claimed with the client installed; exit 8 = a box is open (3.8), then `record` ·
acceptance about 25 min stock, 12 min i2-rc1 · 0559 (2026-11-18) · `[L]` help ·
`--client-build` is refused before launch unless 1-80 characters with no comma or newline; a worktree without
`tools/harness/native/bin` (created by the command) ends with exit 3 at launch (build it first: section 0) · longer series: `--turns 50 --turn-timeout 3600`
with `--pins <rev>` naming the commit whose lock attests the DLL · 9079 (2026-11-22) `[L]` ·
A patched arm without an installer (9079's Increment 2.5, 2026-11-23 `[L]`): `provision.py set-dll <inst> --dll <32-bit fallback>`,
`lay.py prep <inst> --build <build_server out> --work <W> --composite-dll <shell build>/bin/shell_composite.dll --composite-ini <shell build>/bin/shell_composite.ini` (3.3),
`python -I -X utf8 perf/evidence/9079/write_options.py --descriptor <release descriptor .json> --out "<My Games>/VP64Options.ini"`
(the installer's file from the descriptor's option set), `lay.py helper <inst> --work <W> on`; after it `lay.py restore`, `set-dll --previous`.
A crash box ends the run with exit 8 and leaves the front end patched: `python -I tools/harness/frontend.py -i <inst> restore`
before `provision.py verify` · 9079 (2026-11-22) `[L]` ·
[memory-sampler.md](../08-performance/memory-sampler.md), [metrics-ledger.md](../08-performance/metrics-ledger.md) 8.1, 8.5.

### 5.5 I want to write, ingest or query ledger records, and rebuild the ledger page
```
python -I perf/ledger/ledger.py validate <run folder>
%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I perf/ledger/ledger.py ingest [--root R]
%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I perf/ledger/ledger.py query runs|outcomes|ingested
%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I perf/ledger/page/build.py --db F:/c5-64bit-work/ledger/index.duckdb --out F:/c5-64bit-work/ledger/page/
%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I perf/ledger/page/acceptance.py freeze|verify|show ...
```
`ingest` takes only runs with an `acceptance` block (D151); back up `index.duckdb` first. Two files are named
`acceptance.py`: `perf/profilekit/acceptance.py` (run, record) and `perf/ledger/page/acceptance.py` (freeze, verify,
show). Ingest, query and the page need the ledger venv (duckdb) · 0559/0564 (2026-11-18) · `[L]` help ·
[perf/ledger/README.md](../../perf/ledger/README.md), [metrics-ledger.md](../08-performance/metrics-ledger.md) 6.1, 8.1.

### 5.6 I want disk load records (diskwatch)
```
python -I -X utf8 tools/diskwatch/diskwatch.py run --drive F: --out C:/c5-64bit-work/<t>/diskwatch.jsonl --interval 5 --minutes 1
python -I -X utf8 tools/diskwatch/diskwatch.py report %USERPROFILE%/c5-ops/diskwatch [--since 2026-11-21T10:00] [--disk F:]
python -I -X utf8 tools/scenelib/diskwindow.py %USERPROFILE%/c5-ops/diskwatch/diskwatch-<yyyymmdd>.jsonl --from <iso> --to <iso> --disk F:
```
The standing instance (MC) writes `%USERPROFILE%/c5-ops/diskwatch`. A window with a record above 50 ms does not count
for a measurement on F: · 0615/4914, 0646 (2026-11-21) · `[L]` help · [tools/diskwatch/README.md](../../tools/diskwatch/README.md),
[d-drive-load.md](d-drive-load.md) 6, 12-13.

### 5.7 Studies and series (pointers)
| I want | Command | Explains |
|---|---|---|
| the turn-time study's four-way series | `python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S1i,S2i,S3,S4 --runs 12 --quiet --expect-min <N> ...` | [perf/turnstudy/README.md](../../perf/turnstudy/README.md), [turn-time-study.md](../08-performance/turn-time-study.md) |
| the capacity series | `python -I perf/capacity/series.py --root C:/c5-64bit-work/<t>/<set> --levels 4,8,12` (an MC decision) | [capacity-series.md](capacity-series.md) |
| the giant stress game | `python -I tools/stressmap/mappack.py apply -i <inst>` then `python -I tools/stressmap/calibrate.py <inst> --pack --out F:/c5-64bit-work/<t>/<run> --turns 30` then `mappack.py remove -i <inst>` | [giant-stress-game.md](../08-performance/giant-stress-game.md) |
| a sampling profile (attribution, never timing) | `python perf/sampler/sampler.py --out <dir> --target server.exe --focus "CvGame::update" -- python server/run_server.py <spec> --turns 50 --run <dir> --bin <bin>` | [perf/README.md](../../perf/README.md) |
| CPU topology / CPU per turn | `python perf/cputime/cputime.py topology` ; `turnbench.py ... --cputime` | [cpu-time-and-affinity.md](../08-performance/cpu-time-and-affinity.md) |
| a player's session logs per run and per second (older frames, waits, give-ups, server draw, fog particles, gaps, slow writes; read only, no build) | `python -I server/evidence/0687/p0read.py "%USERPROFILE%/Documents/My Games/Sid Meier's Civilization 5/Logs" --runs <yyyymmdd> [--csv C:/c5-64bit-work/<t>/seconds]` (`[L]` 2026-11-23, 0687). TRAP: the composite's logs are NOT in the run folder: `Logs\shell_composite.<yyyymmdd-hhmmss>.log` beside `Logs\VP64\`, named by the launch's date and time; join them to a run by that time or by the EXE pid in their first lines | [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 6 |
| the fog particle call's per-stage split (join's wait, step and its passes, emit, sort, upload, draws, GPU) on recorded bundles, in a scratch build outside git (0692, 2026-11-23, `[L]`) | `sh server/evidence/0692/run_series.sh build` then `run` (about 11 min, 64,800 frames) and `est` (about 1 min), then `python -I server/evidence/0692/split.py C:/c5-64bit-work/0692/runs/series.csv --est C:/c5-64bit-work/0692/runs/est.csv`; `run_series.sh clean` removes the scratch tree. Each step goes through quietlock; the timing points exist only in the copy | [fog-of-war.md](../05-graphics-composition/fog-of-war.md) 12 |
| the fog particle call before and after each 0692 cut (step 2): one bench build per revision in scratch trees outside git, on the workstation and on oldpc's quiet place (0692, 2026-11-23, `[L]`) | `sh server/evidence/0692/run_cuts.sh build` (about 2 min a revision; `ARMS="<rev>:<name> ..."` picks them, default base 8535c8787 and the cuts), `run_cuts.sh run 6` (5 arms about 12 min), `run_cuts.sh pack`, then `python -I -X utf8 tools/testbed/tb.py --host oldpc submit --quiet --expect-min 10 --name "<t> cuts bench" --file C:/c5-64bit-work/<t>/bench/pack=p0692 --file <render_exe>=p0692exe/CivilizationV_DX11.exe -- "%TESTBED_PYTHON%" server/evidence/0692/oldpc_cuts.py --bins "%TESTBED_BIN%/p0692" --exe "%TESTBED_BIN%/p0692exe/CivilizationV_DX11.exe" --out "%TESTBED_OUT%" --rounds 8 [--arms base,BCAD,final]` (5 arms x 6 rounds about 5 min there; the exes run on oldpc, which has no compiler), then `python -I server/evidence/0692/cuts_stats.py <cuts.csv> [--md OUT]`. TRAP: the bench holds `cuts.csv` open while it runs (a read is refused: wait for the end). The particle step's threads for a diagnosis: `C5_RENDER_PARTICLE_THREADS=1..16`; the off-screen cull off: `C5_RENDER_PARTICLE_CULL=0` (both read at the renderer's init) | [fog-of-war.md](../05-graphics-composition/fog-of-war.md) 12.5 |
| a stock against product frame-time pair on a quiet workstation, the same game Lua per-frame hook in both arms (0673, 2026-11-23, `[L]`) | inside MC-B's held quiet hour (START-REQUEST on desk 0136, start after its quiet post): `bash server/evidence/0673/quietpair/quietpair.sh claim`, `prep`, `run stock`, `run product`, `end` (env `STOCK_INST` c5h-b, `PROD_INST` c5h-a, `B`/`SH` the product build, `WQP` a NEW work folder; about 11 min for both arms), then `python -u -I -X utf8 server/evidence/0673/quietpair/an_pair.py <WQP> > <WQP>/an_pair.txt` (per arm: fps and percentiles; the product's pacing report and split). TRAP: do not pipe it into `tee ... | head`: head's exit kills tee and the per-arm lines are lost | [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.15 |
| the DLL's database cache pass per helper: a scratch timer build of the fork (`VP64_DBPROF`) against the pin's DLL, loads timed on quiet oldpc (5036, 2026-11-24, `[L]`) | `python -I perf/speedkit/kitbuild.py --fork <shared clone> --branch <scratch branch> --stubs C:/c5-64bit-work/keep/pins-5.5/deps-x64-release/stubs --out C:/c5-64bit-work/<t>/kb-instr --config release --define VP64_HIDDEN_EMBARK --define VP64_SPEED_DANGER --define VP64_SPEED_DANGER_DG7 --define VP64_DBPROF` (557 s, one LTO link), the layout by `server/build_server.py --src <keep src> --deps <keep deps> --config release --no-proxy --out C:/c5-64bit-work/<t>/pin-layout` (79 s), then `bash perf/evidence/5036/submit_5036.sh <layout>/bin <kb-instr>/dll/CvGameCore_Expansion2.dll 12 15` (the job about 3 min), `tb.py --host oldpc fetch <JOB> --out <dir> --all`, `python -I -X utf8 perf/evidence/5036/analyze_5036.py <dir>/out --host-alias oldpc --md <file>`. TRAP: kitbuild builds with only the defines you pass; the 0.1b pin's DLL carries the three marker defines, so pass them too or the arms differ (one 13-min build lost). A one-load functional smoke on the workstation first: `bash perf/evidence/5036/smoke_local.sh <layout>/bin <instr DLL>` | [speedup-p0-shares.md](../08-performance/speedup-p0-shares.md) "The DLL's database cache pass per helper" |

All `[L]` help 2026-11-21.

---------------------------------------------------------------------------------------------------------------------

## 6. Coordination

### 6.1 I want to use the queue (`tools/queue/ticketq.py`)
```
python -I tools/queue/ticketq.py list                                  # live tickets; --all for done too; --json goes BEFORE the verb
python -I tools/queue/ticketq.py show <t> [--text]                     # --text = the implementer's whole brief
python -I tools/queue/ticketq.py post <t> --by <me> --kind question|answer|note [--to MC] [--reply-to 408,409] [--blocking] --text "..."
python -I tools/queue/ticketq.py wait --for <me> --since <id that post printed> --minutes 30
python -I tools/queue/ticketq.py handback <t> --who <holder> --branch <b> --commit <sha> --acceptance-file <file> --research <files...> --lost "<one line each, or none>" --actual-h <hours>
python -I tools/queue/ticketq.py publish <t> --ref <branch>@<sha> --by MC   # manual override (a branch; switch off)
python -I tools/queue/ticketq.py claim <t> --account B --who dispatcher-B    # prints the reality check after the claim
python -I tools/queue/ticketq.py check <t>                             # the reality check alone (implementer, first step)
python -I tools/queue/ticketq.py return <t> --by <holder> --why "<one line>"   # holder: stale ticket -> withdrawn
python -I tools/queue/ticketq.py accept <t> --by MC --note "merged <sha>: ..."
python -I tools/queue/ticketq.py done <t> --commit <sha> [--actual-h H]   # MC, in the merge before the commit: file done + staged, accepts
python -I tools/queue/ticketq.py sync --dry-run                        # what auto-publish from main would change
python -I tools/queue/ticketq.py sync --enable --by MC                 # the switch; then next/claim/wait sync first
```
Auto-publish (one-off 9085): with the switch on, an issue file on `main` that is ready for an agent is on the queue
within about 30 s and leaves it when it stops being ready; `depends_on` is read live, so a ticket whose blockers are
done is offered with nobody marking it. `done` keeps a squash merge from leaving the file at `ready` · `[L]` sync,
next, claim, check, done on a throw-away repository and store 2026-11-24 (9085) · [tools/queue/README.md](../../tools/queue/README.md)
"Auto-publish from main".
Every answer names each question it answers with `--reply-to`, or the question stays "waiting" (D115). A long `wait`
runs as its own call with the Bash tool's `run_in_background`, never with `&` or output to NUL (refused, exit 2);
the dispatcher's short loop runs in the foreground (`--minutes 2.8`). A ticket whose work is merged but still claimed:
the holder runs `handback` only and the MC `accept`s; `release` is for abandoned work (it makes the ticket ready
again). After widening a ready ticket's `touches` or `machines`, nothing changes until a new `publish` (with
auto-publish on: until the change is on `main` and the next sync, about 30 s).
`capacity`, `owner` and `next` write (they expire leases); `list`, `show`, `places`, `capabilities`, `accounts`,
`desk`, `addresses` only read · about 0.3 s · store `%USERPROFILE%/c5-ops/queue.sqlite` · daily ·
`[L]` help, list, show 2026-11-21 · [tools/queue/README.md](../../tools/queue/README.md),
[epic-task-queue.md](epic-task-queue.md), [docs/briefs/dispatcher.md](../../docs/briefs/dispatcher.md).
The MC's desk wait (a loop over `ticketq.py wait --for MC --minutes 600`) is not in the repository; grep its output
first: `grep -nE 'handback|question|MERGE-REQUEST|FOR YOU' <output file>`.

### 6.2 I want to check the issue files, or rebuild the tracker page
```
python -I -X utf8 tracker/build.py --check [--owners]
python tracker/build.py && git add tracker/index.html tracker/closed.js        # cmd/PowerShell: tracker\rebuild.cmd
python -I -X utf8 tracker/server/server.py status                              # MC: start | restart | stop | log --since 2h
```
`--check` writes nothing: 2.6-2.9 s for 1,215 issues, exit 1 on a parse error; from 2026-11-22 a done ticket without
`actual_h` is an error. The page is rebuilt on every issue change; the tracker server (`http://127.0.0.1:8642/`)
rebuilds within 2 s · 0660 (2026-11-21) · `[L]` check, status · [tracker/README.md](../../tracker/README.md),
[tracker-server.md](tracker-server.md).

### 6.2a I want to add or edit a release on the tab Releases
```
cp tracker/releases/beta4.json tracker/releases/<id>.json       # a new release: copy one, then edit by hand
python -I -X utf8 tracker/build.py --check 2>&1 | grep "releases/"     # nothing printed = the file is clean
python -I -X utf8 -m unittest discover -s tracker -p "test_build.py" -k Release
```
A release file names only its goals, each goal's tickets as a tree (a child lands before its parent), short labels and
the release steps; never a ticket's state or blocker (the page derives them). A broken file is a warning and an error
box on the tab, never a failed build. The server picks a change up within 2 s; one with this change's `live.py` is
needed for that (restart after the merge). Commit with the page (`tracker\rebuild.cmd`) · about 3 s · one-off 9083
(2026-11-23) · `[L]` check, tests · [tracker/README.md](../../tracker/README.md) "Releases".

### 6.3 I want to hand back, or send a merge request
- Task hand-back: `ticketq.py handback` (6.1) with `--acceptance-file`, `--research`, `--lost`, `--actual-h`; push the
  branch; `python -I tools/visualcheck/visualcheck.py list --issue <t> --result undescribed` prints nothing; COMMON
  rule 31's monitor line (6.5); rule 32's runbook line in "time lost".
- MERGE-REQUEST (lead to MC): the template in [MASTER-COORDINATOR-HANDOFF.md](../../docs/MASTER-COORDINATOR-HANDOFF.md)
  section 4.6, posted with `ticketq.py post <epic> --by lead-<epic> --kind note --to MC --text "MERGE-REQUEST ..."`;
  its gates, run on the branch head: the last line of
  `python -I tools/export/check.py --rev HEAD --since main --require-terms --no-env-terms` (about 45 s; exit 0 merge,
  1 private content, 2 setup wrong) and, when `tools/` changed,
  `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/harness/tests/test_safety_static.py`
  (pytest only: `unittest discover` runs 0 tests and prints OK) · `[L]` export gate 2026-11-21 ·
  [upstream-export.md](upstream-export.md).

### 6.4 I want to check every commit of my branch for private details before pushing
Grep every commit, not only the final diff; any hit means squash (the MC decides). Machine names are Windows default
names in capitals with a digit; the words `desktop-`, `laptop-` and `LAPTOP-REQUEST` are not hits:
```
git log -p --no-merges main..<branch> | grep -nE '(^|[^A-Za-z0-9_])(DESKTOP|LAPTOP|WIN)-[A-Z0-9]{7,11}([^A-Za-z0-9_]|$)' | grep -E '(DESKTOP|LAPTOP|WIN)-[A-Z]*[0-9]'
git log -p --no-merges main..<branch> | grep -niE '[a-z]:[\\/]+users[\\/]+[a-z0-9]' | grep -viE 'users[\\/]+(public|default)\b'
P=$(awk '!/^#/ && NF>=2 {print $2}' "$USERPROFILE/c5-ops/export-private-terms.txt" | paste -sd'|'); git log -p --no-merges main..<branch> | grep -niwE "($P)"
```
Read the hits of the third line, do not count them (the repository URL carries the handle). Never paste raw matches
into a note. `grep -F -f <(...)` crashes Git Bash's grep: use the `-E` form · seconds · `[L]` on main's last 20
commits, 2026-11-21 · rules from `tools/export/check.py` (user-path, machine-name, terms) and
[upstream-export.md](upstream-export.md) 2.2.
A branch that merged main also needs its merges read, per parent (the MC's check is "every commit, -m per parent"):
`git log -p -m --format='COMMIT %h %p' main..<branch>` through the same patterns, then sort each hit by its commit. A
hit in a merge commit's diff against one parent, in a file the branch's own (`--no-merges`) commits never touch, is
main's own text: say so in the request. A hit in an own commit means SQUASH · seconds · 0615 (2026-11-21,
epic-0615-lw-2: 0 own-commit hits; 113 merge-diff hits, all in main's tracker files) · `[L]`.

### 6.5 I want to stop my own background watches (COMMON rule 31)
```
python -I -X utf8 ops/monitors/orphan_monitors.py list --mine <your worktree or scratch folder>
python -I -X utf8 ops/monitors/orphan_monitors.py stop --mine <your worktree or scratch folder>
```
`stop --dead` across the machine is the MC's (MC-B on the MC's word). Stops only `tail.exe`, by exact pid · about
1 s · `[L]` list 2026-11-21 · the script's docstring and COMMON rule 31.

### 6.6 I want to size, then remove, a worktree; clean a branch
```
du -sh <worktree>
git -C <worktree> status --porcelain --ignored | sed -n 's/^!! //p' | (cd <worktree> && xargs -d '\n' du -sh 2>/dev/null) | sort -h | tail
git worktree remove <path>
git branch -d <branch>
```
`git worktree remove` deletes git-ignored build output without asking (26 agent worktrees held 195 GB, 2026-11-21): size
it first. Merge alone, verify the merge's two parents in a second command, only then clean up. Never
`git checkout --`, `reset --hard` or `clean` in a worktree you do not own · `[L]` sizing on this worktree, 2026-11-21.

### 6.7 I want hours per issue, the agents tree, or a query on the session log (read only)
```
python -I ops/measured.py [--days]
python -I ops/agents_view.py --text --no-tracker --out C:/c5-64bit-work/<t>/agents.html
python -I -c "import sqlite3,sys; c=sqlite3.connect('file:'+sys.argv[1]+'?mode=ro',uri=True); [print(*r,sep='\t') for r in c.execute(sys.argv[2])]" "$USERPROFILE/c5-ops/sessions.sqlite" "SELECT issue_id, role, started_at FROM sessions ORDER BY started_at DESC LIMIT 3"
python ops/report.py --no-timings --no-graph > report.md
```
`ops/db.py --sql` opens the database read-write and migrates it: prefer the `mode=ro` form. `ops/report.py` rewrites
the timings table unless `--no-timings` · seconds · `[L]` 2026-11-21 · [ops/README.md](../../ops/README.md),
[session-log-hooks.md](session-log-hooks.md).

### 6.8 I want the time for a log line
`date '+%Y-%m-%d %H:%M %Z'` (PowerShell: `Get-Date -Format 'yyyy-MM-dd HH:mm'`); never estimate a time (COMMON rule 11).
Project times are EST.

### 6.9 I want the upstream export (MC; nothing leaves without the owner's word, D168, D174)
`python -I tools/export/check.py --list`, `--rev main --require-terms --summary`;
`python -I tools/export/export.py --target <folder> --rev <rev> --dry-run` · `[L]` help ·
[upstream-export.md](upstream-export.md) 2-3, 9, 13.
The export package for the owner's read, and the two public commits made locally (4707, 2026-11-25), one command:
```
export C5_EXPORT_AUTHOR="<the owner's name> <<id>+<login>@users.noreply.github.com>"   # never committed
python -I -X utf8 tools/export/package.py --rev <rev> --dll-branch export-e5-dll-one --fetch --out C:/c5-64bit-work/<t>/package
```
Needs the fork's `export-e5-dll-one` (0762) and `Release-5.5`; nothing is pushed, tagged or written to the ledger · about
51-70 s · `<out>/summary.txt` (the two commit hashes, checks, NOT AN EXPORT if the check found content), `owner-read.md`,
`pr.md`, `commit-1.txt`, `commit-2.txt`, `files.tsv`, `stage/64bit/`, the branch `x64-export-<rev7>` in `<out>/fork` (push
disabled) · 4707, 2026-11-25 · `[L]` · [upstream-export-package.md](upstream-export-package.md) 3-4. On the beta 5 cut C5
(0759): the two printed hashes go to `tools/installer/build_installer.py --source-commit <commit-2> --source-dll-commit
<commit-1>` (D261); the branch is never remade before 0760 pushes it (another date gives other hashes).
The clean build of that package's commit 2 (4706's check; 0773, 2026-11-26): a local clone of `<out>/fork` checked out at
commit 2 (push URL disabled; a plain `git archive` copy is refused, since the builders take `--unlocked` only for a git
source), then in its `64bit/`, holding one LTO link place for the whole build (the export has no `perf/quietlock`):
```
python -I tools/export/cleanbuild.py --out <new folder> --exe <golden 4390913 DX11 EXE> --exe <golden 26577625 DX11 EXE>
  --db <the descriptor's test_db Civ5DebugDatabase.db> --hand-review 26577625=<exe_tables>/26577625/hand-review.tsv
  --third-party <folder with deps/third-party.lock's 3 archives> --offline --repo-commit <main commit> --unlocked
```
About 10 min (609 s on 2026-11-26) · `cleanbuild: OK <out>/release` and its `SHA256.txt` · `[L]` ·
[upstream-export-clean-build.md](upstream-export-clean-build.md) 2, 4, 11.
