# The x64 DLL's build configurations and the whole-program link place (ticket 0159, decision D107)

Ticket 0159, account B, 2026-11-13. Carries out sections 4A and 4B of
`research/07-architecture-decisions/architect-2026-11-13-linker-memory.md` (the owner's order of 2026-11-13 about 22:20 EST).
Usage is in `tools/x64-dll/README.md`, section "The fast configuration and the whole-program link place". Marks: `[C]`
confirmed in a file or a dump, `[L]` measured live, `[I]` inferred.

## 1. The three configurations

(2026-11-17, ticket 4801: a fourth, marked configuration `pgo-instr` and a profile input for release: section 4a.
2026-11-24, ticket 0513: a fifth, `release-pgo` = release with the locked profile of deps/dll.lock; `release --profile`
is refused since: section 4b.)

| | debug | release | fast (new) |
|---|---|---|---|
| compile | `/Od /Oy-` | `/Ox /Ob2 /Zo -flto` | `/Ox /Ob2 /Zo` |
| defines | shared + `VPDEBUG` | shared + `STRONG_ASSUMPTIONS NDEBUG VPRELEASE_ERRORMSG` | release's |
| link | `/DEBUG /LTCG` (no bitcode, so no LTO) | `/DEBUG /LTCG /OPT:REF /OPT:ICF` | `/DEBUG /OPT:REF /OPT:ICF` + marker |
| whole-program optimisation | no | yes, full LTO, one thread | no |
| link place (section 3) | no | yes | no |
| for | debugging | pins, timing, shipping | functional work only |

[C] `tools/x64-dll/build.py` `cl_args`, `link_lines`, `PREDEFS`, `WHOLE_PROGRAM`, `MARKED_CONFIGS`; the test
`tools/x64-dll/tests/test_config.py` holds release's and debug's command lines as they were before 0159, so their DLL
bytes do not move (release was also rebuilt, section 4).

Note on `/LTCG`: lld-link runs LTO whenever a bitcode object is among its inputs; `/LTCG` itself is accepted and
otherwise ignored. So fast must have no bitcode input at all: its objects are compiled without `-flto`, and the
closed-library stubs, `CvGameDatabase_x64` and `CvLocalization_x64` are native code in every configuration
(`/O2` or `/Od`, no `-flto`, [C] `closedlib-stubs/build_stubs.py`, `cvgamedatabase/build_db.py`,
`cvlocalization/build_loc.py`). A fast build links a release build's `deps\stubs` folder.

### 1.1 How a fast DLL is told apart

- **Marker section** `.vpcfg` in the DLL: 64 bytes, `VP-BUILD-CONFIG=fast; not for timing (tools/x64-dll/build.py)`,
  from a generated `<out>\gen\vpconfig.c` (`#pragma section` + `__declspec(allocate)`), compiled with `/Brepro` in the
  output folder with relative names, kept under `/OPT:REF` by `/INCLUDE:vp_build_config`. Data only; no code refers to
  it. [L 2026-11-13] the fast build of the pin has sections `.text .rdata .data .pdata .retplne .tls .vpcfg .rsrc
  .reloc`; the pin's release DLL (74588135...) has the same list without `.vpcfg`.
- **Records**: `build-summary.json` `"config": "fast"`, `"for_timing": false`, `"whole_program": false`; `build.log`
  prints a NOTE line.
- **Layout name**: a layout holding a fast DLL ends in `-fast` (`x64-04b1211b-pa803776-fast`).
- **Refusal**: `build.py` `timing_refusal(path)` returns the reason when any of the three says fast (marker; a
  `build-summary.json` or `build.json` beside the DLL whose config is fast; one of the DLL's two parent folders named
  `*-fast`). `perf/turnbench/turnbench.py` calls it for every arm before the lock and refuses with
  `arm <name>: refused: ...` (exit 1); test `perf/turnbench/tests/test_turnbench_fast_refusal.py`. The frame-rate tools
  (`proxy/tools/live/idle_window.py`, `fps_table.py`) are to adopt it through ticket 0157's shared check in
  `tools/harness/framecap.py` once 0157 has merged (not done here).

## 2. Link peak memory and time per configuration

Measured by `build.py` itself from 0159 on: after the linker exits, `K32GetProcessMemoryInfo` on its still-open process
handle gives `PeakPagefileUsage` (peak private bytes, the peak of Task Manager's "commit size") and `PeakWorkingSetSize`;
both are in `build-summary.json` (`link_peak_private_mb`, `link_peak_working_set_mb`) and on the `lld-link rc=` line of
`build.log`. Source: a plain copy (`git archive`) of the pin 04b1211b8 (fingerprint matches), the stub libraries of
the pin's own build (`D:/c5-64bit-work/9045/bF/deps/stubs/*.lib`, copied), `--shorten --jobs 6`,
`SOURCE_DATE_EPOCH=1794587302` (the time of tool commit a803776cf, so __DATE__/__TIME__ equal the pin's), tool commit
of this branch, on the workstation while other teams ran games and links (times are indications, not timing results:
COMMON rule 23; memory peaks do not depend on load). Debug was linked against the release stubs (only its link was
wanted).

| Configuration | When (EST) | Folder | Link time | Link peak private | Link peak working set | Compile wall (PCH + 172 TUs) | DLL |
|---|---|---|---|---|---|---|---|
| release | 2026-11-13 23:55 - 11-14 00:05 | C: | **470.7 s** | **13,340 MB** | 8,643 MB | 13 s + 92 s | 23,616,000 B, **74588135...4570 = the pin** |
| fast | 2026-11-14 00:08-00:12 | C: | **1.9 s** | **466 MB** | 612 MB | 18 s + 217 s | 23,450,624 B, 744A2E9C... |
| fast | 2026-11-13 23:16-23:25 | D: | 15.1 s | 433 MB | 597 MB | 183 s + 323 s | 23,450,624 B, 7186F758... |
| debug | 2026-11-14 00:06-00:07 | C: | **1.9 s** | **531 MB** | 642 MB | 22 s + 59 s | 27,272,192 B, B4F87A4F... |

[L] `C:/c5-64bit-work/0159/{release,fast,debug}/build.log` and `build-summary.json` (deleted at hand-back; the
numbers are here). Findings:

- **The release link needs 13.3 GB of private memory at its peak** (the Architect's live reading of 12.7 GB on
  2026-11-13 22:19 was close to the peak) and about 8 minutes; fast and debug links need about 0.5 GB and 2 s. Fast
  takes the release link's memory need down by a factor of about 28 and its time by about 250.
- **Release bytes are unchanged by 0159**: the release build of the pin at this branch, with the pin's stubs and
  epoch, is byte-equal to the pin's DLL 74588135... [C, SHA-256].
- **The two fast DLLs differ in 20 bytes only**: the PE header time stamp (4) and the debug directory's PDB identity
  in `.rdata` (16); code and data are equal. The only difference of the two link response files is the stubs' relative
  path (`..\..\9045\bF\deps\stubs\*.lib` against `..\stubs\*.lib`); the PDB records it, and under `/Brepro` the time
  stamp and PDB GUID are hashes of the output, so the identity moves [C, byte comparison; cause I]. A repeat build that
  gives the stubs the same relative path gives the same bytes (as the release row shows).
- **The D: drive was the bottleneck of the compile on 2026-11-13 evening**: PCH 183 s and 172 TUs 323 s on D: against
  18 s and 217 s on C: for the same fast build (the D: build ran while D: served several instances and an 8 GB copy;
  the release compile on C: was even 92 s with fewer other jobs). One data point each, not a measurement series [L].
- The first take of the place waited 30 s although no one held it: the first look at the state file on D:
  (`D:/c5-64bit-work/locks`) took that long while D: was saturated (my own scratch deletion ran at the same time) [I].

## 3. One whole-program link at a time

`perf/quietlock/linkplace.py`. State `<lock dir>/lto-link.places.json` (holders), events
`lto-link.places.log.jsonl` (take, release, stale), both written under a create-exclusive mutex file
(`lto-link.places.mutex`, stale after 30 s), the file replaced whole. Lock folder as the gates: `C5_QUIETLOCK_DIR`, else
`C:/c5-64bit-work/locks` (*corrected 2026-11-21, one-off 9080: `perf/quietlock/quietlock.py` `DEFAULT_LOCK_DIR`, ticket
0180, D133; was "`D:/c5-64bit-work/locks`"*).

- Places: 1 (`C5_LTO_LINK_PLACES`). Kept at 1 after the memory upgrade of 2026-11-14 until a measurement shows a need
  for 2 (ticket text).
- A holder record: token, `pid` and creation time of the process that took the place (`build.py`), owner
  (`gate.job_owner`: `C5_JOB_OWNER`, `C5_QUIETLOCK_WHO`, `C5_POOL_OWNER`), what, output folder, start, command line, and
  `linker` = pid and creation time of `lld-link` once started (`Place.add_pid`). Stale when none of its processes runs
  (a pid with another creation time counts as gone): the next look removes it, prints "the holder ... is gone; its place
  is free again" and logs `stale`. Recording the linker keeps the place taken when `build.py` is killed but its
  `lld-link` child goes on holding gigabytes [I: on Windows a child outlives its killed parent].
- Waiting: in place, a progress line naming every holder (pid, owner, since when and for how long, linker pid, what,
  output folder) when the holders change and every 5 minutes, polled every 15 s; gives up after 60 minutes
  (`--link-place-wait`, `C5_LTO_LINK_WAIT_MIN`) with "gave up after N min waiting for a whole-program link place: ...;
  nothing was linked; ask the holder's owner, or link later (`build.py ... --link-only` reuses the compiled objects)";
  `build.py` exits 4 then. No order among waiters: whoever looks first after a release takes the place.
- Memory floor (COMMON rule 16): when a place is free, free commit is checked against `gate.heavy_floor_gb()` (8 GB);
  below it the link waits too (the line names the three largest processes by commit), without taking the place. An
  override (`C5_MEMORY_OVERRIDE` or `C5_QUIETLOCK_OVERRIDE`, "WHO: WHY") lets it start, logged by `gate.py`.
- Trouble never blocks a link: no lock folder warns and links without a place; an unreadable state file is treated as
  empty and rewritten; an error writing the file warns and links.
- Shown by `python perf/quietlock/linkplace.py status` and at the end of `quietlock.py check`.
- Only `build.py` takes it. Other whole-program links (none known: `server/build_server.py`, `proxy/`, the stub and
  library builds have no `-flto`/`/LTCG` [C, grep, Architect's note]; the 32-bit builds were not searched) do not.

Tests (2026-11-13): `perf/quietlock/tests/test_linkplace.py` 15 passed (take and release; a second link waits with a
progress line naming the holder's pid and owner and gives up with the message; it takes the place when the holder
releases; a killed holder frees the place; a reused pid counts as gone; a running linker keeps the place after its
build died; two places admit two; the memory floor waits and gives up, its override is logged, a met floor takes at
once; no lock folder; a garbled state file; the status CLI). `tools/x64-dll/tests/test_config.py` 11 passed (flags,
link lines, marker found in a real lld-link output and absent without it, `timing_refusal`'s three signs, only release
takes a place, a timeout exits 4 with the message).

## 4. Release of the pin rebuilt, and d10 save identity of fast against release

**Release rebuilt** (section 2): byte-equal to the pin's DLL 74588135... [C].

**d10 save identity of fast** [L, 2026-11-14 00:13:15-00:52:09 EST, 2,333 s, on C:]: `python -I tools/d10/run_d10.py
--out C:/c5-64bit-work/0159/d10-fast --x86-dll <pins_dir>/dlls/i0ship-26234371/CvGameCore_Expansion2.dll
--srv-bin C:/c5-64bit-work/0159/x64-04b1211b-pa803776-fast/bin --huge-save D:/c5-64bit-work/9020/inputs/9020-HUGE_0350.Civ5Save
--order-exception none --d18-x86 yes --d18-x64 yes --jobs 4`, the layout = the pin layout's `bin/` with the fast DLL
7186F758... (the D: build; the C: build differs from it only in the PDB identity, section 2). Result **PASS**: tiny,
standard and huge pass parts 1, 2 and 3 with only the version-string exception, and x86 direct = x64 direct with
**30,666 / 351,496 / 683,123 rows**, the same counts as one-off 9045's run of the release pin against the same x86 DLL
(`tools/d10/evidence/9045/d10-final-report.txt`). Both builds equal the same x86 DLL under the same test, so their game
results are equal on these games.

**Direct comparisons with 9045's run of the release pin** (its folder `D:/c5-64bit-work/9045/d10-final`, read only):

- huge, x64 continuation of the x86 turn-375 save (an input equal in both runs: its x64 resaves differ only in the
  pregame's file path), `statscmp.py` release against fast: **14 tables, 357,454 rows, IDENTICAL** [L].
- huge, x64 resaves of the turn-350 input and of the x86 turn-375 save, `savecmp.py` release against fast: differences
  only in the pregame, which holds the loaded file's path (`...\9045\d10-final\inputs\...` against
  `...\0159\d10-fast\inputs\...`, one byte shorter); header and body equal [C].
- huge, x64 resave of each run's own x64 turn-375 save: body differs in 2 bytes (offsets 224,114 and 224,605, values
  0x0E/0x0F and 0x09/0x08). That input is regenerated by every run, and two runs of one DLL write saves that differ in
  uninitialised fields (`d10-save-identity-test.md` 4b.3); the gates compare resaves of one file and passed in both
  runs. [I: run-to-run noise of the regenerated input, not a configuration difference; the two bytes are not
  attributed to a field.]

So, on the current pin, the fast configuration's game results equal release's. A parity or save-identity result that
an epic hands in as its proof is still confirmed once on the release configuration (COMMON rule 18, D107).

## 4a. The fourth configuration `pgo-instr` and `release --profile` (ticket 4801, epic 0504 P1, 2026-11-17)

Added by ticket 4801 (MC grant #4429 for `build.py` and `test_config.py`); the measurement and its numbers are in
`research/08-performance/speedup-p1-pgo.md`. [C] `tools/x64-dll/build.py` (`PGO_INSTR`, `pgo_cl_args`,
`compile_pgo_objects`, `dll_profile`, `check_pgo_args`), `tools/x64-dll/tests/test_config.py` (23 tests).

| | release (unchanged) | pgo-instr (new) | release + `--profile FILE` (new input) |
|---|---|---|---|
| compile | `/Ox /Ob2 /Zo -flto` | release's + `/clang:-fprofile-generate` | release's + `/clang:-fprofile-use=<out>/gen/vp64.profdata -mllvm -pgo-warn-missing-function` |
| link | `/LTCG /OPT:REF /OPT:ICF` + 0373's DSE limit | release's + `/LIBPATH` of `clang_rt.profile-x86_64.lib`, the runtime, the flush object, the marker | release's + the `.vppgo` object |
| whole-program, link place | yes | yes (`WHOLE_PROGRAM`) | yes |
| marked, refused for timing | no | yes: `.vpcfg` `VP-BUILD-CONFIG=pgo-instr; instrumented: not for timing, pins or shipping`, `build-summary.json` `config: pgo-instr`, `for_timing: false`, `instrumented: true`, layouts named `*-pgo-instr` | no: `.vppgo` `VP-BUILD-PROFILE=<sha256>` names the profile (`dll_profile()`); `build-summary.json` `profile.sha256`, `dll_profile`, `pgo_diagnostics` |
| link peak private (2026-11-17) | 13,357 MB | **16,323 MB** | **14,662 MB** |
| for | pins, timing, shipping | training a profile only | P1's measurement; adopted by ticket 0513 as the configuration `release-pgo` (section 4b; *2026-11-24: `release --profile` is refused since, same flags under the new name*) |

- **Release's bytes do not move**: without `--profile` the compile and link lines are the ones before 4801
  (`test_release_compile_line_has_no_pgo_flag_without_a_profile`, `test_release_link_lines_unchanged`), and no PGO
  object is linked.
- **The flush object** `tools/x64-dll/pgo/pgo_flush.c` (pgo-instr only): `server.exe` ends with `TerminateProcess`,
  which skips the profile runtime's `atexit` write (an instrumented DLL then leaves a 0-byte `.profraw`); the object
  hooks the host EXE's import slot of `TerminateProcess` at DLL load and writes the profile first
  (`test_flush_object_writes_the_profile_when_the_host_terminates_itself`, with a control without it).
- **Wrong uses are refused before anything is written**: `--profile` with another configuration than release, with
  `--link-only`, a missing file, a raw (unmerged) profile; `--compile-only` with `--link-only`
  (`test_cli_refuses_wrong_pgo_uses_before_building`).
- `--compile-only` (new): stops after the compile and the narrowing gate; used for the 43-civ match counts.
- The narrowing gate of pgo-instr uses release's baseline rows (instrumentation is added after the front end).
- Timing tools: `timing_refusal()` refuses pgo-instr by any of the three signs (marker, record, layout name), as fast
  (`test_timing_refusal_of_pgo_instr`); a release + profile DLL passes (`test_dll_profile_reads_the_profile_section`).

## 4b. The fifth configuration `release-pgo` and the locked profile (ticket 0513, decision D184, 2026-11-24)

The owner, 2026-11-17 19:35 (D184): x64 release builds use PGO once bug 0508's fix is in the pins (it is, since pin batch 4).
Ticket 0513 turns 4801's measurement form `release --profile FILE` into a named configuration and the profile into a
locked build input. [C] `tools/x64-dll/build.py` (`RELEASE_PGO`, `lock_profile`, `check_pgo_args`, `retrain_verdict`,
`PGO_RETRAIN_SHARE`), `tools/x64-dll/build_all.py`, `server/build_server.py` (`deps_config_problem`),
`tools/release/stages/build.py` (`x64_config`, `pgo_plan`, `pgo_check_built`); tests
`tools/x64-dll/tests/test_release_pgo.py`, `test_config.py`, `tools/release/tests/test_take_release_pgo.py`.

| | release | release-pgo (new) |
|---|---|---|
| compile | `/Ox /Ob2 /Zo -flto` | release's + `/clang:-fprofile-use=<out>/gen/vp64.profdata -mllvm -pgo-warn-missing-function` (exactly 4801's `release --profile` line) |
| link | `/LTCG /OPT:REF /OPT:ICF` + 0373's DSE limit | release's + the `.vppgo` object (`VP-BUILD-PROFILE=<sha256>`) |
| whole-program, link place | yes | yes (`WHOLE_PROGRAM`) |
| narrowing gate rows | release | release (the profile is applied after the front end) |
| marked, refused for timing | no | no (it is the timed and shipped build once the MC switches) |
| records | `build-summary.json` `config: release` | `config: release-pgo`, `profile` {sha256, bytes, name, given, from_lock_file, locked, lock_sha256, lock_pin, functions}, `dll_profile`, `pgo_diagnostics`, `pgo_retrain` |
| layout name (release take) | `x64-<commit8>[-mN]-p<tools7>` | `x64-<commit8>[-mN]-p<tools7>-pgo` |

- **The profile is a build input like the source.** `deps/dll.lock` `profile = sha256 <64 hex> bytes <n> pin <commit> name
  <file>` (continuation lines: when and from which instrumented DLL it was trained, the training set with each raw
  profile's SHA-256, the tool versions, the function count) and `profile-file = <pgo_dir>/<file>` (where the file lives on
  the build machine; the file is never committed, MC #4429 (2)). *Corrected 2026-11-26, ticket 0774 (the lock is
  exported, MC #15669): the line names no machine folder; `<pgo_dir>` is the setting `pgo_dir` of
  `tools/release/settings.py` (deps/local-settings.ini, env `C5_PGO_DIR`; `%VAR%` works too), resolved by `build.py
  lock_profile` (`resolve_profile_file`); unset, build.py and the release take stop naming the setting. Was
  `profile-file = <path>`, the path written out.* `build.py --config release-pgo` takes `--profile FILE` or,
  without it, `profile-file`, and refuses (exit 1, before anything is written) a file whose SHA-256 is not the lock's,
  unless `--profile-unlocked` (a measurement or a retrain trial; recorded `profile.locked: false`). Without a profile line
  and without `--profile` it refuses too. `build.py --config release --profile` is refused with a pointer to release-pgo
  (it was 4801's unnamed form; same bytes).
- **build_all.py `--config release-pgo [--profile FILE] [--profile-unlocked]`**: the profile is checked before any step;
  the stubs, the two libraries and the smoke host are built as `release` (`lib_config`; their bytes do not depend on the
  profile), the dll step as release-pgo; `build_all.json` records `config: release-pgo`, `lib_config`, `profile` and the
  dll step's `profile`, `dll_profile`, `pgo_diagnostics`, `pgo_retrain`.
- **server/build_server.py** (MC #13094): `--config release --deps <a release-pgo build_all>` is accepted (the server and
  the proxy have no PGO build of their own); `--config debug` with such deps is refused; `bin/build.json` records
  `deps_config`. `timing_refusal` reads `config` (release) and is not affected.
- **The release take** (`tools/release/stages/build.py`, also called by the tier build stage
  `tools/release/tiers/stages/build.py` through `orig.build_x64` and `orig.run`): the x64 configuration is config
  `x64_config` (a per-take override and the tests' hook), else `deps/dll.lock` `release-config`, else `release`.
  **`release-config = release-pgo` is in deps/dll.lock since ticket 0774 (2026-11-26, the VP 5.5 pin a2627c8ac with its
  retrained profile, at the MC's order #15557)**; *was: "No line `release-config` exists today", 0513*. With it the stage, before anything is built, checks the locked profile (file present,
  the lock's SHA-256 (a mismatch is a stop: "refused"), trained on the x64 commit the take builds (else a stop: retrain));
  builds `build_all.py --config release-pgo --profile <file>` into `<build>/deps`, checks its summary (profile and the
  DLL's `.vppgo` = the locked SHA-256, else failed; `pgo_retrain.needed` true is a stop), then `build_server.py --config
  release --deps`; checks the laid-out DLL's `.vppgo`; names the layout `...-pgo`; the candidate lock's `verified` line
  and the stage outputs (`pgo_profile_sha256`, `pgo_profile_pin`) name the profile; `inputs()` gains `x64_config` and
  `pgo_profile_sha256` (absent for release, so a plain take's inputs hash and the tier stage's adoption are unchanged; a
  new profile is a new build input and defeats adoption).
- **The export's clean build (D174, epic 0468)** does not exist yet: when it does it consumes the same lock lines
  (`build.py --config release-pgo` with `profile-file`, or `--profile` naming the archived file, and the SHA-256 check);
  noted for 0468.
- **The retrain rule** (`retrain_verdict`, in every release-pgo build): `pgo_retrain.needed` when the compile's hash
  mismatches plus functions missing from the profile exceed 1 % of the profile's functions (`llvm-profdata show`
  "Total functions"), when a malformed-profile diagnostic appears, when the function count cannot be read, or when the
  locked profile was trained on another commit than the source's ("retrain before each release": every new pin).
  `build.py` still writes the DLL (a stale profile costs speed, never correctness) and prints a `WARNING: PGO retrain
  rule` line; the release take stops on it. Measured values: speedup-p1-pgo.md section 11.

## 5. Not verified

- Seconds per turn of fast against release (the Architect asked for it as information, measured on the testbed by
  D106): not measured here.
- That the place's wait and give-up work across two real concurrent release builds (the tests use stand-in holder
  processes; one real release link went through the place, section 2: take 23:57:25, linker pid recorded, release
  00:05:19 after 474 s, [L] `D:/c5-64bit-work/locks/lto-link.places.log.jsonl`).
- Other teams' release links do not take the place until their branches carry this change (0160 builds its arms
  from build.py's command lines without build.py, so its links are not counted).
