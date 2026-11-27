# The DLL commit of the upstream export on current upstream master (ticket 4705, E4a, 2026-11-18)

Package E4a of the export plan (`../07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md` 4.5,
commit 1): the x64 changes of the DLL, rebased on upstream master as fetched on 2026-11-18, as one commit on a new
fork branch, behind defines (D104), with the proof that the 32-bit build is unharmed on that base. **Nothing was sent
upstream** (D95, D130, D27): the branch is on the owner's fork only, no pull request. The interface version (E4b) and the
43-civ variant are not part of it. Draft E of `upstream-drafts-3505.md` (the CI job and its script) belongs to package
E5 and plan 4.7, not here (MC answer #5165 on 4705, 2026-11-18).

Marks: `[C]` read in code or git, `[L]` seen in a build or run, `[I]` inferred.

## 1. The result

| | |
|---|---|
| Base | upstream master **`3a511df0fb9f69ef70ac569aea259c5e1dd9eabe`** ("Fix build errors", 2026-11-17 23:44:40 -0500), fetched 2026-11-18 04:46 EST; 24 commits after 3505's base ba8aafdca, 176 after Release-5.4.8 |
| Branch (current) | fork **`export-e4-dll-r2`** = **`73ee473e0d86a316261a23e4ad40ab1181dfea9e`**, one commit on the base, tree `72d5c9759f7e`, pushed to the owner's fork on GitHub only (the `repository` of `deps/dll.lock`) (2026-11-18 06:33 EST). It replaces `export-e4-dll` `fd3d9e171` (05:03 EST, **same tree**, left untouched on the fork, no force-push, MC answer on 4705): only its message changed, because the first one claimed byte-identical x64 saves, which the save test refuted without D18 (section 5.4) |
| Message | `Build the DLL for x64: compile fixes, crash handler, out-of-process build, 32-bit save widths`; body = drafts A-D of `upstream-drafts-3505.md`, corrected for master and for the save test (section 3; text `evidence/4705/e4-commit-msg-r2.txt`, the first one `e4-commit-msg.txt`); no internal ids, project names or cherry-pick lines (D95) [C]; author is still the fork's usual identity (the GitHub no-reply address is the export's step) |
| Content | 18 files, +214 / -81 (`evidence/4705/e4.diff`, in `tools/x64-dll/evidence/4705/`) |
| Proofs | (1) 32-bit master vs branch: not byte-repeatable even master vs master, so the save test games: **identical** (5.2, 5.3). (2) x64 release build: 0 errors, 0 unresolved, narrowing 0 new after 10 benign sites triaged (5.1); save test x86 vs x64 on the branch: behaviour **identical**, saves identical except the hash order of the three D18 containers (5.4); with D18 added (diagnostic arm, not the export) the full save test (5.6). Save-width census: 0 x64-only writes (5.5) |

## 2. How it was made [C, L 2026-11-18 04:46-05:05 EST]

Own clone `C:/c5-64bit-work/4705/fork` (a local clone of the long-lived fork clone, hardlinked objects; `origin` the
fork on GitHub, `upstream` LoneGazebo), fetched `origin` `x64-batch4`, `i0-ship-batch4`, `x64-release-master-ba8aafdc-r2`
and `upstream` `master`. Scripts: `tools/x64-dll/evidence/4705/scripts/` (`s01`-`s08`).

**What the x64 line carries beyond its 32-bit base.** `git diff e96457fd5 c29442e8c` (i0-ship-batch4 -> x64-batch4) is
13 files, +141 / -67, and is exactly the five port commits of 5.4.8 (b25fd45c0, 8e26515e8, 21f396027, f3d2f5669,
47d7664e9): the D18 commit e9d0c4837 and the three batch-4 x64 fix picks (ceadc7549, 15d5f1e7d, c29442e8c) each have
the patch id of their i0 twin and cancel out (`pin-x64-only.diff`). So the port's own content at pin batch 4 is the
same as at 3505's time, and 3505's upstream-shape commits on master ba8aafdca (fork branch
`x64-release-master-ba8aafdc-r2`, picks 972cf559f A, 2f83d30a7 B, a632ba4fb C, c8c72011b D, bc6c97daa D) are its
upstream form.

**The rebase.** Those five picks, cherry-picked in order onto `3a511df0f` (branch `e4-parts`, local only):

| Pick | Result on 3a511df0f |
|---|---|
| 972cf559f A (compile fixes) | clean; all 7 `make_pair` sites, `TContainer.h`, `!CvLuaApi.h` still apply |
| 2f83d30a7 B (crash handler) | **conflict** in `CvGlobals.cpp` at the "address space low" test: master now uses the largest free block over the whole range on every build ("the exe is large address aware and uses memory above 2 GB on 64-bit Windows"), which is what B did for x64 only. Resolved to master's line; the bullet left B's text |
| a632ba4fb C (`VP_OUT_OF_PROCESS`) | **conflicts** in `Exe/CvExe.cpp` and `Exe/CvExeApi.cpp`: upstream #13581 ("Remove the old Palace model from a captured capital", 1fb49429f) added a third EXE feature, `Exe::CanRepairCityArt` / `TryRepairCityArt` (new `CvExeAddresses.h` entries, `CITY_ART_SYMBOLS`), and #13582 (clang-tidy) touched the files. Re-ported by hand on master's files (`scripts/port_cvexe.py`, `port_cvexeapi.py`): the same guards as before, plus the out-of-process `CanRepairCityArt` (bin_hooks_off / unsupported_exe, as the other two) and `TryRepairCityArt` (refuses and logs, `kCity` unused). Its only callers, `CvPlot::RepairCityArt` and the check before it (`CvPlot.cpp` 14050-14065), test `Can*` first, so out of process the repair is never tried |
| c8c72011b D (save widths) | clean |
| bc6c97daa D (`Initialize2DArray`) | clean |

The five picks' tree became one commit on the base (`git commit-tree`, `scripts/s08_squash.sh`): plan 4.5 asks for one
DLL commit whose description is the sum of the drafts. `e4-parts` (the five picks with their old messages, which carry
cherry-pick lines) is not pushed; the squashed commit holds every resolution, so nothing is lost when the clone goes
(the 3510 rule).

**Compile check** [L, 2026-11-18 04:59-05:00 EST]: `tools/x64-dll/build.py --src <worktree of e4-parts> --unlocked
--config debug --census`: 178/178 TUs, **0 errors** (222 `-Wdeprecated-declarations` warnings), census link 327
unresolved closed-library symbols (the same count as 3505's census of ba8aafdca, section 12.2 of `x64-dll-port.md`).
So master's 24 new commits add no x64 compile break.

## 3. The commit message against the drafts

Changes from drafts A-D of `upstream-drafts-3505.md`, each forced by master:
- B: the bullet "On x64 the 'address space low' classification looks at the largest free block overall" is gone
  (master does it on every build now, section 2).
- C: names the third EXE feature ("the city art repair") among those that answer as for an unknown EXE.
- One message for the four drafts (plan 4.5); the testing paragraphs of the drafts are not in the commit: they go into
  the pull request text with this ticket's numbers (section 5).
- The texts carry no "D10", ticket or project ids; checked by grep of the added lines for `D[0-9]+`, issue, epic,
  ticket, tracker, VP64, claude, server: no hit [C].
- **Corrected 2026-11-18 06:33 (r2):** the first message (on `export-e4-dll`) said the changes "write saves on x64 that
  are byte for byte those of the x86 build". On master without D18 that is false (section 5.4: the three hash
  containers come out in each STL's hash order). The r2 message says "in the x86 build's save format, so that each
  build loads the other's saves" and adds a paragraph to the save-width part naming the three containers and that
  writing them in key order is a separate change. Same tree; scrub grep of the r2 message: no hit [C].

## 4. The 22 upstream-authored commits of the x64 line

The plan counted 16 commits by upstream's own people on the x64 line (51 commits, 2026-11-17). At pin batch 4,
`Release-5.4.8..x64-batch4` has 55 non-merge commits, **22 not by the owner** (`upstream-authored.tsv`, script
`s07_upstream_authored.py`). None is in the x64-only diff (all came in through i0-ship), so **every one drops out** of the
export branch, which carries only the port. Against master `3a511df0f`:

| Group | Commits | In master? |
|---|---|---|
| Upstream fix batch 1 (17 picks) and #13566 (batch 3) | b9414f9d6 #13548, 3c835840e #13550, ee0948b56 #13481, 36dbb08bc #13472, 2d0faba37 #13466, eac0390e9 #13530, 5a36cfbf8 #13527, 4782a6276 #13528, 051c5503d #13529, d0e9c3ccb #13447, 4ac6f1185 #13496, dc5e22917 #13421, 80a074c6f (c0188334e), c00498108 (c094e12f0), 272e94ba3 (ff578c1a3), a5f53048d #13494, bbfacca1a #13474, 9cf38e3ad #13566 | **yes, all 18**: the upstream commit is an ancestor of master and master has a commit with the same patch id |
| Taken from open upstream pull requests (batches 3 and 4) | 2eab5a023 + cfa39654f #13482, c125463b1 #13395, 71381245e #13522 | **no**: not merged upstream on 2026-11-18 (no ancestor, no PR number in master's subjects, no equal patch id). They stay fixes of our pins; they are not port and are not carried by the export commit |

## 5. The proofs

### 5.0 How they were run

The release driver (`tools/release/take_release.py`, `release-driver.md` W7.3) with the one-row series
`tools/x64-dll/evidence/4705/series-e4.tsv`: no i0 rows, so its 32-bit branch is upstream master itself and its x64
branch is master + the export commit. Command: `scripts/s10_driver.sh` (`master-3a511df0 --base-commit 3a511df0f
--base-release Release-5.4.8 --series series-e4.tsv --fork <own clone> --work C:/c5-64bit-work/4705/rel --scratch
F:/c5-64bit-work/4705/scratch --skip-ia`). `testdata` built master's game data (05:05-05:08 EST): the game folder with
`VP_MODPACK_MASTER_3A511DF0`, the database `ce0172c1...` (c5h dbbuild of master's (1), (2), (3a) mods), the 5.4.8
localization; every proof below runs on that data (the 5.4.8 database does not fit master's DLL, 3513).

**The narrowing gate stopped the first x64 build** [L, 05:08-05:11 EST]: 799 unique `-Wshorten-64-to-32` sites
against the baseline's 798, **10 new, 9 gone**, the same sites 3505 found on ba8aafdca (`x64-dll-port.md` 12.2): five
keys, all `lua_tointeger` (a 64-bit `lua_Integer` on x64) into an `int` in Lua bindings: `!CvLuaApi.h:51`
(`luaEx_to<int>`), `CvLuaPlayer.cpp` plot x/y arguments at 2527-2528, 12555-12556, 12571-12572, 12712-12713,
13764-13765 and a setting at 12615, 12627, `CvLuaPlot.cpp:2330` (an ID). **Triage: benign** [C]: Lua 5.1 numbers are
doubles holding game ints (plot coordinates, an option value, an object ID), the class of the baseline's 554
`lua_tointeger` rows; nothing is saved or hashed from them. The gone sites are master's rewrites of the same Lua
glue and of `CvDealClasses.cpp` / `CvPlayer.cpp` loops. Unblocked as 3507's dry run did (W7.2 row 8): the driver runs
from a `git archive` of this branch (`scripts/s11_repo_copy.sh`, `C:/c5-64bit-work/4705/repo`) whose copy of
`narrowing-baseline.tsv` has the 10 sites added (`scripts/add_triaged_rows.py`, the rows in
`evidence/4705/narrowing-triage.tsv`). The repository's baseline is not changed by this ticket: it describes the
pins' 5.4.8 source, and a release cut from master writes its own (`--write-narrowing-baseline`, the release's step).

### 5.1 The x64 release build of the branch [L, 2026-11-18 05:15-05:29 EST, workstation]

The driver's `build` stage (`driver-2`, from the tools copy, `--from build`), `branches.py`'s x64 branch
`x64-release-master-3a511df0-c5bdac6e3` = bd556a16a, a pick of fd3d9e171 onto the base whose **tree is the export
branch's** (`72d5c9759f7e`, `branches.json`); built in a detached worktree with `--unlocked`:
`server/build_server.py` -> `tools/x64-dll/build_all.py --config release` -> `build.py --config release --shorten`:
- every `build_all` step rc 0 (stubs, cvgamedatabase, cvlocalization, dll, host, smoke on master's database);
- compile 63 s, **whole-program (LTO) link 436 s** under the machine's one link place (taken after 0 s: no other LTO
  link ran, D107);
- **narrowing gate: 799 sites, 0 new, 9 gone** against the copy's baseline (808 = 798 + the 10 triaged);
- **0 unresolved**; repeatability check OK (186 link inputs, 537 members, none stamped);
- **x64 DLL `7984304767EB22924394F7499FC83384931D5AD08AC5FF4C10A05F8CF111E8AA`, 16,574,976 B** (version string
  "Release-5.4.8-177-gbd556a16ac Clean": a git worktree), `server.exe` and the proxy built beside it (layout
  `C:/c5-64bit-work/4705/rel/layout/x64-bd556a16-punknown`; `punknown` because the tools are a `git archive`).
- The driver's 32-bit plain build of the i0 branch (= upstream master 3a511df0f itself), `build_vp_clang.py --config
  release` in a detached worktree, 492 s: `DA0940A161A9AF25C9BEB574E68752DF42DD29DBC6F1601961988E676D172743`.

### 5.2 Proof (1), part a: the two 32-bit DLLs byte for byte [L, 2026-11-18 05:38-06:00 EST]

The project's 32-bit build (the fork's `build_vp_clang.py --config release`, clang-cl 19.1.7 with the VC9 STL, as
`deps/i0.lock` and the driver's `build` stage), run three times **in one worktree folder** one after the other
(`scripts/s13_x86_builds.sh`, each under `quietlock.py run`, about 8 min each), so that every `__FILE__` path is the
same: upstream master 3a511df0f, then `export-e4-dll` fd3d9e171 (no define: the 32-bit build never sets
`VP_OUT_OF_PROCESS`), then master again.

| DLL | SHA-256 | Bytes |
|---|---|---|
| master (A) | `B514A454E16420167BB7F423B43A1F47C2E758270ECE731766B1F03DF9C83B37` | 13,421,056 |
| export-e4-dll (B) | `7AE900DC434734D5A9B117B07A3341E9B20BBEFD6D112123CE4A666B68C28791` | 13,421,056 |
| master again (A') | `757FD1224BB7778B32529733F08E9415C17C25157A676AC1735D32BF5B5CB55A` | 13,421,056 |
| master, the driver's build in its own folder | `DA0940A1...2743` | 13,423,104 |

- `perf/speedkit/dllcmp.py B A --allow-build-id`: **ERROR, exit 2** ("not a PE32+ image"): dllcmp reads x64 images
  only. So the comparison was made by `scripts/pe32cmp.py`, the same masks (PE time stamp, checksum, debug directory
  time stamps, CodeView GUID and age) for PE32 (`evidence/4705/x86cmp/*.json`):
- **A' against A (same commit, same folder): DIFFERENT**, `.text` 180 bytes in 171 runs, `.rdata` 3,513 bytes in 361
  runs, nothing else. The 32-bit build is not byte-repeatable even in one folder [L]; the driver's own note says why
  (`__DATE__`/`__TIME__` and the link time; `build_vp_clang.py` links with `/LTCG` and without `/Brepro`). The driver's
  build of the same commit in another folder differs more (`.rdata` 2,064 bytes longer: the longer `__FILE__` paths).
- **B against A: DIFFERENT**, `.text` 16 bytes shorter in its virtual size (11,089,653 against 11,089,669), so later
  code moved (5.5 million differing bytes); `.data` equal. The shared lines of the commit (`pair<...>` for
  `make_pair<...>`, `min<size_t>`, the `(uint)` casts, `Refuse` moved above the guarded block in `Exe/CvExe.cpp`, the
  lines added in `CvGlobals.cpp` that move later `__LINE__` values) change the 32-bit code a little even where the
  types are the same.

So byte identity is not reachable for the 32-bit build, and the commit changes shared source lines: the ticket's
alternative applies, the save test's games between the two 32-bit builds (5.3).

### 5.3 Proof (1), part b: the save test's games between the two 32-bit builds: PASS [L, 2026-11-18 06:00-06:08 EST]

`scripts/s12_x86ab.py --a <A> --b <B>` (one `quietlock.py run` job): run_d10's parts 1 and 2 for x86 against x86
(run_d10 itself only pairs x86 with x64). Both DLLs play the save test's Tiny (0-100, save at 25) and Standard (0-200,
save at 50) fixed-seed games in c5h with run_d10's x86 command (`--no-mods`, master's test database, game folder and
the 5.4.8 localization of the candidate descriptor, AI logging off, `--no-profiles`). Then `statscmp.py` on the two
`stats.db` (every table, every turn present), and every save both arms wrote loaded by each DLL from one slot path
and saved before any turn (`tools/i0/save-now.lua`), `savecmp.py` on the two resaves. Reports
`tools/x64-dll/evidence/4705/x86ab/`.

| Check | Tiny 0-100 | Standard 0-200 |
|---|---|---|
| Behaviour (`statscmp`, A vs B) | **IDENTICAL**, 16 tables, **35,904 rows** | **IDENTICAL**, 16 tables, **371,825 rows** |
| Writer (4 inputs each: both arms' early and final saves, resaved by A and by B) | 4/4 **IDENTICAL UNDER EXCEPTIONS** | 4/4 **IDENTICAL UNDER EXCEPTIONS** |
| The one exception | (a) the version string, `Release-5.4.8-176-g3a511df0fb Clean` against `...-177-gfd3d9e171c Clean` | same |
| Host | every run `completed`, exit 0, 0 Lua errors | same |

**Result: the export commit leaves the 32-bit DLL's games and save bytes unchanged on these games** [L]. The row counts
are master's (5.4.8's were 30,666 / 351,496; 3507 measured 35,034 / 350,774 on master ba8aafdca, 24 commits earlier).
Wall times (beside other teams' games; not timings): Tiny 34 s per arm, Standard 282 s per arm.

### 5.4 Proof (2): the save test x86 vs x64 on the branch [L, 2026-11-18 06:08-06:20 EST]

`tools/d10/run_d10.py --games tiny,standard --allow-subset --order-exception none` (`scripts/s14_d10.sh`,
`C5_RELEASE` = the candidate descriptor, `VP_DLL_FORK` = the own clone for its D18 check), x86 = the branch's 32-bit
DLL `7AE900DC...` (5.2), x64 = the layout of 5.1 (DLL `79843047...`, `server.exe` `5fb09baf...`). Huge not run: master's
builds do not load the 5.4.8 huge save (3517). Wall 707 s. Report `evidence/4705/d10-export-report.txt`.

| Part | Tiny | Standard |
|---|---|---|
| 2 behaviour (x86 direct vs x64 direct) | **PASS**: identical, 16 tables, 35,904 rows | **PASS**: identical, 16 tables, 371,825 rows |
| 1 writer (4 inputs, x86 resave vs x64 resave) | FAIL: 2,202-3,740 differing body runs per input, same body length | FAIL: same kind |
| 3 cross-load gates | the two "other DLL vs owner" continuations **identical** (31,919 and 32,485 rows); the 4 round trips FAIL like part 1 | same |
| D18 (run_d10's check) | neither build carries it (`e9d0c4837` not an ancestor, no equal patch id) | same |

**What the difference is** [L]: `scripts/s16_neither.py` compares the 16 failed pairs (8 part-1, 8 round trips) again
with `savecmp.py --d18-side neither` (both writers lack D18: the anchored D18 containers may hold the same records in
any order, nothing else may differ). **All 16 are IDENTICAL UNDER EXCEPTIONS** (`evidence/4705/d10-export-neither.txt`):
every differing byte lies in `CvExplorationPlotSet::m_aValues` (12 or 24 per save, the three exploration sets of
the players) and, in saves with enough cities, `CvCity::m_miInstantYieldsTotal` (up to 45 per save); counts and record
multisets equal; the anchors parse master's saves (64 player blocks, N = 37 strategies, the city blocks). So **x64
plays the same games as x86 and writes the same records; only the hash-table order of those containers differs**,
which is what D18 (sorted writes, `e9d0c4837`, a commit of its own) removes. The D25 orders and bug 0116's fixes were
not needed for identical behaviour on these two games [L, these games only].

MC answer on 4705 (2026-11-18, the question of 06:31): **choice (a)**: the DLL commit stays port-only (32-bit save
bytes unchanged against master, proof (1) strict); D18's sorted writes stay their own later commit (the 0081 draft,
`research/sources/0081-upstream/0081-pr-deterministic-order-body.md`); export + D18 is shown as a labelled diagnostic
arm (5.6). (Option (b), D18 inside the commit, would change the order of those containers in 32-bit saves against
master, so proof (1)'s writer part would itself need the order exception.)

### 5.5 Save-width census on the branch [L, 2026-11-18 06:25-06:31 EST]

The driver's `census` stage (`--only census`; `tools/x64-dll/save-audit/save-width-census.py --arch both` on the x64
branch's source, 338 s): **0 x64-only** save writes, 9 sites on each build, the same as 3505's census of ba8aafdca and
3507's of the release copy. So master's 24 newer commits add no save write that is 64-bit only, and draft D's list is
still complete. The driver's `narrowing` stage (`--only narrowing`) is ok (it reads the build's gate: 0 new).

### 5.6 Diagnostic arm, not the export: branch + D18: full save test PASS [L, 2026-11-18 06:31-07:02 EST]

Labelled diagnostic (MC answer on 4705): local branch `e4-d18` = `export-e4-dll` fd3d9e171 + a clean pick of D18's
`40ce81aee` (`FStlContainerSerialization.h`, 4 lines) = `b338bb23b`, never pushed (`scripts/s17_d18_branch.sh`,
`s19_d18_arm.sh`). 32-bit: `build_vp_clang.py --config release` in the same worktree, `063FB7A5...7470`. x64:
`tools/x64-dll/build.py --config release --shorten` (the copy's baseline, the driver's stubs), LTO link 409 s, narrowing
0 new / 9 gone, `E3773286...7B8E`, 16,579,072 B, in a copy of 5.1's `bin/` with only the DLL replaced (no smoke step).
`run_d10.py` Tiny + Standard as 5.4 (`evidence/4705/d10-d18-report.txt`):

| Part | Tiny | Standard |
|---|---|---|
| 1 writer | **PASS**, 4/4 identical, **no exception used** (both builds carry D18 and one version string) | **PASS**, 4/4 |
| 2 behaviour | **PASS**, 35,904 rows identical | **PASS**, 371,825 rows identical |
| 3 cross-load | **PASS** (continuations 31,919 / 32,485 rows identical; 4 round trips identical) | **PASS** (341,792 / 332,169 rows; 4 round trips) |

**D10 save-identity test: PASS** (Tiny and Standard; huge not run, 3517). So the export commit plus D18 alone gives
byte-identical x86 and x64 saves on master; nothing else of the pins' identity set (D25 orders, bug 0116, the
statistics retries) is needed for these two games [L, these games only].

## 6. Not verified

- **VC9 (MSVC 2008), upstream's own toolchain**, was not run: the 32-bit builds here are the project's clang-cl 19.1.7
  with the VC9 STL. Every 64-bit-only line is behind `_M_X64`/`_WIN64`/`_MSC_VER >= 1900`/`VP_OUT_OF_PROCESS` and the
  shared lines are C++03 [C], so VC9 compiles the same code [I: confirmed by upstream's CI or a VC9 build].
- **Only the Tiny and Standard games**: master's builds do not load the 5.4.8 huge reference save (3517), and no whole
  game was played on master's data (the testbeds hold 5.4.8 data). Both proofs rest on two games each.
- The out-of-process `Exe::*` answers (now including `CanRepairCityArt` / `TryRepairCityArt`) compiled and the server
  ran the save test with them, but no run called `Exe.*` from Lua or checked the refusal text [I from the code].
- **Upstream's new EXE feature `CanRepairCityArt` (#13581) is refused out of process**, so the repair of a captured
  capital's leftover Palace model never runs on the server route once the pin moves past 5.4.8 [C: `CvPlot.cpp`
  14050-14065 tests `Can*` first]. The EXE still holds that stale art there; whether the shell side should do the repair
  (the EXE is in that process) is open and not tried.
- The x64 crash handler never caught a real crash (as in 3505).
- The proxy and the hutest host of master were built by the driver but not run here (the proxy's Lua route on master
  is issue 3519); the save test runs the server directly.
- `pe32cmp.py` is an evidence script of this ticket, not a tool; `perf/speedkit/dllcmp.py` still refuses PE32 images.
- The four commits taken from open upstream pull requests (#13482 twice, #13395, #13522) are not in master; whether
  they land later is not tracked here.
- The debug configurations (x64 and 32-bit) were not built on the branch (the 04:59 debug compile was of the five
  picks, whose tree is the branch's).

## 7. Scratch and branches left for the follow-up

- Fork branches: `export-e4-dll-r2` (current), `export-e4-dll` (first message, same tree). Local only in the clone
  `C:/c5-64bit-work/4705/fork`: `e4-parts` (the five picks), `e4-d18` (diagnostic arm), the driver's
  `i0-release-master-3a511df0-c5bdac6e3` / `x64-release-master-3a511df0-c5bdac6e3`.
- `C:/c5-64bit-work/4705/` (clone, worktree, driver work folder with master's test data and the x64 layout, 32-bit
  DLLs) and `F:/c5-64bit-work/4705/` (save test runs, census) are kept until the merge (COMMON 16, D111), for E4b.
  (2026-11-18 08:55: `C:/c5-64bit-work/4705/` is gone after 4705's merge; E4b works from its own clone
  `C:/c5-64bit-work/4719/fork`, which fetched `export-e4-dll-r2`, `x64-batch4` and `i0-ship-batch4` from the fork.)

## 8. E4b: the interface version between the DLL and the server (ticket 4719) -- design note

Status: **accepted by the MC as written (answer to question #5517 on 4719, 2026-11-18 09:08)**: (d) as recommended,
`server/src/srv_main.cpp` added (direct mode), the names and branches as below. Built: section 9.
Plan 4.5: "one constant, raised whenever the DLL side of the contract with the server changes; the 64bit build refuses
to compile against a DLL source with another number, and the server refuses to load a DLL with another number".

### 8.1 What the server relies on in the DLL (the contract the number versions) [C, read 2026-11-18]

- The export `DllGetGameContext` and every interface the server's generated code calls through: the `ICv*` vtables that
  `gen_ifaces.py` reads from the DLL's headers at the server's build (`build_server.py`, `host_ifaces.gen.h`).
- The import slots the server rewrites in the DLL's image after the load: the game database's `Database::Connection`
  calls (`SrvDbInstall`, `srv_remote_db.cpp`) and the Lua calls (`SrvLuaInstall`, `srv_remote_lua.cpp`).
- The shape of a failed check that the dll-fatal handler decodes (`srv_dll_fatal.cpp`, ticket 0422: the register moves
  before the call that precedes the `ud2`).
- What the DLL does under `VP_OUT_OF_PROCESS` (the `Exe::*` answers, the crash handler) and the save widths (draft D).

The number is raised when a DLL-side change to any of these would make a server built for the old number misbehave.
A change on the server side alone (a new hook of an existing import) does not raise it. It does not replace the pin:
`deps/dll.lock` keeps naming the exact commit here (plan 4.5); the number is what upstream, which has no pin, checks.

### 8.2 Where the constant lives in the DLL source

- A new header **`CvGameCoreDLL_Expansion2/CvOutOfProcessInterface.h`**: `#define VP_OUT_OF_PROCESS_INTERFACE_VERSION 1`
  and a comment saying what raises it (8.1, in upstream's words: no project ids, D95). Plain C preprocessor, so VC9 and
  the 64bit folder's build both read it; the folder `CvGameCoreDLL_Expansion2` is already on the server's include path
  (`tools/x64-dll/build.py` `INCLUDE_DIRS`, passed as `/external:I` by `build_server.py`).
- An export **`DllGetOutOfProcessInterfaceVersion`** (`extern "C" __declspec(dllexport) unsigned int ...()` returning the
  constant), appended at the **end** of `CvGameCoreDLL.cpp` inside `#if defined(VP_OUT_OF_PROCESS)`, the header included
  inside the same block. Not in `CvGameCoreDLL.def`: the `.def` is shared with the 32-bit build, which must not gain an
  export (D104). Because the block is last and preprocessed away without the define, no `__LINE__` of the 32-bit build
  moves and the 32-bit preprocessed text of every translation unit stays the same (the proof in 8.6).
- Names follow the fork's existing `VP_OUT_OF_PROCESS` wording (the upstream commit says "out-of-process build", not
  "server"); the MC may rename.

### 8.3 How the number reaches the pinned DLLs and the export branch

- **Pins:** a new fork branch `x64-ifver` on `x64-batch4` (one commit), cherry-picked `-x` into the next x64 pin batch
  as A9/A10 were (ticket 0517's route). It is x64-only content (behind the define), so `i0-ship` does not take it and the
  carry's patch-id rule treats it as a sixth port commit. Until that batch, the pins have no constant (8.5).
- **Export line:** a new fork branch **`export-e4-dll-r3`** on the same base `3a511df0f`: one commit whose tree is r2's
  plus the header and the export, its message r2's plus one paragraph on the interface version. `export-e4-dll-r2` stays
  untouched (no force-push); never the pin, `master` or upstream-tracking branches; nothing sent upstream (D95, D130,
  D27). Plan 4.5 asks for one DLL commit, hence a squash rather than a second commit on r2.

### 8.4 What the server does at load

In `kRt_Boot` (`srv_remote_main.cpp`), right after `LoadLibraryExW` and before the import-slot installs and
`DllGetGameContext`: `GetProcAddress(dll, "DllGetOutOfProcessInterfaceVersion")`, compared with the expected number in
the new `server/src/remote/srv_dll_interface.h` (the decision itself is one inline function there, so the unit test
needs no server).
- **Match:** one `Report` line ("interface version 1: matches"), boot goes on.
- **Mismatch:** `kRt_Boot` answers `ok = 0` with the message "the DLL's interface version is N, this server expects M:
  the DLL and the server come from different builds" (both numbers; `server-report.txt` gets it through `Fail`). The
  proxy already turns a failed `kRt_Boot` into the D118/D119 route: `SetFail("handshake", "dll-boot", <message>)`, the
  error report with the message in its `[failure]` section, and the `vp64helper` popup (`proxy-design.md` 22.1); the
  session rules of 22.1/22.8 apply unchanged (a D33 session ends; never a degraded game). No proxy change is needed.
- **Direct mode** (`srv_main.cpp`, the save test's and run_d10's route, not the product) loads the DLL by its own code;
  the same check there is one call, but `srv_main.cpp` is not in 4719's `touches`. Proposed: add it (part of the
  question).

### 8.5 A DLL without the constant (today's pins)

Proposed rule, decided at the server's compile time so that nobody has to flip a switch later:
- **Server built against a DLL source that has the header** (every pin from the batch that carries it, and always in
  the 64bit folder): a DLL without the export is **refused** like a mismatch ("the DLL has no interface version").
- **Server built against a source without the header** (today: `x64-batch4`): a DLL without the export is **accepted
  with a LOUD line** in `server.log` and the report ("the DLL has no interface version; accepted because this server was
  built against a DLL source without one"); a DLL that has one is still compared with the expected number.
So today's layouts keep working, and the strict rule starts by itself with the pin batch that carries the header.

### 8.6 The compile-time check (the 64bit folder's build)

`srv_dll_interface.h` holds `SRV_DLL_INTERFACE_VERSION_EXPECTED` (this repository's number) and, with clang-cl's
`__has_include("CvOutOfProcessInterface.h")`, includes the DLL's header when it is there and
`static_assert(VP_OUT_OF_PROCESS_INTERFACE_VERSION == SRV_DLL_INTERFACE_VERSION_EXPECTED, "...")`: a server build
against a DLL source with another number fails to compile, with both numbers in the message. Without the header it
compiles and sets `SRV_DLL_SOURCE_VERSIONED 0` (8.5). `srv_remote_main.cpp` includes it, so every server build checks.

**32-bit unharmed (step 2):** (a) the 32-bit preprocessed output (`/P`, the fork's 32-bit flags) of `CvGameCoreDLL.cpp`
before and after is the same apart from line markers, and the header is included by no other unit, so the 32-bit
objects are the same by construction [to be shown]; (b) the save test's games x86 master vs r3, as 5.3, when a game
instance is free (4719 needs none until then). dllcmp reads only x64 images (5.2), and the 32-bit build is not
byte-repeatable even against itself (5.2), so a byte comparison of the 32-bit DLLs proves nothing.

### 8.7 Tests (step 2)

`server/tests/remote/test_dll_interface_version.py`, built as the 0422 tests (clang-cl x64, `/W3 /WX`; ASan once under
MSVC `/fsanitize=address`, COMMON 9): stand-in DLLs exporting 1 and 2 and one without the export; the unit loads each
and runs the header's decision both ways (source versioned and not): match accepted; mismatch refused with both numbers;
missing refused (versioned) or accepted LOUD (unversioned); a compile against a fake DLL header with another number must
fail with the static_assert's text (negative compile test, exit code checked).

## 9. E4b as built (ticket 4719, 2026-11-18)

Evidence: `tools/x64-dll/evidence/4719/` (scripts in `scripts/`). Nothing sent upstream (D95, D130, D27).

### 9.1 The fork branches [C, pushed to the owner's fork 2026-11-18 between 09:12 and 09:19 EST]

| Branch | Commit | What |
|---|---|---|
| `x64-ifver` (new) | **`344bdff8e57b`**, one commit on `x64-batch4` c29442e8c | "Add an interface version for the out-of-process build": new `CvGameCoreDLL_Expansion2/CvOutOfProcessInterface.h` (`VP_OUT_OF_PROCESS_INTERFACE_VERSION 1`, the comment of what raises it) and 11 lines at the end of `CvGameCoreDLL.cpp` (the export, inside `#if defined(VP_OUT_OF_PROCESS)`). For the next x64 pin batch (`cherry-pick -x`, as A9/A10); `i0-ship` takes nothing |
| `export-e4-dll-r3` (new) | **`aa23bb7fed3b`**, one commit on upstream master `3a511df0f` | tree = r2's tree + the same two files (`r2-to-r3.diff`: 2 files, +30), made by `cherry-pick --no-commit` of 344bdff onto r2 and `commit-tree` with r2's parent; message r2's with ", interface version" in the title and one paragraph before the attribution line (`r3-commit-msg.txt`, `scripts/make_r3_msg.py`). Scrub grep of the message for `D[0-9]+`, issue, epic, ticket, tracker, VP64, claude, server, cherry: no hit. `export-e4-dll-r2` untouched |

The project files (`*.vcxproj`, filters) do not list the new header: no build needs it there (the 32-bit build never
includes it, `build_vp_clang.py` compiles by its own list); whether upstream wants it listed is for the PR review [I].

### 9.2 The server [C]

- `server/src/remote/srv_dll_interface.h`: `SRV_DLL_INTERFACE_VERSION_EXPECTED 1u`; `__has_include("CvOutOfProcessInterface.h")`
  -> include + `static_assert` + `SRV_DLL_SOURCE_VERSIONED 1`, else 0; `SrvDllInterface::Decide` (the rule of 8.5) and
  `Check(HMODULE)` (`GetProcAddress` of the export).
- `srv_remote_main.cpp` `kRt_Boot`: after `LoadLibraryExW`, before `SrvDbInstall`/`SrvLuaInstall`/the dll-fatal handler
  and `DllGetGameContext`: `Report("boot: <text>")`; on a refusal the text is kRt_Boot's message (ok = 0), so the proxy's
  `SetFail("handshake", "dll-boot", ...)` puts it into the D119 report and starts the popup; nothing is installed into a
  refused DLL. `srv_main.cpp` (direct mode): the same call after its `LoadLibraryExW`, `Fail(text)` on a refusal.
- **To raise the number:** a DLL change of 8.1 raises `VP_OUT_OF_PROCESS_INTERFACE_VERSION` on its fork branch, and the
  pin change that takes it raises `SRV_DLL_INTERFACE_VERSION_EXPECTED` with the server's matching change; a server
  build against the new source fails on the static_assert until both agree.
- The refused DLL stays loaded (its `DllMain` has run; a `FreeLibrary` would run its `DestroySingleton` for nothing):
  the server ends with the proxy's failed boot as for every other kRt_Boot failure.

### 9.3 Unit tests: 46/46, with the end-to-end part 52/52 [L, 2026-11-18, 46/46 before 09:12 EST, 52/52 before 09:30]

`python -I server/tests/remote/test_dll_interface_version.py --out <dir> --dll-src <x64-ifver checkout> --asan`
(`unit-tests.txt`): stand-ins v1, v2, none; the unit unversioned, versioned (fake header 1), against the fork's real
header, and both again under MSVC `/fsanitize=address` (canary caught; no ASan report). Every arm: match accepted,
mismatch refused with "interface version is 2, this server expects 1", missing refused (versioned) or accepted LOUD
(unversioned), `Check(NULL)` safe, the whole Decide table; negative compile: a fake header that says 2 fails on the
static_assert, clang's diagnostic "static assertion failed due to requirement '2 == 1U'" plus the message.

**End to end, remote mode** (`--e2e <build> --pinned-dll <DLL>`, `unit-tests.txt`): the real `server.exe --remote` of
9.5's versioned build, booted by ticket 0418's stand-in EXE (`exe_crash_standin.cpp` "incall", which sends `kRt_Boot`
as the proxy does and prints its `ok`), with `server.cfg` `dll=`: the build's own DLL -> `kRt_Boot ok 1`, "boot:
interface version: the DLL's is 1, as this server expects", the dll-fatal handler installed; the v2 stand-in -> `ok 0`,
`server-report.txt` "FAILED: kRt_Boot: interface version mismatch: the DLL's interface version is 2, this server expects
1 ..."; the none stand-in and **the pinned x64-batch4 DLL** -> `ok 0`, "interface version missing ..."; on every refusal
nothing is installed into the DLL (no dll-fatal handler line). What the proxy does with `ok 0` (its `dll-boot` failure,
the D119 report and the popup) is the existing path of `proxy-design.md` 22.1, not run here [C].

### 9.5 The server built; direct mode live [L, 2026-11-18]

- `server/build_server.py --src <x64-ifver worktree> --out <dir> --config debug --unlocked`: `build_server: OK in 369 s`
  (DLL, server, proxy); the DLL "Release-5.4.8-64-g344bdff8e5 Clean", debug, sha256 `b3f2a932...eec9`; its export table
  (`llvm-readobj --coff-exports`): `DllGetGameContext`, `DllGetOutOfProcessInterfaceVersion`; the pinned layout's DLL:
  `DllGetGameContext` only. The server compiled against the real header: the static_assert held (1 == 1).
- Direct mode (`srv_main.cpp`, `scripts/direct_runs.sh`, `direct-mode.txt`), Continents Tiny fixed seed, 2 turns, the
  release descriptor's 5.4.8 game folder and test database: the build's own DLL -> "dll: interface version: the DLL's is
  1, as this server expects", result passed, exit 0; the same `bin` with the pinned x64-batch4 DLL -> "FAILED: interface
  version missing ...", phase boot FAILED, exit 2 after 2.0 s.
- The transition (8.5) live: the server built against the **pinned x64-batch4 source** (no header; `build_server: OK in
  238 s`, `--no-proxy`) with its own DLL (no export), the same game: "dll: interface version: LOUD: the DLL has no
  DllGetOutOfProcessInterfaceVersion export; accepted because this server was built against a DLL source without one
  (it expects 1 once the DLL has it)", result passed. So `main`'s builds on today's pins keep working, with one LOUD
  line per boot, until the pin batch that carries `x64-ifver`.
- Not run: the release configuration (LTO) of either build; the proxy's handling of the refusal inside a real game
  (`dll-boot` -> D119 report and popup, existing code); the x86 save test master vs r3 (waits for a free game instance).

### 9.4 32-bit unharmed: preprocessed text identical [L, 2026-11-18 between 09:12 and 09:19 EST]

`scripts/pp32.py`: in ONE fork worktree (same `__FILE__`), at commit A and then B, the 32-bit build's own flags (read
from each commit's `build_vp_clang.py` `build_cl_config_args(Release)`, under `vsvars32.bat` of VS 2008 as that script
runs them, `/c -flto /Zi /FS` dropped, `/EP /P` added) on `CvGameCoreDLL.cpp` (the only `.cpp` of the diff; no other
unit includes the new header, `git grep`):
- `x64-batch4` vs `x64-ifver`: **IDENTICAL**, sha256 `8fa01b4c...c900`, 6,201,925 bytes (`pp32-x64-batch4-vs-x64-ifver.txt`);
- `export-e4-dll-r2` vs `export-e4-dll-r3`: **IDENTICAL**, sha256 `8b9fedb7...2226`, 7,684,858 bytes (`pp32-r2-vs-r3.txt`).
Control: each `.i` holds `DllGetGameContext` once and `DllGetOutOfProcessInterfaceVersion` not at all. The 32-bit
compiler gets the same text for every unit, so the 32-bit objects are those of the base by construction; the `.def`
is unchanged, so the export table is too. The x86 save test (as 5.3) is still to run when a game instance is free
(none was used by 4719, MC answer).

## 10. The rebase on upstream master 5.5 (ticket 0762, 2026-11-25)

Status: paused 14:38-14:49 EST (MC D258/D259: beta 5 goes on VP 5.5; one agent edits DLL files at a time), then
lifted by lead-0763 (answer #15130): this branch is the x64 layer of 0764's `x64-vp55` (= `i0-vp55` + these commits),
kept rebasable, **one commit per part**, no gameplay fixes; the danger package's diagnostics stay behind their off
defines (no unifdef); only comments and messages are scrubbed for D95. Nothing sent upstream (D95, D27). Scripts:
`tools/x64-dll/evidence/0762/scripts/`.

**The PR's single commit: fork `export-e5-dll-one` = `1cea1501339c3980b095fc40c6950a3096d3d206`** (pushed 2026-11-25
17:1x EST, lead-0763 #15467; 4707's package reads it): one commit on b0bce8c2f whose tree `67659eab4` is
`export-e5-dll`'s (checked by `s13_one_commit.sh`), 40 files +2,884 / -81; message `evidence/0762/msg-one.txt`: the
three parts' bodies in one, naming the companion commit of the pull request (the `64bit/` folder) without a hash,
attribution line kept (D258 (3)); scrub grep of the message: no hit.

**0764's x64 layer: fork `export-e5-dll` = `58a8ce863`** (pushed 2026-11-25 15:0x EST), three commits on b0bce8c2f:

| Commit | Part |
|---|---|
| `6f5f1cf55` | port drafts A-D + interface version (tree of `0996ca622` below); r3's message with the out-of-process paragraph naming master's new `Exe` features and how each answers |
| `03ce5ed6e` | the enemy-reach cache of the danger maps (`VP64_SPEED_DANGER`, `_DG7`; diagnostics behind their off defines) |
| `58a8ce863` | hidden embarks (`VP64_HIDDEN_EMBARK`) |

Its tree equals `export-e5-dll-wip` `189f9c949` plus a comment-only scrub (7 files, +25 / -29: no project name,
ticket/epic ids, repository paths, dates, "server"/"pin" wording in the added source; one `fprintf` string of the
verify build's info file shortened) [C: `git diff`, grep of the branch diff 2026-11-25]. Split by `s02_split.sh`
(the embark part = `1fd51353e..a49d8b2e7` reversed out of the final tree; one hand resolution in
`CvGameCoreDLLPCH.h`, where both includes sit together); `git diff e5-parts e5-split` empty.
`export-e5-dll-wip` stays untouched (no force-push).

**x64 debug compile + census link** [L, 2026-11-25 15:35-15:37 EST, workstation, `s04_x64_debug.sh`]:
`tools/x64-dll/build.py --src <export-e5-dll> --unlocked --config debug --census`: marker defines
`VP64_HIDDEN_EMBARK`, `VP64_SPEED_DANGER`, `VP64_SPEED_DANGER_DG7`; **0 errors**; 327 unresolved closed-library
symbols in the census link (the same count as 4705 on 3a511df0f and 3505 on ba8aafdca); compile 29 s, total 85 s;
DLL `86367d39...cf19`, 22,690,816 B. So master 5.5's 52 newer commits add no x64 compile break, and the danger package
compiles on 5.5's finder.

The first form below (section 10.2) is kept as the record of the resolutions.

### 10.4 Proof (1) on 5.5: the 32-bit build, master vs the branch [L, 2026-11-25, workstation]

**Builds** (`s03_x86_builds.sh`, 15:42-16:08 EST, each under `quietlock.py run`, started only while no LTO link
place was taken): the fork's `build_vp_clang.py --config release` (clang-cl 19.1.7, VC9 STL, `/LTCG`) in ONE worktree
folder `C:/c5-64bit-work/0762/wt`, about 8.5 min each (link 376 s): master b0bce8c2f `34EDE13E...6506`, the branch
58a8ce863 `F6F857B8...4B1E`, master again `919A9AE4...3A48`, all 13,451,776 B. `pe32cmp.py` (4705's, `s07_pe32cmp.sh`):
master vs master again DIFFERENT (`.text` 19 bytes in 18 runs, `.rdata` 429 bytes: not repeatable, as 4705 5.2);
branch vs master DIFFERENT, `.text` virtual size **16 bytes shorter** (11,112,533 against 11,112,549), the same 16
bytes as 4705's export commit on 3a511df0f, so the reach cache and hidden embarks add no x86 code (their lines are
preprocessed away; only `__LINE__` values move) [I from the equal delta]. So byte identity is not reachable and the
games decide, as in 4705.

**Test data** (`s06_testdata.sh`, 16:09-16:11 EST): `tools/release/stages/testdata.py --tag Release-5.5 --out
C:/c5-64bit-work/0762/testdata --fork <own clone> --base Release-5.4.8 --no-compare` (2 min 15 s): game folder with
the 5.5 modpack, `db/Civ5DebugDatabase.db` built from 5.5's mods with the tag's own DLL, descriptor `Release-5.5.json`
(localization still the base's 5.4.8 one).

**Save test games, A = master, B = the branch** (`s08_x86ab.sh` -> `scripts/s12_x86ab.py`, 4705's with the owner name
changed; c5h headless, run_d10's x86 command, `--no-mods`, 5.5 test database; runs on F:):

| Check | Tiny 0-100 | Standard 0-200 |
|---|---|---|
| Behaviour (`statscmp`) | **IDENTICAL**, 16 tables, **32,987 rows** | **IDENTICAL**, 16 tables, **350,979 rows** |
| Writer (each arm's early and final saves resaved by A and by B, `savecmp`) | **4/4 IDENTICAL UNDER EXCEPTIONS** | **4/4 IDENTICAL UNDER EXCEPTIONS** |
| The one exception | the version string, `Release-5.5-0-gb0bce8c2f Clean` against `Release-5.5-3-g58a8ce863e Clean` | same |
| Host | every run rc 0 | same |

**Result: PASS** (16:12-16:44 EST, verdict line of `s08.log`; reports copied to `evidence/0762/x86ab/`): the three
commits leave the 32-bit DLL's games and save bytes unchanged on these two games of master 5.5 [L]. Row counts are
5.5's (4705 on 3a511df0f: 35,904 / 371,825). Wall times beside other teams' work (not timings): Tiny 137 s per arm,
Standard 674 s per arm, the resaves up to 10 min each.

### 10.5 The 64-bit build on the product path: one short game, PASS [L, 2026-11-25 16:45-17:01 EST, workstation]

Debug configuration only: MC-B (#15326) gave the LTO link place to 0764's take, so the release build of this branch
is left to 0764's `x64-vp55` take [not run here]. `s09_product_debug.sh`: `build_all.py --config debug` with
`C5_RELEASE_TEST_DB` = the 5.5 test database (every step rc 0 in 166 s; smoke on 5.5's database; DLL `a17590af...a228`,
22,818,816 B; marker defines as above), then `server/build_server.py --config debug --deps` from this repository
(main of 2026-11-25 merged into the ticket branch): server, proxy and civ5save OK in 148 s; `gen_runtime` 61 interfaces,
kAppSchemaHash `0x765d76dcdc4e55b2`; the purity test not run (unlocked source; the release driver's purity stage runs it).

`s11_proxy_game.sh`: `proxy/tools/run_proxy_game.py --mode proxy` (the product's route: the 32-bit proxy DLL in the
headless host, `server.exe --remote` with this DLL), Continents Tiny fixed seed, **30 turns**, strict, local answers and
verify, Lua mirror, `--fail-on-unexpected`, 5.5's test database and game folder, the pinned 5.4.8 localization:
**PASS**, exit 0, wall 534 s, host 3.3 s/turn, host Lua errors 0; `server-report.txt`: "boot: interface version: the
DLL's is 1, as this server expects", result passed, peak working set 170 MB, methods without remote treatment 0;
database write recording: 95 writes replayed, 0 results differed, 5 identity checks 0 failed. No game instance (the
EXE) was used: the live product route on 5.5 waits for 0765's patch side and 0766's 5.5 instances [not run].

### 10.1 Upstream master has moved to 5.5 [C, fetched 2026-11-25 14:35 EST]

Upstream `master` = **`b0bce8c2f23c865cf5b0779986654366c88cef62`** ("5.5 Release", 2026-11-24 02:19 -0500), tag
`Release-5.5`; **52 commits** after 4705's base 3a511df0f. Six of them touch `Exe/` (`git log 3a511df0f..master --
CvGameCoreDLL_Expansion2/Exe`): 0d7a8d906 (EXE addresses for the November 2026 Steam update, #13614), 4d4373e8f
(FireTuner in multiplayer, #13387), 3a0f92f7d (grow the engine's UI draw buffers from Lua), e2cf4fd70 (UI command
stream addresses for the update), d1022016b (map graphics laid out in budgeted batches against the event stream's
size), 7b6123203 (event stream addresses for the update). So `Exe::` now has nine more functions [C, `Exe/CvExe.h` at
b0bce8c2f]: `CanGrowUICommandStream`/`TryGrowUICommandStream`, `CanGetUICommandStreamUsage`/`TryGetUICommandStreamUsage`,
`TryReadLocalMachineEventStreamStats` (does not log a refusal: called every frame), `CanEnableTunerInMultiplayer`/
`TryEnableTunerInMultiplayer`, `OnGameLoaded`, `AnnounceTunerAfterLoad`.

### 10.2 What the branch holds [C: git, 2026-11-25 14:35-14:41 EST]

Own clone `C:/c5-64bit-work/0762/fork` (`git clone --shared --no-checkout` of the local fork clone; `origin` the fork,
`upstream` LoneGazebo), worktree `C:/c5-64bit-work/0762/src` on local branch `e5-parts`, pushed to the fork as
**`export-e5-dll-wip` = `189f9c949`** (two commits on b0bce8c2f; never master, `x64`, `i0-ship` or a batch branch):

| Commit | What | Conflicts and resolutions |
|---|---|---|
| `0996ca622` | cherry-pick of r3 `aa23bb7fe` (port drafts A-D + the interface version), r3's message unchanged | `Exe/CvExeApi.cpp`: master's new `ExeApi::` accessors (tuner, UI manager, command stream, Fire allocators, event stream) kept inside the `#if !defined(VP_OUT_OF_PROCESS)` block, then r3's `#endif`. `Exe/CvExe.cpp`: master's tuner helpers kept inside the guarded anonymous-namespace block; master's new in-process features kept in the `#else` part; **nine out-of-process answers added** to the `#if defined(VP_OUT_OF_PROCESS)` part, each as master's in-process code answers for an unknown EXE (symbols unresolved): `Can*` = `bin_hooks_off` without BIN_HOOKS else `unsupported_exe`, except `CanGetUICommandStreamUsage` = `unsupported_exe` always (master's in-process version has no BIN_HOOKS test); `Try*` log the refusal through `Refuse` except `TryReadLocalMachineEventStreamStats` (no log, as master's); `OnGameLoaded` and `AnnounceTunerAfterLoad` do nothing (the tuner is never enabled out of process). No `Exe/` header is included outside `Exe/` on master (grep). Script `resolve_ours_then_theirs.py` |
| `189f9c949` | the pin 1b87dd185's x64-only additions beyond the port: `git diff x64-batch5 1b87dd185` (21 files, +2,593 / -0: the danger package `VP64_SPEED_DANGER`/`VP64_SPEED_DANGER_DG7`, its verify header, hidden embarks `VP64_HIDDEN_EMBARK`, no `#line`), applied with `git apply --3way` | `CvTeam.cpp` (2 hunks, `makeHasMet` and `SetAllowsOpenBordersToTeam`): master's new diplomacy logging and the package's two `TeamScope`s both kept. `CvUnit::setEmbarked`: master's changed-value test with `UpdateAreaEffect(false)` kept, the package's `VP64Danger::UnitEvent(this)` after it (as before, unconditional). The other 19 files applied cleanly |

Why these two pieces [C]: `git diff i0-ship-batch5 x64-batch5` is exactly r3's content (15 files, +171 / -67: the
port's 13 files + the interface version's 2); `x64-batch5..1b87dd185` adds only the danger package, hidden embarks and
the `#line` removal; i0-0.1b `f33fc46a4` = i0-ship-batch5 + hidden embarks. The 32-bit fixes of the pins (fix batches
1-5, D18, A9, A10, ...) are not port and are not in this commit (as 4705, section 4).

### 10.3 Open

- (Done 15:0x: the D95 comment scrub, r3's message naming the new `Exe` features, one commit per part; see the top of
  section 10.) Plan 4.5's single DLL commit is now the export's squash of the three parts (lead-0763 wants them
  separate while 0764 builds on them).
- The danger package was written against 5.4.8 + A10's 32-bit `CvAStar` generation id (`671cddf44`): it compiles on
  5.5 (debug, above), but whether 5.5's finder keeps the generation rule the package checks is not verified by a game.
- (Corrected 17:0x: the 32-bit save test and a product-path game have run, 10.4 and 10.5.) Still not run: the x64
  release (LTO) build with the narrowing gate (0764's take holds the link place); VC9, upstream's own toolchain; a
  live game in the EXE on 5.5; games other than the save test's Tiny and Standard and one 30-turn Tiny proxy game.
- The out-of-process answers of the nine new `Exe::` functions compiled and the proxy game ran with them, but no run
  called them from Lua or checked a refusal line [I from the code].
- Upstream's new `Exe` features refuse out of process. On the server route the 5.5 UI Lua that calls
  `Exe.TryGrowUICommandStream` would reach the server's DLL and be refused [I: not traced through the proxy]; the
  product's own UI draw-list growth (D235) is in the proxy and unaffected.
