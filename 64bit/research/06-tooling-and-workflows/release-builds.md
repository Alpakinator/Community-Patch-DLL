# Release builds for players: configuration i3 and release 0.1b (ticket 0574)

Ticket 0574, decision D195 (the owner, 2026-11-18: closed beta 0.1b, one installer `.exe`), D197 (the option values). The plan
is `docs/RELEASE-0.1b.md`; the release driver is `release-driver.md` and `tools/release/README.md`; the installer is
`installer-notes.md` and `tools/installer/README.md`. This file records what a release for players is made of and how
each release candidate was taken and checked.

## 1. Configuration i3: the i2 build and an option set [C: code and hermetic tests, 2026-11-18]

`shell_config` was `thin` or `i2` (issue 2602). `i3` is the build and files of `i2` (the x64 layout with the
compositor, `shell_composite.dll`, `.sha256`, `.ini`, MinHook's licence) plus an **option set**: the values of
`VP64Options.ini` (`docs/OPTIONS.md`) that the release sets for the player. The options' built-in defaults stay as
they are (MC, Q1 on thread 0574, 2026-11-18 22:03: the installer writes the keys; no compiled release defaults).

| Where | What i3 adds |
|---|---|
| descriptor (`tools/release/descriptor.py`) | `shell_config: "i3"` and the field `options` = `{"<Section>": {"<Key>": <int>}}`, allowed only with i3 (`{}` when absent); names letters, digits and `_`; integers only; `[Renderer] ServerRenderer` refused (`vp64helper on` owns it); a section or key given twice in another case refused (`parse_options`) |
| testdata stage | the candidate descriptor carries `options` beside `shell_config`; `_common.shell_config_of` fails a stage whose descriptor's option set differs from the run's (the 0464 guard, extended) |
| build stage | i3 builds and lays out exactly what i2 does (`COMPOSITE_SHELLS`); its inputs and outputs say `i3` |
| pack (`pack.py --shell-config i3 [--options FILE]`) | i3 needs the compositor like i2; the manifest gets `"shell_config": "i3"` and `"options"` (`{}` without `--options`); `--options` with another configuration is a usage error (exit 2); a bad set exit 1, nothing written. The pack stage writes the descriptor's set to `<work>/inputs/options.json` and passes it; the set is in the stage's inputs and outputs. thin and i2 packs are unchanged |
| installer build (`build_installer.py`) | i3 accepted; the manifest's set checked again (`descriptor.parse_options`), an empty set refused ("else it is i2"), the two variants' sets must be equal; the payload gets `VP64\release-options.txt` (rows `Section<TAB>Key<TAB>Value`, sorted, CRLF); the build record gets `options` |
| installer (`install.iss`) | before `vp64helper on`, `P64WriteOptions` sets each key in `<My Games>\VP64Options.ini`: the section's last uncommented line of the key, else its first commented line (the form the proxy writes its defaults in: `; Key = default`), else a new line after the section's last non-empty line; a missing section is appended; a missing file is created with a three-line header. Line ends as the file had them. A UTF-16 or binary file is refused (the install rolls back, exit 20). The record gets one step `options` (H1 = the file's SHA-256 before, `-` when absent; H2 = after the write). Removal: after `vp64helper off` (which gives back the bytes `on` found, i.e. the installer's), a file equal to H2 is put back from its backup (or deleted when it did not exist); a file changed since is left, with a note naming the kept backup. A rollback restores the file through the earlier `saved` step |

Why before `on` [C: `proxy/helper/vp64helper.cpp` and installer-notes 9]: `on` records the whole file it finds in
`vp64-options-side.ini` and `off` gives those bytes back when the file is unchanged since `on`. With the keys written
first, `off` (the player's off switch) leaves the i3 keys in place with `ServerRenderer = 0`: the renderer keys do
nothing then (each needs `ServerRenderer = 1`, `docs/OPTIONS.md`), and with `mode=off` the proxy loads the 32-bit DLL
[I: the pacing options are read in `ProxyPacing::OnUpdate`, called from the proxy's update hook of the 64-bit path
(`proxy/src/proxy_update.cpp` line 289); a mode=off start's `vpproxy.log` without a `pacing:` line would confirm it].

Not a release choice: `[Renderer] ServerArc` and `ServerPath` stay 0 in 0.1b (MC #6561): their server-drawn pictures
beside stock are DIAGNOSTIC (D112) only (`research/05-graphics-composition/arrows-and-path-line.md` 13.6,
`path-models.md` 7.6-7.9), and neither has its D118 failure route nor rule A's default-on scene bundle.

## 2. Where players report (D195) [C, 2026-11-18]

*Corrected 2026-11-25 (D260, ticket 0767): the installer, its notice and both player docs now name upstream's
issues (`https://github.com/LoneGazebo/Community-Patch-DLL/issues/new`) and releases; the helper popup's line is
0761's (installer-notes 17.2). What follows is the D195 state it replaced.*
Every player-facing place names the DLL fork's issues page (`notices.py` `REPORT_URL` = `FORK_REPO + '/issues'`) until
the upstream merge:
`patch64.iss` `AppSupportURL` and the finished page ("Please report problems at ..."), the notice
(`notices.py` `REPORT_URL`), `docs/INSTALL.md` section 6, the testers' README, and the D118 popup's default hand-in line
(`proxy/helper/vp64helper.cpp` `kDefaultHandin`, `.../issues/new`; `vpproxy.ini handin=` still overrides it). The
notice's source lines keep naming both repositories (the GPL source offer). The finished page names the off switch of
the configuration: `vp64helper.exe off` for i2 and i3, `vpproxy.ini` `mode=off` for thin.

## 3. Release 0.1b: the option set (D197, MC #6523, 2026-11-18 22:03)

`ServerRenderer 1` (by `vp64helper on`), `[Renderer] ServerUnits 1`, `[Performance] AiUpdateTickMs 0`,
`AiSkipVisualDrain 1` (2 if 0571 merges by the freeze), `AiDrainUnordered 1`; `ServerCities`/`ServerLandmarks` 1 if 0561
merges by the freeze; hidden embarks (0573) and the 0504 package as the MC names them at the freeze (Thursday
2026-11-19 20:00 EST). ServerArc and ServerPath 0 (section 1).

## 4. Release candidates

### 4.1 rc0 (dry run of current main, 2026-11-18/19)

Purpose: find faults of the pipeline before the freeze; nothing of it goes to the owner. Inputs: this branch (main + 0574's
tools), pin batch 4 (`deps/i0.lock` e96457fd5, `deps/dll.lock` c29442e8c; no barred commit in either, checked
2026-11-18 22:2x), the run's descriptor `C:/c5-64bit-work/0574/Release-5.4.8-i3-rc0.json` = `releases/Release-5.4.8.json`
+ `shell_config i3` + `{"Renderer": {"ServerUnits": 1, "ServerArc": 0, "ServerPath": 0}, "Performance":
{"AiUpdateTickMs": 0, "AiSkipVisualDrain": 1, "AiDrainUnordered": 1}}` (SHA-256 4a308d45...). Command (the 0509 form):
`take_release.py Release-5.4.8 --tier validated --carry pins --for-art --descriptor <that file> --fork <own clone>
--work C:/c5-64bit-work/0574/rc0/run --scratch C:/c5-64bit-work/0574/rc0/scratch --who 0574 --gate-wait 60` with
`C5_RELEASE_INSTANCE=c5h-b`, `C5_RELEASE_HI_INSTANCE=c5h-b`, `C5_RELEASE_TESTBEDS=local`.

Results [L, 2026-11-18 23:08 -]: survey, testdata, lock ok by 23:11; **build ok** 23:11-00:05 (x64 1,399 s, compositor
424 s, i0, i0ia), `tools_commit` a79dde255, `tools_dirty` false, `shell_config` i3, layout `x64-c29442e8-pa79dde2`,
compositor `6FB0A8A0...`; narrowing, census, model, purity ok; **pack ok** 00:34 (manifest `shell_config` i3, `options`
as the descriptor); offline ok 00:45; **savetest ok** 01:09 with tiny 31,226 / standard 349,074 / huge 683,261 rows,
equal to the descriptor's batch-4 reference counts; drift (31,226 = 31,226), fixchecks, refsave ok; instance (c5h-b)
ok 01:13-01:44 (c5h-b: arm-tiny 276 s, arm-huge); **wholegames ok** 01:44-03:41, local, **4 of 4 x64 = x86
identical**: tc Tiny Continents (turn 425 Cultural, 1,258,102 rows), sf Standard Fractal (345 Diplomatic,
1,375,306), sp Standard Pangaea (396 Science, 1,863,156), mp Small Pangaea (367 Cultural, 1,172,594); 0 Lua errors.
**hi_journey** on c5h-b 03:41-04:22 (default stage-laid path, compositor off): release-basic **passed** on product
(537 s) and stock (478 s), 6/6 checkpoints the same; release-large stock **passed** (410 s), product **exit 3** at
checkpoint `attacked` after 11 steps: "no usable picture: top band 26 levels from the reference (printwindow, 2
retakes)"; the saved picture has no interface at all (world only). That is the known product-route PrintWindow flake
of the thin default path (`human-input-testing.md`, i2-rc1 rerun: "top band 37 levels", CONTRACT 4.2) [I: not rerun
yet]. So the take ended **TAKE-EXIT 10** (D162 stop, "for the owner: NO"); every other stage of the validated tier ok. The
dialog watcher's crash entry of 04:03:35 (c5h-b, pid 38500, 0xC0000005 without a box) is the stock game's exit after
release-basic's stock replay: the instance-aware test DLL's known exit crash (ticket 0484, `research/03-dll-engine-
boundary/proxy-design.md`: `ThunkOperatorNew<2>` after the host's `FreeLibrary`; test builds only, a release ships no
`-ia` DLL) [I: same signature, same moment as 0509's]. Scratch cut afterwards (MC #6868: 2 GB per team on C:):
5,352 MB freed, the work folder keeps state, layout, pack and build records (1.6 GB).

### 4.2 Hermetic tests of the i3 changes [L, 2026-11-19 00:08-01:34, branch task-0574-release-0-1b]

All through `quietlock.py run` (MC #6590: a named exception to COMMON 16 beside rc0's later stages, never during its LTO
link nor beside a game launch of mine). `test_descriptor*.py` 65 OK (new `test_i3_options`: the set read, `{}` for i3
without it, refused for thin/i2/absent, 12 bad sets refused by field); `test_take_release_shell_config.py` 20 OK (i3
pack with and without options, i3 on a thin build refused, `--options` without i3 exit 2 for three configurations, five
bad sets exit 1 with nothing written, a missing file; the pack stage passes i3 and `--options` and records the set);
`test_take_release_i2_compositor.py` 11 OK; `tools/harness/tests/test_installer.py` 34 OK (new: the i3 payload's
`release-options.txt` rows, compositor files, manifest and notice with the fork's issues link, the release name 0.1b
in the file name, a stub install of i3 and its config.ini refusal, `AppSupportURL`, six refused option sets; with the
real `install.iss` and the stand-in helper: options written before `on` (the stand-in's side file holds the
installer's bytes: a commented default replaced in place, a missing key added after the section's last line, a player's
comment with a cp1252 byte kept) and the player's file back byte for byte after removal; no file: one created and
removed; a file changed after the install left with the note and its backup kept; `on` refused: rollback restores the
file; a UTF-16 file refused, nothing installed); `tools/installer/tests` 36 OK. After the fixture fix below
(01:35-02:35): `test_take_release*.py` 168 OK, `test_validated_tier*.py` 76 OK, `proxy/tools/test_vp64helper.py`
165/165 (the default hand-in line names the fork's issues), `tools/harness/tests/test_safety_static.py` 55 passed.

**Finding: the release driver's fixture takes had stopped at savetest since the reference counts moved** [L, 2026-11-19
00:26-00:52]. `StageFixture.make_desc` (`tools/release/tests/test_take_release_stages.py`) and `test_validated_tier.py`
built their descriptors from the real `releases/Release-5.4.8.json`, so its `reference_counts` (moved by 0452/0463 to
huge 689,879 and by 0517 to 31,226 / 349,074 / 683,261) no longer matched the fake run_d10's rows (30,666 / 351,496 /
683,123): every whole fixture take ended in "STOP savetest" (3 of 20 shell-config tests, 6 of 11 i2-compositor tests
failed on the merged state before this ticket's changes touched them). Fixed in the tests (the fixtures carry the fake's
counts, `FAKE_REFERENCE_COUNTS`); a first full suite run (22:13-23:05) was also polluted by tracked edits in the
worktree during it (the dirty=1 trap of `release-driver.md` 0351: the fake build stage checks the repository).

### 4.1b rc0's TEST installer on c5h-d [L, 2026-11-19 12:26-12:57 EST]

`build_installer.py --work <rc0 work> --test-build --release-name 0.1b-rc0` (std only):
`VP64-Patch-0.1b-rc0-for-VP5.4.8-TEST.exe`, SHA-256 `81299143...`, 8,990,644 B, configuration i3; the payload's
`VP64\release-options.txt` holds the six keys of rc0's set. On c5h-d (MC-B's grant #6810; F: read latency 0.1-0.4 ms at
each launch), script `s20_install_check.sh`:
- the player's state: `provision.py set-dll c5h-d --dll <the tag's DLL 262aee88...>`; My Games listed (2,812 rows);
- **install** exit 0 (silent, test mode): the payload's files under `MODS\(1) Community Patch`, the record and backups;
  `VP64Options.ini` (the instance's own short file, `9497f85b...`) became `fae50678...`: the six keys added after the
  last line of `[Renderer]` and in `[Performance]`, then `ServerRenderer = 1` by `vp64helper on`; config.ini in the D33
  form with its side file;
- **D162 product route on the installer-installed layout** (`C5_RELEASE_HI_INSTALLED=c5h-d`, compositor on):
  release-basic **passed** (63 steps, founded/research/turn5/turn10/loaded/menu ok, 537 s), release-large **passed**
  (21 steps, moved/attacked/city/camera/menu ok, 522 s; no PrintWindow flake on this route: its pictures come from the
  composite); the stage then stops as expected for a run of one route ("stock not played");
- **removal** exit 0: `vp64helper off`, every file back from its backup by SHA-256, `VP64Options.ini` back to
  `9497f85b...` (not in the tree diff), the record and folders gone; My Games against the listing before: 146 rows
  differ, all in the game's run outputs (`Logs\`, `cache\`, `ModdedSaves\` autosaves); 0 outside;
- `set-dll c5h-d --original`, `provision.py verify c5h-d` rc 0, `frontend.py status` original; claim released.

### 4.3 rc0b: the validated take of the exact 0.1b pins [L, 2026-11-19 13:13-16:43 EST]

Inputs: this branch at `84d21f977` + the research commits (tools commit of the build `84d21f9`), `deps/dll.lock`
x64-0.1b `1b87dd185`, `deps/i0.lock` i0-0.1b `f33fc46a4`; the run's descriptor `Release-5.4.8-i3-rc0b.json`
(SHA-256 `f3f5156d...`) = `releases/Release-5.4.8.json` (batch-5 counts) + `shell_config` i3 + options {ServerUnits 1,
ServerArc 0, ServerPath 0; AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1} +
`purity_allowed_drops` (the 4 rows of 5.4); work on F:, instance c5h-b (MC-B's grant), whole games local; `--for-art`.

| Stage | Result |
|---|---|
| build | ok 13:46 (x64 561 s after one quiet-lock refusal and wait; compositor; i0; i0ia). Layout `x64-1b87dd18-p84d21f9`: x64 DLL `78CE2275...`, server.exe `80DB8123...`, proxy `AB80ACB0...`, compositor `2990E3F7...`. Marker defines `VP64_HIDDEN_EMBARK VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7`; narrowing 794 / 0 new / 4 gone; proxy step 7: 4 allowed drops (LOUD), full test 66/66 on the audited source |
| model | equal to the committed model (neither feature changes the proxy model) |
| purity | 135 methods, 27 Lua kept, 4 dropped (the allowed rows); full test 66/66 on the audited source |
| pack | manifest `6110E41E...`, shell_config i3, options as the descriptor |
| offline | ok |
| savetest | **30,569 / 359,261 / 685,488** rows = the descriptor's batch-5 references: the danger package and hidden embarks change no game result |
| drift, fixchecks, refsave | ok |
| instance | ok (c5h-b, 14:19-14:34) |
| wholegames | **4/4 x64 = x86 identical** (tc, sf, sp, mp; 14:34-16:05) |
| hi_journey | **passed** on product and stock (release-basic 63 steps, 6/6 checkpoints the same; release-large 21 steps, 5/5); the stock game's known -ia exit crash once after release-basic (16:25:43, pid 29340; evidence `c5h-b\run\crash\20261119_162543_pid29340`; 0484, the stage went on) |
| report | tier validated reached 16:43; "for the owner: yes" |

### 4.4 rc0b's 43-civilization take, and why D162 cannot be played on it [L, 2026-11-19 16:44-17:22 EST]

`s11_take43.sh rc0b` (`--major-civs 43`, the same descriptor, work on F:): survey .. pack ok (build with the marker
defines and step 7's allowed drops; purity 66/66 on the audited source; pack `major_civs` 43). Its hi_journey on c5h-b
(MC #6707: D162 for both variants) failed at once on the product route: the server refused the journey's start save by
design, `server.log` (run `20261119-171318-10160-1`): "FAILED: save-variant: the save was made with a 22-civilization
build of Vox Populi (by its layout ...), the running DLL has 43 major civilizations" (server exit at 105 s; no crash,
no box). Both HI journeys' start saves (`HI-4506-BASIC-T0`, `HI-4506-LARGE-SCENE`) are 22-civilization saves, so no
43-civilization build can play them, and `build_installer.py` without `--test-build` can never pass `--art-check` on a
`--work43` folder. The stage then failed the other routes (a session left active on the instance: "end it first"),
restored the instance (verify rc 0), and the take ended TAKE43-EXIT 10. Put to the MC as Q17 (#7103): (a) the 43
variant's evidence is its own validated take (whole games with 0372's 30-major game) plus the standard variant's D162,
with a recorded waiver in the installer build; (b) 43-civilization journeys and saves (later); (c) ship the standard
variant only. rc0b's installer was built with `--test-build` for both variants:
`VP64-Patch-0.1b-rc0b-for-VP5.4.8-TEST.exe`, SHA-256 `1d4baa12...`, 16,277,043 B.

### 4.5 rc0b's TEST installer (both variants) on c5h-d [L, 2026-11-19 17:22-17:47 EST]

`build_installer.py --work <rc0b> --work43 <rc0b-m43> --test-build --release-name 0.1b-rc0b`:
`VP64-Patch-0.1b-rc0b-for-VP5.4.8-TEST.exe`, SHA-256 `1d4baa12...`, 16,277,043 B, i3, variants std+m43. On c5h-d
(MC #7080), `s20_install_check.sh`: install exit 0; `VP64Options.ini` held ServerUnits 1, ServerArc 0, ServerPath 0,
AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1 and ServerRenderer 1 (helper); D162 product
route on the installer-installed layout (compositor on, the 0.1b pins): release-basic **passed** (530 s), release-large
**passed** (420 s); removal exit 0; My Games against the listing before: 164 rows differ, **0 outside the game's run
outputs**; `provision.py verify c5h-d` rc 0. (The 43 variant was not installed: the instance's Vox Populi is the
standard one; 0580 adds 43-civ journeys.)

### 4.6 rc0c: rc1's option set as a dry run; the release stopped by D200 [L, 2026-11-19 18:05-21:xx EST]

rc0c = the validated take (`--for-art`) of the branch at `216987017`'s tools (main with 0561 cities and 0465 live terrain
merged; the 0.1b pins) with descriptor `Release-5.4.8-i3-rc0c.json` (SHA-256 `987b6276...`): rc0b's set plus
`ServerCities 1`, `ServerLandmarks 1`. Results: build ok 18:31 (marker defines, step 7's 4 allowed drops); model equal;
purity 135 / 27 kept, 4 dropped, 66/66 on the audited source; pack, offline (scene library format 9) ok; **savetest
30,569 / 359,261 / 685,488 rows, the same as rc0b**: cities, landmarks and live terrain change no game result; drift,
fixchecks, refsave, instance (c5h-b 19:08-19:22) ok; whole games running when the owner cancelled the beta (D200, about
20:40, a Steam update of the game): a STOP file ended the take at the next step, before hi_journey; no rc1, no freeze.

Main merge check before the planned rc1 (MC #7204, a scratch worktree, 2026-11-19 20:05-20:40): the branch +
`83a802aaa` (0580's 43-civ journeys) merged with one conflict (this ticket's log); rc0b's and rc0c's descriptors load
through the branch's reader (the "options: unknown field" 0580 saw was main's reader, which lacks the field);
test_safety_static 55 passed; tools/release/tests 411 run, 1 failure, main's own:
`Units.test_only_listed_journeys_other_files_of_the_folder_ignored` still asserted the two 22-civ journey names after
0580 added the m43 ones. The fix (the names per major count) is `tools/release/evidence/0574/test_hi_journey-after-0580.patch`;
main is not merged into this branch after D200 (MC-B's order), so the MC applies it on main.

## 5. The 0.1b product pins and the marker defines (MC #6806, #6824, #6827, 2026-11-19)

### 5.1 The composed branches [C: git in the take's fork clone, 2026-11-19 03:30-04:00]

The 0.1b release is built from product pins composed by 0574 (new fork branches only), moved into `deps/*.lock` only
after 0551 has merged pin batch 5:

| Branch | Commit | Made of |
|---|---|---|
| `x64-0.1b` | `9a3ca174b` | merge (--no-ff) of `hidden-embark-x64-b5` `a49d8b2e7` (= `x64-batch5` `1fd51353e` + 1 commit) into `perf/speed-danger-b5` `5d4a90aa5` (= `x64-batch5` + 0504's 12 commits) |
| `i0-0.1b` | `f33fc46a4` | `hidden-embark-i0-b5` (= `i0-ship-batch5` `5d18d3d6c` + 1 commit); 0504 has no i0 commits (lead-0504 #6813) |

None of the three barred commits is an ancestor of either. Five files are touched by both features (`CvGame.cpp`,
`CvGameCoreDLLPCH.h`, `CvPlayer.cpp`, `CvPlot.cpp`, `CvUnit.cpp`); four merged cleanly; **one conflict**,
`CvGameCoreDLLPCH.h`, where both add an include block after `CvContractClasses.h`. Resolution (`remerge-diff` of
`9a3ca174b`): both blocks, the danger package's first, then hidden embarks, then the package's `#line 251` (so the
header's later lines keep batch 5's numbers, as the package's define-off identity relies on).

**The tree is the sum of the two features** (patch-ids of each feature's diff from batch 5 against the composed
tree's diff from the other feature, file by file): every file `same` except `CvGameCoreDLLPCH.h` (the resolution
above) and `CvUnit.cpp`, whose hunks of the two features are adjacent, so their context differs; its changed lines
(+/- lines of `diff -U0`) are identical on both sides (hidden `4ac8ff2b...` = `4ac8ff2b...`, danger `d8cb0cc8...` =
`d8cb0cc8...`). Scripts: `s50_compose_pins.sh`, `resolve_pch.py`, `s51_finish_compose.sh`, `s52_unit_check.sh` of
the ticket's scratch folder (to be kept with its evidence). Pushed to the fork 03:5x (new branches; never `master`,
`x64`, `i0-ship` or a batch branch).

Still to do at the freeze (MC #6806): build hashes of both, the D10 save tests, both journeys; the lock move after
0551's batch 5 is on `main`.

### 5.2 Marker defines in `tools/x64-dll/build.py` [C: code; L: unit tests 2026-11-19]

Agreed with lead-0504 (thread 0574, answer to #6812): `MARKER_DEFINES` lists each feature's marker header in
`CvGameCoreDLL_Expansion2` and its defines: `VP64HiddenEmbark.h` -> `VP64_HIDDEN_EMBARK` (0573's rule, generalised);
`VP64SpeedDanger.h` -> `VP64_SPEED_DANGER`, `VP64_SPEED_DANGER_DG7` (never the `_VERIFY`, `_STATS`, `_SPOIL*`
defines). `cl_args` adds them after the PREDEFS and the major-count define, in that order; a source without markers
keeps its compile line byte for byte (pins keep their hashes). `VP64_MARKER_DEFINES=0` turns every marker define off;
`perf/speedkit/defineoff.py` (its build environment) and `build_as_pin.py` (before `build.py`'s main) always set it,
so no define-off or as-pin build compiles with them (MC #6827). `build-summary.json` records `marker_defines`,
`markers_found` and `marker_defines_opt_out`, and the build log has one `marker defines:` line. Tests:
`tools/x64-dll/tests/test_marker_defines.py` (each marker, both, none, the opt-out and its only value `0`, the record,
markers elsewhere, never in PREDEFS, the two speed-kit tools) with `test_hidden_embark_define.py`, `test_major_civs`,
`test_repro`, `test_config`: 79 passed; `perf/speedkit/tests/test_speedkit.py` 25 passed.

### 5.3 The narrowing gate and the package's `#line` lines [L, 2026-11-19 11:56-12:3x EST]

The first take of the composed pins (rc0b, release, `--carry pins` on `9a3ca174b`) stopped at build (11:59): "the
narrowing gate found 26 new -Wshorten-64-to-32 site(s)" (build.py: 794 unique sites against the baseline's 798, 27 new,
31 gone), every new one a known site shifted by its line ("(+1)": CvCity, CvGame, CvMinorCivAI, CvPlayer, CvPlot,
CvTraitClasses, CvUnit). Cause [C: `git grep '^#line'`]: the danger package's own `#line N` lines (65 in 13 files) and
the PCH's one: the gate keys a site by its physical line's text, so a `#line` makes known sites appear new. x64-batch5
has none; 0573 met the same and dropped its `#line` (`research/08-performance/hidden-embarks.md` 7.1: "17 new, 21
gone"). So `perf/speed-danger-b5` alone does not pass the release gate either [I: not built alone; its kit builds run
without the gate]. Fix proposed (Q15, #7000): one commit on top, removing every `#line N` line of
`CvGameCoreDLL_Expansion2` (66 lines, 14 files, script `s56_strip_line.sh`) = local `1b87dd185`. Its release build
(unlocked, F:, `s57_build_branch.sh`): marker defines `VP64_HIDDEN_EMBARK`, `VP64_SPEED_DANGER`,
`VP64_SPEED_DANGER_DG7`; **narrowing gate 794 sites, baseline 798, 0 new, 4 gone** (the numbers of 0551's and 0573's
builds). With the defines on, which 0.1b ships, nothing depends on the `#line` lines; they served the package's
define-off byte identity.
MC #7028 (13:05): yes. The fork branch `x64-0.1b` was fast-forwarded to `1b87dd185` and pushed; `deps/dll.lock` names it
(fingerprint v1 `996b97a5...` over 479 files, describe `Release-5.4.8-80-g1b87dd185`). The package's `#line` lines
served only its define-off byte identity, which 0.1b does not ship; the gate passes 794 / 0 new / 4 gone. lead-0504
strips `#line` on `perf/speed-danger-b5` too. The 0.1b pins stay outside the carry series (MC #7029): the composed
tree is proven by 5.1's checks; series rows follow when 0504 and 0573 land as pins for all.
**Corrected 2026-11-23 (ticket 0639):** the 0.1b pins are now rows of the carry series (5.5); a take with
`--carry series` on Release-5.4.8 gives both pins' trees exactly. (Replaced: "the 0.1b pins stay outside the carry series".)

### 5.4 The proxy's purity test on the composed pins: allowed dropped rows [C: code and tests; L: rc0b, 2026-11-19]

rc0b's second build (12:55-13:06, on `1b87dd185`) passed the narrowing gate and then failed in `proxy/build_proxy.py`
step 7, the full purity test at the pin (`test_purity` 55/66): `derive_purity.py` at x64-0.1b drops 4 audited rows,
so the test's "every listed row kept at the pin" cannot hold:

| Kind | Row | Why it drops (derive's report) |
|---|---|---|
| method | `ICvUnit1::GetFacingDirection(bool) const` | 0573's facing hook: the cited text `CvDllUnit.cpp:154-157` changed (`research/08-performance/hidden-embarks.md` 7.2) |
| lua | `Player.GetCapitalCity` | "callee Get: 5 definition(s) at the audit, 7 now": the danger package adds two definitions of a `Get` |
| lua | `Plot.GetPlotCity` | the same `Get` |
| method | `ICvPlayer1::GetUnit(int)` | the same `Get` |

Dropping is the safe direction: the shipped `purity.tsv` lacks the rows and those calls cross to the server instead of
the proxy's mirror. MC #7044/#7046 (option C with an allow-list): step 7 now runs `derive_purity.py` at the pin first;
without drops nothing changes; with drops, each dropped row is one LOUD line, every dropped row must be in the
allow-list (`C5_PURITY_ALLOWED_DROPS`, a `kind<TAB>key` file) and the full test then runs on the audited source
(`C5_PURITY_AUDITED_SRC`, a plain copy of `proxy/model/purity-baseline.json`'s `audited_at`; the test cache key includes
it), as the release driver's purity stage already does (0480); any other drop, or a missing list or audited source,
fails the build. The allow-list is the release descriptor's field `purity_allowed_drops` (`[kind, key]` pairs, carried
by testdata's candidate, part of the build stage's inputs); the build stage writes `<work>/inputs/purity-allowed-
drops.tsv`, copies the audited source and sets both variables for `server/build_server.py` (inherited by the proxy
build). Tests: `proxy/tests/test_purity_drops.py` (7: no drop, allowed, a row not allowed, no list, no audited source,
derive failure, a bad list row), `test_descriptor.py` (the field and its refusals).

**Follow-up after 0.1b (MC #7044, option B):** re-audit the 4 rows (0573's facing hook and the danger package's two
extra `Get` definitions) so that the lists hold at the 0.1b pins again; a ticket for the purity owner.
**Superseded 2026-11-20 (ticket 0616, MC #8594):** the lists are re-baselined at `1b87dd185` with the 4 rows
`not-audited` (6.4), so no build at the pin uses the allow-list any more; the mechanism stays (tested). The re-audit
that could win the rows back is the draft ticket `0628 (drafted as NEW-0616-1)`, after the first beta.
**Done 2026-11-24 (ticket 0628):** the 4 rows were re-audited by hand at `1b87dd185` and are `pure` again
(`research/03-dll-engine-boundary/proxy-design.md` 17.5b); the derivation keeps all 169 rows at the pin.
Live [L, rc0b, 2026-11-19 13:25-13:35 EST]: the build stage passed the allow-list and the audited source copy of
`76992d235`; `build_proxy.py` step 7 logged one LOUD line for each of the 4 rows ("in the allow-list"), then the full
test on the audited source: 66/66 passed in 65 s. Build stage ok 13:46.

### 5.5 The 0.1b pins as carry-series rows; how a DLL hotfix is added (ticket 0639, 2026-11-23)

Why: with `--carry pins` the validation tiers' raise key is the x64 and i0 commits, so every DLL hotfix on a
pins-based release is raised to the intermediate tier; with `--carry series` the key is the carry mode and the tag
commit, and a DLL fix that is one more series row stays a hotfix (validation-tiers.md 3, 13).

**The rows** [C: `tools/release/branches.py` in a fork clone of 0639's own, 2026-11-23 12:0x-12:2x EST], appended to
`tools/release/series/carry.tsv` after batch 5 (72 rows, was 57):
- 13 `x64` rows, class `port`: epic 0504's danger package, the 12 commits `a2405fd81`..`5d4a90aa5` of
  `perf/speed-danger-b5` on `x64-batch5`, then `a70ebe15c` (lead-0504's strip of the package's `#line` lines, the same
  66 lines in 14 files as `1b87dd185`). Class `port` for the stop policy only: a conflict stops the take instead of
  leaving half a package in the DLL. They pick cleanly on `Release-5.4.8` + the x64 rows (no i0 rows below them).
- 1 `i0` row, class `port`: ticket 0573's hidden embarks `f33fc46a4` (i0-0.1b). The x64 commit `a49d8b2e7`
  (hidden-embark-x64-b5) has the same patch id (`fe298698...`, `git patch-id --stable`), so x64 takes it by the i0
  merge, as every pin took i0-ship; no x64 row for it.
- 1 `merge` row: `9a5b3be44`, fork branch `series/0p1b-pch-merge` (pushed 2026-11-23 12:1x, a new branch), the one
  conflict of the composing merge as a resolved row (below).

**Why a merge row** [C]: `branches.py` makes `x64-release` as base + the x64 rows, then merges `i0-release` with the
merge base `Release-5.4.8`. Hidden embarks (i0 side) and the danger package (x64 side) both insert an include block
directly after `#include "CvContractClasses.h"` in `CvGameCoreDLLPCH.h`. Two insertions at the same line conflict in a
three-way merge whatever else the x64 side holds: tried, the danger rows alone and the danger rows + a hidden-embark x64
row resolved onto them both STOP "the merge of i0-release into x64-release conflicts: CvGameCoreDLLPCH.h". No order of
rows avoids it, because the merge's result depends only on the three trees. So `branches.py` gained `merge` rows (MC
#11631; `tools/release/README.md` section 3): a row with branch `merge` names a pushed two-parent merge commit R; a
conflicted file of the i0 merge takes R's blob only when its blobs at the merge base, the x64 side and the i0 side
equal R's at R's merge base, first parent and second parent (an exact replay); anything else stays the stop, naming the
file and the side whose blob differs. A series without merge rows behaves as before. Tests:
`tools/release/tests/test_branches.py` (19 passed, 6 new: resolves on equal blobs, i0-side and x64-side blob differ
(negative controls: STOP, no branch written), unused when the merge is clean, the row must be a merge, no upstream).

**R** = `9a5b3be445f0174620d6254c260715d7b0338593`, parents `a70ebe15c` (x64 side: x64-batch5 + the danger package,
`#line` stripped) and `f33fc46a4` (i0 side: i0-0.1b); merge base `2fbf08f9a`. `git merge-tree` of the two conflicts in
`CvGameCoreDLLPCH.h` only; resolved with `1b87dd185`'s blob (both blocks, the danger package's first, no `#line`); the
resulting tree `0ec88ea5fbc5...` equals `1b87dd185^{tree}` (checked by the script before `commit-tree`). The PCH blob
equals at the series' merge base (`Release-5.4.8`) and R's (`2fbf08f9a`), at the series' x64 side and `a70ebe15c`, at
the series' i0 side and `f33fc46a4`, so the replay applies.

**The check (acceptance 2)** [C]: `branches.py --base Release-5.4.8 --series tools/release/series/carry.tsv
--compare-i0 f33fc46a4 --compare-x64 1b87dd185`: 72 applied, 0 dropped, 0 skipped; `i0-release` tree `ff3265f33c81`
equals `f33fc46a4`'s, `x64-release` tree `0ec88ea5fbc5` equals `1b87dd185`'s; `branches.json` `merge_resolved`:
[`CvGameCoreDLL_Expansion2/CvGameCoreDLLPCH.h`]. Rerun on a fresh clone that fetched only origin's branches (x64-0.1b,
i0-0.1b, ia-luaexec-no-loadstring, perf/speed-danger-b5, series/0p1b-pch-merge): every series commit present (the
rule of issue 3510). The old series still gives batch 5 exactly (both trees equal `x64-batch5` / `i0-ship-batch5`).
Scripts: `C:/c5-64bit-work/0639/s03_try.sh`, `s05_merge_commit.sh`, `s06_takeclone.sh` (scratch, not committed).

**How a DLL hotfix is added from now on** (a take with `--carry series`, D220/D221):
1. Make the fix on a new fork branch from the right side's last row: a fix for both builds from `i0-0.1b`
   (`f33fc46a4`; the x64 release takes it by the i0 merge), an x64-only fix from `x64-0.1b` (`1b87dd185`). Never on
   `master`, `x64`, `i0-ship`, a batch or an upstream-tracking branch. Push the branch (issue 3510's rule).
2. Add one row at the end of a release copy of the series (`tools/release/series/carry-<release>.tsv`, passed with
   `take_release.py`/`take_tier.py --series FILE`, issue 3509) or of `carry.tsv` itself once the fix is for every later
   release: class `fix` (or `port`/`identity` when a conflict must stop the take), branch `i0` or `x64`, the full hash,
   the subject, `-` or the upstream commit/PR that carries the same change.
3. Check it before the take: `python -I tools/release/branches.py --fork <clone> --base Release-5.4.8 --series <copy>
   --name <n> --out <dir>` must end `0 skipped` with the new row `applied`; the trees now differ from the pins by
   exactly the fix (`git diff --stat` of `x64-release-<n>` against `1b87dd185`).
4. If the fix changes `CvGameCoreDLLPCH.h` on either side, the merge row's blob no longer matches, and an i0 fix next
   to a danger-package hunk can make the i0 merge conflict in another file: the take STOPS at branches naming the file
   ("i0 side blob differs", or "no merge row"). Then make a new R (the new x64 side and i0 side merged, the files
   resolved by hand, pushed as a new branch) and put it in a new `merge` row (README section 3 has the commands).
5. Take it in the hotfix tier against the last accepted `--carry series` take: `take_tier.py <tag> --tier hotfix
   --carry series --series <copy> --base-take <that take's work> ...` (runbook 1.12). tierraise does not raise (same
   carry mode and tag commit); the DLLs are built (the commits changed), and the stages that key on the DLL's bytes
   (savetest Tiny + Standard, wholegames Tiny Continents, the product route of both journeys) run fresh.
**Trap: the `-ia` patch of a series take must be given** [L: take A, 2026-11-23 20:08:59]: the build stage picks the
instruments patch by the newest `IA_PATCHES` base that is an ANCESTOR of the i0 commit (`stages/build.py`
`pick_ia_patch`). A series branch is made of cherry-picks on `Release-5.4.8`, so it contains no pin commit and the
rule falls back to 9045's patch (base `Release-5.4.8`), which does not apply: STOP build "the instruments patch
ia-ee893bc18-440878983.patch does not apply to <i0 commit>". The i0 tree equals `f33fc46a4`'s, so 4968's patch
applies: pass `--ia-patch tools/d10/evidence/4968/ia-5d18d3d6c-4968.patch` to every `--carry series` take of these
rows (recorded in the state; the 32-bit adoption of a later hotfix compares it, so give the same path each time).
A by-base rule that matches trees or patch ids instead of ancestry would remove the trap [I: not changed by 0639].

The first `--carry series` take after the pins-based first beta is an intermediate take (the carry mode changed from
`pins`), once; its measured run is 5.6.

### 5.6 The first `--carry series` take, and a DLL hotfix as a row (ticket 0639, 2026-11-23) [L]

Wall clock on a busy workstation (other agents' render builders, run_all suites and games beside it), not a D106
timing claim. Runs from a clone of the 0639 branch (`C:/c5-64bit-work/0639/runco`, 1b9352dbc = the branch with main
merged), fork clone `C:/c5-64bit-work/0639/fork-take` (fetched from origin only), results store
`C:/c5-64bit-work/0639/results` (cand1/run and cand2/tier3 imported), descriptor `cand2/Release-5.4.8-beta2.json`.

**Take A: the intermediate tier, `--carry series`, base `cand2/tier3` (the second beta's hotfix take, pins-based)**,
work `C:/c5-64bit-work/0639/tierI`, through build and the comparing stages (no suites barrier, no game stage: MC-B
#11643; the suites were run on the branch instead, ticket log). Command: `take_tier.py Release-5.4.8 --tier
intermediate --carry series --base-take C:/c5-64bit-work/cand2/tier3 --descriptor <d> --fork <clone> --results <store>
--jobs 4 --ia-patch <runco>/tools/d10/evidence/4968/ia-5d18d3d6c-4968.patch --work <w> --scratch <s> --only <stage>`,
stage by stage (script `C:/c5-64bit-work/0639/takeA.sh`).

| Stage | When (2026-11-23) | Minutes | Result |
|---|---|---|---|
| survey | 19:55:56-19:56:00 | 0.1 | ok |
| branches | 19:56:02-19:58:48 | 2.8 | ok: 72 applied; i0-release tree equals `f33fc46a4`, x64-release tree equals `1b87dd185` (the merge row resolved the PCH) |
| testdata | 19:58:51-20:05:43 | 6.9 | ok (game copy 267 s; snapshot's accepted DLC map failures) |
| lock | 20:05:45-20:05:48 | 0.1 | ok: x64 `4d8fb5c0b`, i0 `2ea800bd7`; x64 fingerprint `996b97a5...` 479 files = `deps/dll.lock`'s |
| tierraise | 20:05:51-20:07:26 | 1.6 | ok: `dll_pin` changed `{pins, x64 1b87dd185, i0 f33fc46a4}` -> `{series, tag_commit 7307aefdf}` recorded, not raised (intermediate); compiler, VP release and EXE build 26577625 equal; export preflight 95 s |
| build | 20:09:26-20:50:05 | 40.6 | ok, first try 20:08:59 STOPPED on the `-ia` patch (5.5's trap; rerun with `--ia-patch`). x64 build key changed (the pin commit), so built: x64 2,350 s (LTO and server/proxy), i0 1,361 s and i0-ia 914 s side by side, compositor 316 s |
| narrowing, census, model | 20:50:06-20:50:08 | 0.0 | CARRIED from cand1/run: x64 source fingerprint `996b97a5dd4f43ea` equal, compiler, tools equal |
| purity | 20:50:10-20:52:15 | 2.1 | fresh (x64 DLL bytes differ), derive + full test 115 s, ok |
| pack | 20:52:20-20:52:22 | 0.0 | ok |
| offline | 20:52:24-20:58:11 | 5.8 | ok |
| savetest | 20:58:12-21:28:11 | 30.0 | fresh, run_d10 Tiny, Standard and Huge x64 = x86 both ways, 1,799 s, ok |
| sum | | 89.9 | |

**The DLLs against the pins' (cand1/run)** [C: byte comparison, `C:/c5-64bit-work/0639/dllcmp.py`]: the x64 DLL has
the same size (23,725,056 B) and differs in 43 bytes; the only printable strings in one file and not the other are the
build date and time (`Nov 23 2026` `22:35:13` against `Nov 22 2026` `10:18:33`, the repository-commit SOURCE_DATE_EPOCH
of validation-tiers.md 20). The x64 version string is the lock's describe, so it is equal. The 32-bit DLLs differ in
about 1.37-1.38 MB of 17 MB at equal size: their version strings name the i0 commit (`...-g2ea800bd7b Clean` against
`...-gf33fc46a4b Clean`) and the 32-bit build is not byte-reproducible across builds; their game behaviour is checked
by savetest (x64 = x86), not by bytes.

**Series commits are new at every run** [C]: `branches.py` writes each pick with `git commit-tree`, so the committer
date makes new hashes at every run of the stage: take A's first branches run (12:33) gave x64 `9b5767c8e` / i0
`972b38ed4`, its second (19:56) `4d8fb5c0b` / `2ea800bd7`, from the same series and base. Every DLL adoption rule keys on
the commits (the hotfix tier's: x64 and i0 commits equal; the intermediate tier's x64 build key: the pin commit), so a
series take never adopts a DLL of an earlier series take, not even the 32-bit DLLs of an x64-only hotfix. Making the
picks deterministic (committer date = the picked commit's author date, the merge's = the later parent's) would let a
same-row branch keep its hash; a proposal, not done by 0639 [I].

**Take B: a DLL change as a new series row, in the hotfix tier (the dry run of acceptance point 3; 0630's M1 point
(f))**, work `C:/c5-64bit-work/0639/tierH`, base take A with `--base-unaccepted` (A is not a release), release copy
`C:/c5-64bit-work/0639/carry-0639-probe.tsv` = carry.tsv + one row `fix x64 9163a376d`: fork branch
`series/0639-hotfix-probe`, one comment line appended to `VP64SpeedDanger.h` on `1b87dd185`, a probe that is NEVER
shipped (MC #11646). Command as take A with `--tier hotfix --base-take <A> --base-unaccepted --series <copy>` (script
`takeB.sh`). Stopped before hi_journey (MC #11646: its minutes estimated from validation-tiers-m2.md 8.4i's fresh
stage, 49 min `[I]`); tierreport not run. The build ran in two parts around MC-B's disk hold (23:30-23:34).

| Stage | When (2026-11-23/24) | Minutes | Result |
|---|---|---|---|
| survey, branches, testdata, lock | 21:28:42-21:32:12 | 3.5 | ok: 73 applied; i0 tree equals `f33fc46a4`; x64 tree differs from `1b87dd185` in 1 file (the probe) |
| tierraise | 21:32:12-21:33:15 | 1.1 | **ok, NOT raised**: `dll_pin` `{series, tag_commit 7307aefdf}` on both sides; compiler, VP release, EXE build 26577625 equal. The DLL change stays a hotfix. |
| build | 23:01:42-23:30:21 and 23:34:38-23:58:42 | 52.7 | ok. "no adoption (x64_commit changed)": the hotfix tier adopts all DLLs or none (`why_not_adoptable` compares both commits), and builds one after another: x64 + server/proxy 1,511 s, compositor 174 s, i0 712 s, i0-ia 695 s |
| narrowing | 23:58:43 | 0.0 | fresh (x64 source changed), ok |
| census | 23:58:45-00:11:03 | 12.3 | fresh, ok |
| model | 00:11:04-00:15:58 | 4.9 | fresh, ok |
| purity | 00:15:59-00:17:52 | 1.9 | fresh, ok |
| pack | 00:17:53-00:17:55 | 0.0 | ok |
| offline | 00:17:56-00:22:47 | 4.8 | ok |
| suites | 00:22:48-00:24:51 | 2.0 | safety-static 55 passed, export-gate passed (the diff since the base's tools commit is empty) |
| savetest | 00:24:52-00:37:16 | 12.4 | fresh, Tiny + Standard (744 s), ok |
| wholegames | 00:37:18-01:06:27 | 29.1 | fresh, Tiny Continents x64 and x86 on `local` (1,749 s), 2 runs, 0 failed, ok |
| hi_journey | not run | 49 `[I]` | take 7's fresh stage (8.4i); here both routes would run fresh, the 32-bit DLL being rebuilt |
| sum | | 124.7 measured + 49 `[I]` = about 174 | |

**Against the study's 65-75 minutes** (validation-tiers.md 16, the row "a DLL change as a carry-series row"): NOT met
on this busy workstation, about 2.3x. Where the minutes go [L, with the estimates marked]:
- the build, 52.7 min: every DLL is rebuilt one after another. Only the x64 DLL changed (x64 + server/proxy 25.2 min +
  compositor 2.9); the two 32-bit DLLs (23.5 min) are rebuilt because the hotfix tier's adoption is all-or-nothing on
  both commits and a series run makes new commits anyway (5.6 above). Per-DLL adoption in the hotfix tier (the
  intermediate tier already has it) plus deterministic series commits would save about 24 min, and would let
  hi_journey carry the stock route (the 32-bit DLL unchanged): about 21-26 min for the product route instead of 49 `[I]`.
- wholegames, 29.1 min against the study's "about 10" `[I]`: the Tiny Continents game on both builds.
- census and model, 17.2 min fresh: any DLL source change makes them fresh.
- the stages ran one after another (no `--places`); side by side (census, model, offline, savetest and the journeys
  after the build) the wall clock would be about 110 min as taken and about 85 min with the two fixes above `[I]`.
Proposals for the tier owner (not done by 0639): per-DLL adoption in the hotfix tier; deterministic picks in
`branches.py`; `--places` for hotfix takes.

## 6. The first beta's release path: 0574 and epic 0504 integrated with main (ticket 0616, D216, 2026-11-20)

Branch `int-0616-release-path` (from main `5f51dd2ee`): 0574 as one squash commit, epic 0504's merge request
(`epic-0504-squash` 95a270d16) as one squash commit, then the lock line and the model. Not merged into main by 0616.

### 6.1 How the two branches meet main [C: git merge-tree, 2026-11-20]

- The only product file both branches change is `tools/x64-dll/build.py`. 0574 adds the marker defines (5.2), 0504
  adds the `pgo-instr` configuration (4801) and the narrowing gate that follows `#line` (4810), and main has
  `VP64_RENDER_EVENTS` (4932) since 2026-11-19. In the merged file `cl_args(..., render_events=True, profile_use=None)`
  takes both as keywords. The compile line is PREDEFS, then the major-civilization define, then the marker defines
  (hidden embarks, then the danger package), then `VP64_RENDER_EVENTS`. A source with only the hidden-embark and
  render-events headers keeps main's line and order.
- `deps/dll.lock`, `deps/i0.lock`, `proxy/build_proxy.py`, `tools/release`, `tools/installer`: 0574 only. 0504 changes
  no lock. 0574's `proxy/build_proxy.py` step 7 (5.4) merges cleanly with 0609's VC9 STL check.
- options/: id 11 was main's `kReserved11`, held for 0504's `GameEventsOneCrossing` (K4). It is now that option,
  default 0 (MC #8392 Q4). Ids 12-15 and `kCount` 23 come from main. The default file is now 8,935 bytes, so a test
  buffer of 8 KiB is too small.
- Epic 0504's squash commit carried the local user folder in 540 lines of 115 files (perf/evidence/0504, five 48xx
  tickets). Its own export check (`--require-terms --no-env-terms`, 2026-11-19) checked product and notes files only.
  Main's `tools/export/check.py` also checks repository files (16,241 on 2026-11-20) and finds these lines. So 0616 took
  the merge request as a squash with the folder replaced by `%USERPROFILE%`, not as a merge of 95a270d16. The check
  then passed: 0 new findings over `5f51dd2ee`.
- A pin move after 0602 also needs `proxy/gen/regen_model.py --lock deps/dll.lock --write`. Run at 1b87dd185 it reported
  EQUAL in 463 s: only `commit`, `fork_path`, `generated_at` and the counts that depend on the source path changed.
  `test_hi_coverage` compares the model's commit with the lock (33 passed, 13 skipped without `lupa`). The
  Re-pinning block of `deps/dll.lock` now says so.

### 6.2 The new EXE build and the 0.1b pins [C: git apply --check; lead-0590 #8419, 2026-11-20]

- Build 26577625 needs no change in the product DLLs: epic 0590 changed the proxy, shell, server, render and the
  build table. 0606's live runs used 1fd51353e / 5d18d3d6c [L, lead-0590]. Only the harness's instance-aware test DLL
  needs 4968's `IA_PATCHES` row (fork branch `ia-luaexec-no-loadstring` d45303627, base 5d18d3d6c).
- i0-0.1b `f33fc46a4` contains 5d18d3d6c, so `pick_ia_patch` takes row 4968, and the patch applies to f33fc46a4
  (`git apply --check` in a scratch repository made from an archive) [C].
- No run of 1b87dd185 / f33fc46a4 on build 26577625 exists yet [not verified]: the first beta's take (0601) is the
  first.

### 6.3 Checks on the integration head [L, 2026-11-20, workstation, C:/c5-64bit-work/0616]

Pytest with plugins off (`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider`), tree of the
head before the user-folder scrub (code identical): the machine was shared with other teams, so the times are long.

| check | result |
|---|---|
| `tools/release/tests` | 417 passed, 1 failed (2 h 02 min). The failure was `test_ia_patch_4968_row_is_the_current_pin...`: it required the 4968 row's base to be the i0 lock's commit, and with 0574's lock the pin is f33fc46a4, composed on that base. The test now also accepts a pin whose Re-pinning line names it as built on the row's base (6.2); 1 passed after the fix |
| `tools/harness/tests/test_installer.py` | 49 passed |
| `tools/installer/tests` | 36 passed |
| `tools/harness/tests/test_safety_static.py` | 55 passed |
| `tools/harness/tests/test_hi_coverage.py` | 33 passed, 13 skipped (no `lupa`) |
| `tools/exebuild/tests` | 52 passed |
| `tools/x64-dll/tests` | 148 passed |
| `proxy/tests` (pytest) | 10 passed, 1 skipped, 8 errors. The errors are "fixture 's' not found" in five script-driven test files that the branch does not change (`test_crash_filter`, `test_farm_guard_proxy`, `livereload`, `luaexec`, `ui_bridge`): pytest collects their helpers. The result is the same on main. 0574's `test_purity_drops.py` passes |
| options unit tests (`ipc/build.py --target test_options`, x86 and x64) | 699 passed each; default file 8,935 bytes |
| `server/tests/luaraise/run_luaraise_test.py` (0504's) | 20,231 passed |
| `proxy/build_proxy.py` | VC9 STL check (0609) ok on the proxy DLL (48 objects) and vp64helper (2). Step 7 without the allow-list fails as designed, naming the 4 rows of 5.4. With `C5_PURITY_ALLOWED_DROPS` (the 4 rows) and `C5_PURITY_AUDITED_SRC` (an archive of 76992d235): 66/66 purity, OK in 242 s |
| `server/build_server.py --config release --deps <x64 build>` (same purity environment) | OK in 436 s (server, render, proxy) |
| `tools/x64-dll/build_all.py --config release` at 1b87dd185 | OK in 894 s. Marker defines `VP64_HIDDEN_EMBARK`, `VP64_SPEED_DANGER`, `VP64_SPEED_DANGER_DG7`; render events absent; narrowing gate 794 / baseline 798 / 0 new / 4 gone; smoke "22 major civilizations", DLL sha256 1719a788... |
| the same with `--major-civs 43` | OK in 846 s, gate 794 / 0 new, DLL sha256 565434cc... |
| `tools/export/check.py --rev HEAD --since origin/main --require-terms --no-env-terms` | PASS, 0 new findings |

Both LTO links ran one after the other.

After merging current main (d7eba2e47, 284 commits, 2026-11-20 17:05-17:39): `test_safety_static` 55 passed,
`test_hi_coverage` 33 passed / 13 skipped, `tools/x64-dll/tests` 148 passed, `test_installer` 49 passed,
`test_take_release_stages` 47 passed, options unit tests 742 passed on x86 and x64 (main's epic 0455 extras: kCount
28, default file 10,086 bytes), export check PASS. Main changed none of tools/release, tools/x64-dll/build.py,
proxy/build_proxy.py, the locks or proxy/model since the full runs above, so those were not repeated.

### 6.4 Purity re-baselined at the pin: a plain dev build needs nothing set [C: derive_purity.py --make-baseline and a baseline comparison; L: build_proxy, 2026-11-20 17:55-18:08 EST, workstation]

Why (MC #8594 (1)): with `deps/dll.lock` at `1b87dd185`, `proxy/build_proxy.py` step 7 failed for every plain dev
build, because the derivation drops 4 audited rows there (5.4) and only the release driver set
`C5_PURITY_ALLOWED_DROPS` / `C5_PURITY_AUDITED_SRC`.

What was done (commit "0616: purity re-baselined ..."):
- The 4 rows of 5.4 are `not-audited` in `proxy/model/pure-methods.tsv` (`ICvPlayer1::GetUnit(int)`,
  `ICvUnit1::GetFacingDirection(bool) const`) and `lua-pure-bindings.tsv` (`Plot.GetPlotCity`,
  `Player.GetCapitalCity`); each note says it was pure at 76992d235, why the derivation drops it at 1b87dd185 and that
  its re-audit is the draft ticket `0628 (drafted as NEW-0616-1)`. The rows stay in the files, so the audit trail is kept.
- `derive_purity.py`'s `AUDITED_AT` = `1b87dd185`; `derive_purity.py --make-baseline --fork <fork clone>` wrote
  `purity-baseline.json`: 162 entries (135 method, 27 lua), 397 citations, 384 callee names, 0 unresolved (was 166).
  `gen_pure_list.py --check`: the lists hold.
- It is mechanical, not a re-audit: compared entry by entry with the 76992d235 baseline, the 162 kept rows have the
  same citation texts (SHA-1), the same callee names and the same definition hashes of every callee; only the line
  numbers of 96 rows moved. Exactly the 4 rows are gone. So the set of rows a proxy keeps at this pin is the same as
  before (they were already dropped there); no product behaviour changes.
- The allow-list mechanism stays (tested, `proxy/tests/test_purity_drops.py`); no committed descriptor named
  `purity_allowed_drops`, and a take at this pin needs none.
- New tests in `proxy/tests/test_purity_drops.py` (10 passed): the baseline's `audited_at` = `deps/dll.lock`'s commit
  = `derive_purity.AUDITED_AT` (a pin move without a re-baseline now fails a test, not a team's build), the lists hold
  and the 4 rows are not kept, and with no dropped row no variable is read.

Proof [L]: `build_proxy.py --src <plain copy of 1b87dd185> --out <scratch>` with neither variable set: no LOUD line,
step 7 `66/66 passed in 146 s` on `--src` itself, `build_proxy: OK in 210 s`.

**Re-baselining at a pin move (how to):** if the derivation at the new pin drops rows, either re-audit them by hand
(cite the new pin's lines and start the evidence column with `[cited at <pin commit>]`, ticket 0628, proxy-design.md
17.5b) or mark them `not-audited` with a note; then set `AUDITED_AT` in `derive_purity.py` to the new commit, run
`python -I -X utf8 proxy/tools/derive_purity.py --make-baseline --fork <fork clone holding the commit>` and
`gen_pure_list.py --check`, and compare the old and new baselines (same texts and definition hashes for every kept row).

Cost of the 4 rows: ~~those calls cross to the server instead of being answered by the proxy's mirror~~ *(corrected
2026-11-24, ticket 0628: none of the 4 is ever answered by the proxy; a call that is not pure-listed moves the epoch,
so the mirrored getters read after it cross again; the Lua rows cost nothing with the product's `lua_mirror=0`, and
`ICvPlayer1::GetUnit(int)`, called about once per idle frame, may have cost most of the idle-frame mirror [I, not
measured]: proxy-design.md 17.5b)*; the number per turn was not measured [not verified]. **Won back 2026-11-24 by ticket
0628**: the 4 rows are `pure` again after a hand re-audit at `1b87dd185` (proxy-design.md 17.5b), their evidence cites the
pin with the new per-row marker `[cited at 1b87dd185]` of `derive_purity.py`, and the re-baseline keeps 169 of 169 rows
at the pin. The note for that ticket (the server's unit feed calls `GetFacingDirection(false)` on the DLL's thread and
cites the purity list; at 1b87dd185 that call goes through `VP64HiddenEmbark::VisualFacing`) is answered there: the call
is still pure.

### 6.5 Job 3 on main fb6d77b9a: plain builds from the shared pin folder [L, 2026-11-20 18:18-19:19 EST, workstation]

The shared folder for all teams is `C:/c5-64bit-work/keep/pins-0.1b` (read-only, 1.04 GB, under keep, never cleaned):
`src-x64-1b87dd185` (a git archive of the fork, 353 MB), `deps-x64-release` (`build_all.py --config release`, 685
MB, OK in 675 s), `README.txt` with the two commands, and `build_all.log`. It holds release only: every team builds
release (MC-B #8601, lead-0590). There is no i0 copy, because only the release take uses f33fc46a4.
`build_server.py --deps` only reads the folder: a build against the read-only copy passed. **Corrected 2026-11-20
(ticket 0629):** before 0629 that build's `<out>/bin` copies of the dependency DLLs and PDBs were read-only too, so a
later removal or rebuild of that folder failed. See 6.6 (it was: "a build against the read-only copy passed", with
nothing about the outputs). Ticket 0624 met the same before 0629 (2026-11-20 21:37 EST) [L]:
`proxy/tools/test_proxy_session.py --build <out>` failed at once (`PermissionError` appending to the pack's
`CvGameCore_Expansion2_x64.dll` in case pack-hash), as did `test_proxy_rt.py --build <out>` (`Access is denied` on
the layout's x64 DLL); on a branch without 0629, clear the attribute in your own out folder only (MC #8846).

The branch head 250eaa106 (main fb6d77b9a merged in, no conflict) was checked with the shared source and deps and
with neither purity variable set. Script: `C:/c5-64bit-work/0616/run_job3.sh`.

| check | result |
|---|---|
| `proxy/build_proxy.py --src <shared>` (release) | OK in 61 s; purity test cached from 6.4's pass (same 26 inputs) |
| `server/build_server.py --src <shared> --deps <shared deps> --config release` | OK in 304 s (server, render, proxy) |
| options unit tests (x86, x64) | 742 passed each |
| `test_safety_static` | 55 passed |
| `proxy/tests/test_purity_drops.py` | 10 passed |
| `tools/x64-dll/tests` | 148 passed |
| `tools/harness/tests/test_installer.py` | 49 passed (it covers build 26577625 as product-ready, see below) |
| `tools/installer/tests` | 36 passed |
| `test_take_release_stages` | 47 passed (27 min) |
| release tests that read files main changed since the full run of 6.3 | 86 passed: `test_take_release_i2_compositor` and `test_take_release_new_tag` (server/build_server.py), `test_descriptor`, `test_descriptor_tools` and `test_descriptor_testdata` (docs/OPTIONS.md via descriptor.py), `test_settings_exe_tables` (tools/exebuild/builds.py) |
| `tools/export/check.py --rev HEAD --since origin/main --require-terms --no-env-terms` | PASS, 0 new findings |

How the delta set was chosen: `git diff --name-only 5f51dd2ee origin/main`, without tracker, research and evidence
files (137 files), grepped for in `tools/release/{*.py,stages,tests}`. No release test runs `tools/harness/pool.py`
(the stages call it as a tool, and the tests use fakes). No release test reads the changed proxy, render or server
sources. So the rest of the full suite was not repeated.

**Installer, supported-build line [C: grep and test, 2026-11-20].** The "does not support yet" check exists only in
`tools/installer/patch64.iss` (`ProductReadyExeBuild`: 4390913 and 26577625, from 0606).
`build_installer.py` always compiles that file. `install.iss` and `install_stub.iss` are its `P64InstallInc`
include and hold no build list. The branch has had 0606's line since its merge of main at d7eba2e47 (17:39). The earlier
push 909c02e9b (15:40) did not have it, which explains the warning a TEST installer built from it gave on 26577625
(ticket 0620). `test_installer.py::test_installer_product_ready_list_equals_builds_py` fails if the line is missing.

After merging main 72d5ee5dd (epic 0590's 0612: the testdata stage's core database pinned by SHA-256, `core_db` has
no default, `deps/local-settings.ini` names the local copy; no conflict with 0574's `purity_allowed_drops` carry in
`stages/testdata.py`), head 209be35d3, 2026-11-20 19:20-19:46: `test_descriptor_testdata`,
`test_settings_exe_tables`, `test_take_release_new_tag` and `test_take_release_stages` gave 85 passed.

Final delta after combat stage 4 [L, 2026-11-20 20:59-21:08 EST]: main c5a4d2949 (with stage 4 cb8575020 and 0625)
merged without a conflict into head c3d4920f6. Stage 4 changes proxy, server and render sources and no release,
installer, options or purity file. With the shared source and deps and no special environment: `build_proxy` OK in
73 s (purity cached, same 26 inputs), `build_server --config release` OK in 161 s, options 742 passed on x86 and x64,
`test_safety_static` 55, `test_purity_drops` 10, `test_installer` 49, export check PASS. No release test reads a file
stage 4 changed, so none was rerun.

### 6.6 Read-only copies from the shared pins folder (ticket 0629) [L: reproduced and fixed, 2026-11-20 22:18-22:40 EST, workstation; C: code reading of the release path]

**Finding [L].** `shutil.copy2` copies the Windows read-only attribute. The shared folder is read-only by design, so
`server/build_server.py` (step 3, `copy_bin`, through `tools/x64-dll/linkcheck.py` `copy_checked`) wrote read-only
copies of the x64 DLL, the three stub DLLs and their PDBs into `<out>/bin`. lead-0042 #8845 (22:05) found it when
`proxy/tools/test_proxy_rt.py`'s second `layout()` failed. Reproduced without a build
(`C:/c5-64bit-work/0629/repro.py`, copies from `deps-x64-release`): the copies are read-only, `shutil.rmtree` of the
bin fails with WinError 5, and a second copy into the same bin also fails (PermissionError 13 when it overwrites a
read-only copy). That second failure means a rebuild into the same `--out` failed too. The shared folder kept its
attribute. `proxy/build_proxy.py` copies no file from `--src` (it compiles), so it was not affected.

**Fix.**
- `linkcheck.copy_checked` now clears the read-only attribute on each copy (`linkcheck.make_writable`).
- Before overwriting or removing an earlier copy, it makes that copy writable, so a bin left read-only by a build
  before 0629 is rebuilt cleanly.
- `tools/release/pack.py` copies through `copy_writable`, which is `copy2` followed by clearing the attribute on the
  copy.
- The source is never changed.
- Tests: `tools/x64-dll/tests/test_linkcheck.py` (read-only source gives a writable copy and the source keeps its
  attribute; a read-only earlier copy is replaced and a stale one removed; the negative: `copy2` alone keeps the
  attribute) and `tools/release/tests/test_pack_writable.py` (a read-only layout gives a pack with no read-only file,
  the layout keeps its attribute, and the negative).
- After the fix the repro gives writable copies, a clean `rmtree` and a clean second copy.
- `proxy/tools/test_proxy_rt.py` is unchanged.

**Rule.** A tool that copies from a shared or kept folder (`C:/c5-64bit-work/keep/...`) clears the read-only attribute
on its copy and never on the source. `shutil.copy2` and `copy` keep the attribute; `shutil.copyfile` does not.

**The release path: a read-only file cannot reach a player's install [C, with L checks].**
1. The take does not read the shared folder. `tools/release/stages/build.py` builds from its own `git archive` of the
   pin and its own `build_all` deps (for 22 civilisations `build_server.py` without `--deps`; for 43 a `build_all` into
   `<build>/deps`). Its files are written by the take, so they are writable. Ticket 0626's rehearsal take (22:30, read
   only): `build.json` names `src` `C:/c5-64bit-work/0626/std/run/src/x64-1b87dd18` and its own `deps`. Of 21,972 files
   under `C:/c5-64bit-work/0626`, none is read-only, and its pack (7 files) has none [L].
2. Before 0629, a read-only file could reach a pack only through a hand-run `pack.py` on a layout built with
   `--deps <shared folder>`. `pack.py` now clears the attribute.
3. The installer payload cannot carry the attribute. `tools/installer/build_installer.py` `stage_variant` stages every
   pack file with `shutil.copyfile` (a new file with default attributes; checked [L] on this machine: a copyfile of a
   read-only file is writable) or `write_bytes`. Inno Setup extracts that payload to `{tmp}`.
4. Installed files: `install.iss` `P64PlaceFile` copies the payload file to `<dest>.p64tmp` (`FileCopy`) and renames it
   over the destination (`MoveFileEx`, replace). `P64DeleteOwn` clears the attribute before every delete, so removal
   works on a read-only file the installer owns. A `MoveFileEx` replace over a read-only destination would fail, but no
   read-only destination can come from the installer, since its payload is writable.
5. Not verified: a player who sets the attribute on an installed file themselves [I: the next update's replace of that
   file would fail with a named error]. No game or installer was run for 0629.

## 7. The rehearsal take of the first beta (ticket 0626, D216, 2026-11-20/21)

This was a REHEARSAL. Nothing of it goes to the owner or to GitHub. Every name carries `rehearsal`: the release name
`0.1b-rehearsal`, the TEST installer, and the descriptor `Release-5.4.8-i3-rehearsal.json`. D150, D162 and D27 are not
affected. The purpose was to run the release path once, end to end, on main after 0616's pin switch, so that its
faults are found on Saturday and not at Sunday's candidate (0601).

### 7.1 Inputs and method

- Repository: main `e9cea869a` (0616 merged), branch `task-0626-beta-rehearsal-take` at `a4c1c8428`. It does not
  contain 0623 (merged later, `a7162f560`).
- Drivers: `tools/release/evidence/0626/`:
  - `s02`: the fork clone (shared objects, no checkout) and the descriptor;
  - `s10`: the standard take;
  - `s11`: the 43-civ take up to pack;
  - `s15`: the installer;
  - `s20`: install, play, hi and remove on one instance;
  - `s30`: stage times;
  - `s31`: the read-only check;
  - `s05`: the disk guard.
- Pins: `deps/dll.lock` x64-0.1b `1b87dd185` and `deps/i0.lock` i0-0.1b `f33fc46a4`. The core database is the pinned
  `dec46218...` (ticket 0612, `deps/local-settings.ini core_db`).
- Descriptor: SHA-256 `1d0ed18e...`, made of `releases/Release-5.4.8.json` + `shell_config i3` + this option set:
  - `[Renderer]`: ServerUnits 1, ServerCities 1, ServerLandmarks 1, ServerArc 0, ServerPath 0;
  - `[Performance]`: AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1, GameEventsOneCrossing 0.
- **ServerCombat is not in the set:** options id 28 is not on main, so it was not written (MC #8752 Q5). A take after
  it lands must add it.
- Command: `take_release.py Release-5.4.8 --tier validated --carry pins --for-art --descriptor <it> --fork <clone> --work
  C:/c5-64bit-work/0626/std/run --who impl-0626`, with `C5_RELEASE_INSTANCE` and `C5_RELEASE_HI_INSTANCE` = c5h-new-a
  (Goldberg, build 26577625, mods form) and `C5_RELEASE_TESTBEDS=local`.
- Work folders are on C: (MC #8752: not on F:, the USB disk the instances use). A start needs 65 GB free, and the disk
  guard stops the take below 50 GB (MC #8754).
- The driver builds its own x64 source and deps from git archives of the pins. It has no option to reuse
  `keep/pins-0.1b`, so the rehearsal paid the LTO link as Sunday will.

### 7.2 The standard take, stage by stage [L, 2026-11-20 21:28-, workstation, other teams' work running]

| stage | result | minutes (rc0b 2026-11-19 for comparison) |
|---|---|---|
| survey, branches (skipped, `--carry pins`), lock | ok | 0.1 |
| testdata | ok, the pinned core database (0612's fix holds) | 2.1 (1.0) |
| build | ok 21:31-22:09: x64 (LTO) about 17 min, shell, i0, i0ia (row 4968's patch on f33fc46a4, exit 0). Layout `x64-1b87dd18-pa4c1c84`: x64 DLL `BD431BFE...`, server.exe `0B5F9D95...`, proxy `59061751...`, compositor `F8F44AA2...`. Marker defines `VP64_HIDDEN_EMBARK`, `VP64_SPEED_DANGER`, `VP64_SPEED_DANGER_DG7`. | 38.3 (32.6) |
| narrowing | 794 sites / baseline 798 / 0 new / 4 gone | 0.0 |
| census | ok | 10.1 (5.1) |
| model, purity | ok: 135 methods and 27 Lua names, derived at 1b87dd185 (the 6.4 re-baseline, no allow-list) | about 3 |
| pack | ok; pack (7 files) and layout (34 files) have **0 read-only files** (MC #8846; s31) | 0.0 |
| offline | ok | 11.5 (2.2) |
| savetest | ok | 22.5 (17.8) |
| drift, fixchecks, refsave | ok | about 4 |
| instance | **ok on c5h-new-a, build 26577625** (23:10-23:28): the FIRST run of the first beta's pins on that build. arm tiny 54,531 proxy answers verified, arm huge 309,426, 0 mismatches, 0 lock errors; vpproxy.log `known and supported: the 64-bit path starts`, LOUD 0, FALLBACK 0 in both arms; server.log `hidden-embark: ... the DLL's feature version 1; told the DLL off` (the stage's test pack keeps HiddenEmbarks 0) | 18.2 (14.9) |
| savetest rows | 30,569 / 359,261 / 685,488 = the descriptor's reference counts (= rc0b) | |
| wholegames | ok 23:28-01:24, one local host, **4/4 x64 = x86 identical**: tc Tiny Continents (turn 445 Diplomatic, 1,381,434 rows), sf Standard Fractal (401 Science, 2,098,323), sp Standard Pangaea (363 Cultural, 1,569,886), mp Small Pangaea (325 Diplomatic, 945,978) | 115.4 (91.5) |
| hi_journey, run 1 (01:24-01:46) | on c5h-new-a, stage-laid layout, build 26577625. release-basic product **passed** (519 s); release-basic stock replay **passed** (453 s), then 0xC0000005 at the quit step (01:44:33, pid 71496, no box): ticket 0484's exit crash of the instance-aware TEST DLL (`ThunkOperatorNew<2>` after the host's `FreeLibrary`; root cause [C] by 0484's dump, accepted for this hit by the MC, #8978; no WER dump of this pid). Then the operator wrote the take's STOP file while the release-large product game ran (01:46:30). The stage went to its restore with the game alive, `set-proxy --original`, `set-dll` and `frontend restore` were refused ("CivilizationV_DX11.exe is running"), and the take ended **EXIT 1** ("RESTORE OF c5h-new-a FAILED"). Repaired by hand: launcher's stop with `stop_readiness`, `set-proxy --original`, `set-dll` back to `ff0f6e86...`, front end equal by SHA-256, `verify --quick` ok (01:50) | 22.5 |
| hi_journey, run 2 (01:51-02:11, `--only hi_journey`, `C5_RELEASE_HI_JOURNEYS=release-large`) | release-large product **passed** (459 s), stock **passed** (405 s, no crash), 5/5 checkpoints the same; the stage restored and released c5h-new-a itself. The driver's D162 line is "for the owner: NO, release-basic was not played" (exit 10), correct for a run of one journey | 19.9 (37.8 for both journeys) |

**D162 on build 26577625 [L].** Both release journeys passed on both routes on the first beta's pins, but across two
runs, not in one. For the rehearsal that counts (MC #9008). For the candidate, one run of the take's own hi_journey
stage must carry both journeys, and the driver's own D162 line must read YES. Assembling from reruns does not count.

**The queue guard on build 26577625 [L, logs].** The proxy's guard is on: arm-huge `vpproxy.log` has "queue guard: UI
message queue guard active (DX11 build; ...) (ticket 0492)" (no address copied, D202). The guard that is off is the
**instance-aware TEST DLL's** (the -ia build the stock routes of tests run): `crashlogs/queueguard.log` has "UI message
queue guard off: not the EXE build this was written for (timestamp)" three times on c5h-new-a (11-19 22:58, 11-20
02:01, 11-21 01:36). The shipped 32-bit pin never had such a guard. A player with the patch off runs their own VP DLL.
So this is no release condition from the 32-bit pin and no known-issues line (MC #9164, D224, D225). Off-mode guards
are ticket 0635 as rewritten. It is not the cause of the crash above. (Corrected 2026-11-21, MC #9164; replaced:
"The 32-bit DLL's own guard is off ... That DLL runs in the EXE only when the player switches the patch off ...")

**Wall time of the standard take.** 21:28:5x-01:46:48 (4 h 18 min) up to the stop. One uninterrupted hi_journey with
both journeys and both routes takes about 40 min (rc0b 37.8), so an unbroken take is about **4 h 35-40 min** on a
shared workstation. The slow stages compared with rc0b are wholegames (+24 min), offline (+9), census (+5), build
(+6) and savetest (+5).

### 7.3 The 43-civilization take (candidate stages to pack) [L, 2026-11-21 02:12-02:50]

`s11_take43.sh`, the same descriptor, `--major-civs 43`, `--tier candidate --only <stage>` for survey, branches,
testdata, lock, build, model, purity and pack: every stage exit 0. Times: testdata 98 s, build 1,791 s (29.9 min), model
244 s, purity 100 s, pack 1 s; 38 min in all. Layout `x64-1b87dd18-m43-pf3a6693`. The tools commit differs from the
standard layout's (`a4c1c84`) because tracker commits went in between. `build_installer.py` accepted the two variants
all the same. No hi_journey: no granted instance carries the 43-civ Vox Populi (gap named; 0580's m43 journeys need
one). Pack and layout have 0 read-only files.

### 7.4 The installer [L, 2026-11-21 02:50-02:51]

- (a) The release path without `--test-build`, run `--stage-only` with `--art-check-waiver m43 "D199 ..."`:
  **REFUSED, exit 4** by D162's `--art-check` of the standard work folder ("played on proxy_dll 59061751... server
  0b5f9d95... x64_dll bd431bfe... (a build for anybody but the owner: --test-build)"). That is correct: the take's D162 line
  is NO. The waiver path of m43 was not reached. Driver lesson: in `s15` the first version printed `exit 0` for the
  refusal, because `$?` came after a `$(date ...)` in the same `echo`. Read the tool's own exit code first.
- (b) The TEST build: `VP64-Patch-0.1b-rehearsal-for-VP5.4.8-TEST.exe`, SHA-256 `42497629f440799f...`, 17,541,760 B,
  release 0.1b-rehearsal, VP 5.4.8, i3, variants std+m43, built in 26 s. Its folder has 0 read-only files.

### 7.5 Installer-laid runs (`s20_install_run.sh <instance> install | play | hi | remove`) [L, 2026-11-21]

No developer switch and no `--env` in any run. The player's state is set first: `provision.py set-dll` with the tag's
DLL `262aee88...`. The installer runs silently through `evidence/2600/run_installer.py` in test mode.

| instance (build, form) | install | load + 10 turns + Lua exit | removal and tree comparison |
|---|---|---|---|
| c5h-new-b (26577625, mods), 02:53-03:41 | exit 0 in about 25 s; the log has `Game EXE: ... Steam build 26577625`, `the standard variant (22 major civilizations) of the patch fits it` and no "does not support yet". VP64Options.ini holds the whole set + `ServerRenderer = 1`; My Games differs by exactly the installed files; the DLC tree is unchanged | copy `C5H-0626-HUGE_0350` of HUGE_0350, turns 350 -> 360, 393 s from launch; `UI.ExitGame` ended the EXE in 7.1 s. vpproxy `boot: pack ... VP64 ok` and `known and supported`. **HiddenEmbarks = 1 from an installer-laid pack on build 26577625, the first time: on, told the DLL on, 275 calls, 0 missed reveals**. LOUD: vpproxy 0, render 0, server 200 (the cap of 0576's stale-unit guard on this save, as 0620's run M), composite 1 at the exit (0623 B, fixed on main `a7162f560`, not in this tree); FALLBACK 0; no box | removal exit 0 in 3 s; VP's DLL back from its backup; **My Games and the DLC tree identical by SHA-256 outside the game's run outputs**; set-dll --previous `ff0f6e86...`; verify --quick ok |
| c5h-new-mpk (26577625, modpack), 03:42-03:51 | exit 0 in 8 s, `install 0.1b-rehearsal (std, i3, modpack form)`; the DLC gained the 18 product rows (as 0620) | a copy of 0620's turn-19 autosave (rotated into `Saves\single\auto\prev\0ldAutoSave_0019` by a later game). There is no mid-game modpack save, so that gap is named. Turns 20 -> 30, 204 s; Lua exit 6.2 s. HiddenEmbarks on; the server drew 11,000 units, 633 city and 4,906 landmark keys acked; world drawn. LOUD: vpproxy 0, render 0, server 2 (0576), composite 1 at the exit (0623 B); no box | removal exit 0 in 12 s; My Games identical; the DLC kept `VP_MODPACK\Logs\shell_composite.log` (**0623 A**, fixed on main, not in this tree). Moved to the evidence by hand; after that the DLC equals the listing before except the DLL put back by set-dll --previous; verify --quick ok |

| c5h-d (4390913, mods), 2026-11-21 10:53-11:05 (after the account's limit pause) | exit 0 in 9 s; `Game EXE: ... Steam build 4390913 (PE stamp 0x546CD0A8 ...)` | HUGE_0350 copy, turns 350 -> 360, 503 s from launch; Lua exit 16.4 s; `known and supported` for 4390913; HiddenEmbarks on (275 calls, 0 missed). LOUD: vpproxy 0, render 0, server 200 (0576), composite 1 at the exit (0623 B); no box | removal exit 0 in 4 s; My Games and DLC identical outside the run outputs; the DLL from before the run (`3c2f47da...`) put back by set-dll --previous; verify --quick ok |

### 7.6 The D162 journeys on the INSTALLED layout (c5h-new-b, 03:03-03:40) [L]

`C5_RELEASE_HI_INSTALLED=c5h-new-b`, both journeys, both routes (MC-B 02:17):

| journey | product route | stock route |
|---|---|---|
| release-basic | **passed**: 63 steps; founded, research, turn5, turn10, loaded, menu ok; 533 s | exit 5 at step 8: the `in_game` judge timed out (60 s), "channel silent" |
| release-large | **passed**: 21 steps; moved, attacked, city, camera, menu ok; 438 s | exit 5 at step 6: the same judge |

**Why the installed stock route cannot pass [C: stages/hi_journey.py and the stop records].** On the installed layout
the stock route is vpproxy.ini `mode=off`. That loads the installed fallback, VP's own 32-bit DLL. The stage needs
"no instance-aware 32-bit DLL" there, so there is no Lua channel, and the journeys' judges read the game through the
channel. The game itself was in play (turn 0, 4000 BC), but with a **deep red map**: the `mode=off` flip leaves
config.ini in the D33 form that `vp64helper on` wrote, so the stock 32-bit game runs with its systems disabled (the
A'/A'' red map of release-driver.md W3b/3516). Before this, no installed-layout run had played the stock route (rc0,
rc0b: product only). **MC #9064 (option A):** for the candidate, the take's own hi_journey stage plays both journeys
on both routes on the stage-laid layout in ONE run (its D162 line counts). In addition, both journeys run on the
installer-installed layout on the PRODUCT route only. The installed stock route is not played. The red map of a
hand-set `mode=off` goes to impl-0635 as a guard (NO PLAY + a D118 report).

### 7.7 Faults, ordered by what would stop Sunday's take (2026-11-21)

| # | fault | would it stop Sunday? | owner / state |
|---|---|---|---|
| 1 | The UI message queue guard that is off on build 26577625 ("not the EXE build this was written for") is the instance-aware TEST DLL's. The shipped 32-bit pin never had one, and a player with the patch off runs their own VP DLL (corrected 2026-11-21, MC #9164; it read "the 32-bit DLL's guard ... a release condition") | no: no release condition from the 32-bit pin, no known-issues line | off-mode guards: ticket 0635 as rewritten (D224 `QueueGuardWhenOff`, D225 `GuardsWhenOff`) |
| 2 | A take's STOP file written while a journey's game runs: the hi_journey stage restores with the game alive, every restore step is refused, and the take fails (EXIT 1, the instance left with the test layout) | yes, if an operator does it | driver ticket 0634 (backlog p7). Operator rule meanwhile: no STOP file while a journey's game runs (MC #8978 (5)) |
| 3 | The installed-layout stock route cannot pass: VP's own 32-bit DLL has no Lua channel for the judges, and the `mode=off` flip runs with the D33 config.ini (red map) | no: ruled out of the candidate's steps (MC #9064, option A: installed layout product route only) | a driver ticket draft in the hand-back (the installed stock route switched off as a player does, with a channel). The red map of a hand-set `mode=off` goes to impl-0635 (NO PLAY + D118 report) Fixed by 0640 (main 87a5e2efd, 2026-11-23). |
| 4 | The instance stage claims without `--ticket`/`--address` (`stages/instance.py` `claim_one`), so a box goes to the MC's desk | no | draft for epic 0630's lead (in the hand-back). Work-around: `pool.py transfer <I> --by <who>-release --ticket <id> --address <addr>` Fixed by 0640 (main 87a5e2efd, 2026-11-23). |
| 5 | The modpack form leaves `VP_MODPACK\Logs\shell_composite.log` in the game folder after the removal (0623 A); one LOUD `fow_models` line at every exit (0623 B) | no: fixed on main `a7162f560` (0623), not in this rehearsal's tree | done; Sunday's take on a main after `a7162f560` carries it |
| 6 | The stage-laid stock route's exit crash at the journey's quit step (instance-aware TEST DLL, 0484) | no: named, not counted (MC #8978) | 0484 (test builds only) |
| 7 | Whole games took 115 min on one local host (rc0b 92). The take is about 4 h 35-40 min in all | it sets the schedule | MC #8876: more local hosts (`C5_RELEASE_TESTBEDS=local1,local2`) where MC-B's places allow |
| 8 | `echo "... $(date) ... $?"` prints the exit of the command substitution, not of the tool (s15's first version showed 0 for a refused D162 check) | only if the operator reads a wrapper's line | operator note: read the tool's own exit line |
| 9 | ServerCombat (options id 28) was not on main at the take, so the rehearsal's set lacks it | no, if the candidate's set is fixed before the take | the MC (#9012: the candidate's set unchanged tonight) |
| 10 | No mid-game modpack save exists; 0620's turn-19 autosave was rotated into `auto\prev` by a later game | no | a save of this ticket's 30-turn game exists only as autosaves on c5h-new-mpk |

Build 4390913 (c5h-d) was run at 10:53-11:05, after the account's weekly limit had paused the work from 03:52 to
10:51; c5h-d was untouched in between. Not run: the 43-civ variant's journeys, because no granted instance has the
43-civ Vox Populi.

### 7.8 Wall time of the chain, for Sunday's schedule (measured 2026-11-20/21, a shared workstation)

| step | measured | note |
|---|---|---|
| standard take, survey to wholegames | 21:28:5x-01:24:03 = 3 h 55 min | build 38, census 10, model 9, offline 12, savetest 23, instance 18, whole games 115 |
| its hi_journey, both journeys and both routes in one run | about 40 min | run 1 (basic) 22 min, run 2 (large) 20 min |
| 43-civ take, survey to pack | 02:12:36-02:49:59 = 37 min | build 30 |
| installer, TEST build | 26 s | the non-test build runs the same compile after D162's check |
| per installed instance: install, load + 10 turns + Lua exit, removal + tree comparison | 2 + 7 + 2 min (Huge t350); 1 + 5 + 1 min (modpack t19) | |
| installed-layout journeys, product route, both journeys | about 20 min (basic 9, large 7.5, plus launches) | the stock route is not played (MC #9064) |
| **whole chain, one take + 43-civ + installer + one installed instance with its journeys** | **about 6 h 05 min** | with a second whole-games host, about 1 h less |

**Finding (driver, minor).** The instance stage claims its instance without `--ticket`/`--address`
(`stages/instance.py` `claim_one`). The pool then shows `!! NO TICKET`, and a box on the instance would go to the MC's
desk instead of to the take's owner (D155 part 4, ticket 0388). Work-around used: `pool.py transfer c5h-new-a --by
impl-0626-release --ticket 0626 --address dispatcher-B` (23:13). The hi_journey stage already passes
`C5_RELEASE_HI_TICKET`/`C5_RELEASE_HI_ADDRESS`.
