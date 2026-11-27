# Drafts of the upstream texts for the x64 port (issue 3505, 2026-11-13)

Drafts for the owner. **Nothing here has been sent** (D95): no pull request, issue or comment upstream. The owner decides
whether and when; the MC relays. The texts inside the quoted blocks are written for the upstream repository
(LoneGazebo/Community-Patch-DLL): no internal ids, no project names, no cherry-pick lines. The notes outside the
blocks are for the owner and the MC.

**Update 2026-11-18 (ticket 4705, E4a):** drafts A-D are now one commit on upstream master 3a511df0f, fork branch
`export-e4-dll-r2` (`upstream-export-e4-dll-commit.md`), with its own message; B lost its "address space low" bullet
(master does it on every build) and C covers master's third EXE feature (city art repair). **Draft D's claim needs
D18:** on master, without the sorted writes, x64 saves are not byte-identical to x86 saves; behaviour is identical and
only the hash order of the three D18 containers differs (that file, 5.4); with D18 added the full save test passes (5.6).
So D's "Testing" paragraph is true for master only together with the 0081 draft's sorted writes. E belongs to package
E5 (MC #5165).

Source of each draft: the upstream-shape port commits on upstream master ba8aafdca (`x64-dll-port.md` section 12;
fork branch `x64-release-master-ba8aafdc-r2`, commits 00bef5695 A, 12a80396e B, ceaf0c413 C, 15bb0a0b2 and 891ec17e0 D),
in the W4 clone `D:/c5-64bit-work/3500/w4/fork`. A PR branch would be cut from `upstream/master` with the one
commit (the fork's `master` is not an upstream mirror since 0081). The commit messages already say the same as the
bodies below in shorter form.

Section F (added 2026-11-24, ticket 0356) holds two small drafts unrelated to the x64 port: the 43-civ define name
in the release and test instructions, and the victory screen's city-state loop.

Order proposed: A first (everything else needs a compiling x64 build), then D (saves), B, C, E. D18 (sorted
writes) and the D25 determinism commits have their own draft from 0081 (`research/sources/0081-upstream/
0081-pr-deterministic-order-body.md`), not repeated here.

What these drafts claim and how it is backed (for the owner, not for the PR text):
- "x86 build unchanged": [C] by reading every hunk (each is behind `_M_X64`/`_WIN64`/`_MSC_VER >= 1900`/
  `VP_OUT_OF_PROCESS` or is a type that is the same width on x86); [L] on 5.4.8 by the 32-bit A/B of item 4
  (`release-driver.md`, W4 section: the 32-bit DLL built from the x64 pin's source plays the save test's games
  identically to the pinned 32-bit build). Not re-measured on master (no master game data in the test setup).
- "x64 compiles": [L] x64 debug build of master + these commits, 0 errors, link 0 unresolved against the
  closed-library stubs; release build: `x64-dll-port.md` 12.3.
- "x86 compiles on master": [L] the fork's `build_vp_clang.py --config debug` (clang-cl with the VC9 STL) builds
  and links master + these commits (`x64-dll-port.md` 12.2).
- Not built with VC9 (MSVC 2008) here; the upstream CI would be the first VC9 build of these commits [I: every
  construct used is C++03 and VC9 already compiles the x86 branch of each `#if`].

## A. Compile with clang-cl -m64 and a modern STL

> **Title:** Compile with clang-cl -m64 and a modern STL
>
> The DLL only builds for x86 against VC9's STL. Compiled with clang-cl `-m64` against the MSVC 14 STL it stops on
> compile errors in five files, and a few constructs are x86-only. Each change below compiles with both toolchains and keeps
> the x86 build's types and code:
>
> - `CvUnit.h`: the modern `std::swap` is `noexcept`, so the explicit specialisation for `SUnitIDValueContainer` no
>   longer matches. With `_MSC_VER >= 1900` an overload found by argument-dependent lookup is used instead, which is
>   what the modern STL looks up anyway. VC9 keeps the specialisation.
> - `CvHomelandAI.cpp`: `make_pair<CvUnit*, BuilderDirective>(NULL, eDirective)` does not compile against a
>   `make_pair` that takes forwarding references. `pair<CvUnit*, BuilderDirective>(NULL, eDirective)` builds the same
>   value (7 sites).
> - `CvTacticalAI.cpp`, `CvEspionageClasses.cpp`: `min`/`max` of an `unsigned int` and a `size_t` cannot deduce one
>   type on x64. `min<size_t>`/`max<size_t>`; on x86 both operands are 32-bit already.
> - `FBatchAllocate.h`: the alignment arithmetic casts pointers to `unsigned int`, which truncates on x64. `size_t`
>   is the same type on x86.
> - `TContainer.h`: the lookup cache's game-core-thread test reads the thread id with `__readfsdword(0x24)`, which
>   exists only on x86. On x64 the same TEB field is at `gs:[0x48]` (`__readgsdword(0x48)`); x86 is unchanged.
> - `!CvLuaApi.h`: `luaEx_pushtag` casts its `DWORD` tag through `UINT_PTR` before `void*` (clang warns about the
>   direct cast on x64; same value on both).
>
> `CvTeam.cpp`'s int-to-unsigned aggregate initialisations still need `-Wno-c++11-narrowing`, which the x86 clang
> build already passes.
>
> **Testing:** x64: every translation unit compiles (clang-cl 19.1.7 `-m64`, MSVC 14.43 STL, debug), 0 errors. x86:
> the same games played with a 32-bit build with and without these changes give identical saves and statistics
> (tested on 5.4.8, where the same changes apply). Not built with VC9 here.

## B. Crash handler on x64

> **Title:** Crash handler: scan the stack and report addresses on x64
>
> The crash handler reads `ESP` and 32-bit stack slots, casts pointers to `DWORD` and prints addresses and sizes
> with `%08x` / `%u`. A 64-bit build would not compile that, or would write truncated reports. Every change is keyed
> on the compiler's own macros (`_M_IX86`, `_M_X64`, `_WIN64`); the x86 build compiles the same code with the same
> format strings as before.
>
> - `AppendStackScan`: pointer-sized stack slots (`ULONG_PTR`) from `RSP` on x64. The preceded-by-CALL test already
>   covers the x64 encodings (`E8 rel32`, `FF /2` with or without a REX prefix, `FF 15` RIP-relative). On any other
>   architecture the scan is compiled out.
> - `CustomFilter`: addresses kept in `UINT_PTR` and printed at full width on x64 (`CRASH_ADDR_FMT`,
>   `CRASH_SIZE_FMT`); the module RVA stays 32-bit, since a PE image is below 4 GB.
> - On x64 the "address space low" classification looks at the largest free block overall: the low 2 GB can be
>   fragmented while the process has terabytes free.
> - The minidump comment names the architecture it was built for.
>
> The scan stays a heuristic, as on x86: no unwinder, no allocation.
>
> **Testing:** compiles for x64 and x86; on x86 the functions are unchanged apart from moved `__LINE__` values.
> Not exercised by a real x64 crash.

Note for the owner: the x64 path has never caught a real crash [I]; an induced crash in the server (null write) would
check it before the PR is sent.

## C. Out-of-process build

> **Title:** Add VP_OUT_OF_PROCESS: a build that runs the DLL outside the game's EXE
>
> This adds a build define for running the DLL in a process of its own, with no Civ V EXE in it, for example a
> 64-bit host that serves the 32-bit game. A build without the define is unchanged. With it:
>
> - The EXE interop (`Exe/`) is left out: `CvExeApi.cpp`, `CvExeBuild.cpp` and `CvExeSymbols.cpp` compile to
>   nothing, and `Exe::*` answers exactly as it does for an unknown EXE: `GetBuildName()` returns "Unknown",
>   `Can*`/`Try*` return `bin_hooks_off` when BIN_HOOKS is off and `unsupported_exe` otherwise, and `Try*` logs the
>   refusal. The Lua `Exe` table is registered as before, so scripts see the same API. Nothing reads or writes
>   another module's memory, and the pointer-to-`DWORD` casts in `Exe/`, which do not compile for x64, are not
>   built.
> - Error dialogs (asserts, preconditions, the crash dialog) are always off, reported as "out-of-process build":
>   nobody sees the host's windows and a modal dialog would block it. `crashes.log`, `CvAssert.log` and minidumps
>   are written as usual.
>
> **Testing:** an x64 build with the define compiles and links; with BIN_HOOKS on, `Exe.CanScheduleResync()`
> returns `false, "unsupported_exe"` [to be checked in a run before sending]. x86 builds without the define are
> unchanged.

Note for the owner: this PR names a use upstream has no reason to support. It is the one most likely to be declined; A, B
and D stand without it (x64 then needs the `Exe/` casts fixed instead, four `UINT_PTR` casts, and those features
cannot work out of process anyway).

## D. Saves on x64

> **Title:** Keep save counts and saved hashes 32-bit on x64
>
> A 64-bit build writes `size_t` as 8 bytes, so every container count and every `size_t` member that reaches
> `FDataStream` would make its saves differ from the x86 build's and unreadable by it. A compile census of all
> translation units (`FDataStream`'s 64-bit overloads) lists every such place; this pins each one to 32 bits:
>
> - `FStlContainerSerialization.h`: container counts go through `FStlSerializedCount()`, which on `_WIN64` narrows
>   with a range assert and on x86 is the expression itself. Both readers hold the count in `unsigned int`, so the
>   `0xFFFFFFFF` sentinel test works on x64 too.
> - `CvInfosSerializationHelper.h`: `(uint)` on the four vector `size()` writes.
> - `CvGame`: the saved name hashes (advisor messages seen, destroyed cities, great people born) and `stringHash`
>   are `uint`.
> - `CvAStar.cpp`: the `CvPathNodeArray` count is read and written as `uint`.
> - `CvEspionageClasses.cpp`: 14 writes of `size()` cast to `uint`; their readers already read 32 bits.
> - `CvDatabaseUtility::Initialize2DArray`: the allocation size stays `size_t` (it was narrowed to `unsigned int`).
>
> On x86 every changed type is already 32-bit and the same `FDataStream` overloads run, so x86 saves do not change.
>
> **Testing:** on 5.4.8 with these changes, a 32-bit and a 64-bit build write byte-identical saves (apart from the
> version string) for a Tiny game to turn 100, a Standard game to turn 200 and a Huge save from turn 350 to 400, and
> each loads the other's saves. The census finds no remaining 64-bit-only write on master.

Note for the owner: "no remaining 64-bit-only write on master" is [L]: `save-width-census.py --arch both` on the master
port ce47ef342, 2026-11-13 22:45-22:52 EST: 178 TUs per architecture, 0 failed, 9 records on each build, **0
x64-only** (`x64-dll-port.md` 12.2). The D (Testing) save claims are from the 5.4.8 save test (`deps/dll.lock`
verified line), not from master.

## E. An x64 compile check in CI

> **Title:** CI: compile the DLL for x64 (no link)
>
> The DLL ships for x86 only, but a 64-bit build is useful (a 64-bit host, sanitizers, newer compilers), and
> 64-bit breaks are easy to add without noticing: a pointer cast to `DWORD`, an x86 intrinsic, a `make_pair` with
> explicit template arguments. This adds a job that compiles every translation unit with clang-cl `-m64` against
> the MSVC 14 STL of the runner (`/DVP_OUT_OF_PROCESS`, `-Wno-c++11-narrowing`), without linking (the closed
> libraries exist only for x86). It fails on any compile error and lists `-Wshorten-64-to-32` sites in the log
> without failing on them.
>
> ```yaml
>   compile-x64:
>     needs: [detect-changes]
>     if: needs.detect-changes.outputs.dll-source == 'true' || needs.detect-changes.outputs.build-scripts == 'true'
>     runs-on: windows-2022
>     timeout-minutes: 60
>     steps:
>       - uses: actions/checkout@v7
>       - name: Install LLVM 20.1.8
>         run: choco install llvm --version=20.1.8 --allow-downgrade -y --no-progress
>       - name: Compile every translation unit for x64
>         run: python build_vp_clang_x64check.py
> ```
>
> `build_vp_clang_x64check.py` takes the source list and include paths from `build_vp_clang.py`, runs `vcvarsall
> x64`, builds the precompiled header and compiles each `.cpp` with `-m64 /c` and the same defines as the x86 build
> plus `VP_OUT_OF_PROCESS`.

Note for the owner: the script is not written yet; the project's own `tools/x64-dll/build.py` does this and more, but
cannot be offered as is (it reads the project's lock files and links against stubs). A ~120-line upstream script
is the work if the owner wants E. The job's cost on upstream's runners [I]: about 3 minutes (178 TUs, 27 s wall on 32
cores here; GitHub's windows-2022 runners have 4 cores).

## F. Two small fixes from the player-limit study (43-civ define name, victory-screen city-state loop)

Two independent one-line pull requests, unrelated to the x64 port: either can be sent on its own, in any order,
whenever the drafts above are sent or earlier. Both come from the player-limit study (`player-limits.md` 1.1 and 3.2,
follow-ups F1 and F2). **Nothing sent.** They wait for the owner's yes to the exact text, and upstream drafts are sent only
after the beta.

Re-checked against upstream master of the day [C]: `git grep` at LoneGazebo/Community-Patch-DLL `master`
b0bce8c2f23c865cf5b0779986654366c88cef62 ("5.5 Release", committed 2026-11-24 02:19 -0500), fetched 2026-11-24 16:24
EST into a scratch `git clone --shared --no-checkout` of the local fork clone (`git fetch
https://github.com/LoneGazebo/Community-Patch-DLL.git master`; scratch removed afterwards). Both faults are still
there, on the same line numbers. The diffs below are `git diff --no-index -U1` of each file's blob at that commit
(`git show <commit>:<path>`) against a copy with the one line edited by `sed`; the `a/`/`b/` paths in the headers
were then set to the repository paths and the `index` lines dropped. Upstream keeps these files with LF endings in
the blob; `VictoryProgress.lua` indents with tabs, kept in the diff.

### F1. The 43-civ define name in the two instruction files

> **Title:** Release and test instructions: name the 43-civ define as it is in CustomModsGlobal.h
>
> `NEW RELEASE INSTRUCTIONS.txt` (step 7 A) and `NEW TEST VERSION INSTRUCTIONS.txt` say to uncomment
> `#define MAX_MAJOR_CIVS (43)` in `CvGameCoreDLLUtil/include/CustomModsGlobal.h`. That line does not exist: the
> file has `// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)` (line 11), and `CvDLLUtilDefines.h` derives `MAX_MAJOR_CIVS`
> from it. Someone who follows the text literally and adds `#define MAX_MAJOR_CIVS (43)` gets a redefinition warning
> and a 22-civ DLL, because `CvDLLUtilDefines.h` defines `MAX_MAJOR_CIVS` as `(22)` again after the include.
> `DEVELOPMENT.md` and `scripts/release.py` already use the right name; this brings the two text files in line.
>
> ```diff
> diff --git a/NEW RELEASE INSTRUCTIONS.txt b/NEW RELEASE INSTRUCTIONS.txt
> --- a/NEW RELEASE INSTRUCTIONS.txt
> +++ b/NEW RELEASE INSTRUCTIONS.txt
> @@ -121,3 +121,3 @@ F. Delete the clang-output and clang-build folders in the repository.
>  [STEP 7]
> -A. Build the 43 Civ version of the game core using the Release config: Uncomment "#define MAX_MAJOR_CIVS (43)" in CvGameCoreDLLUtil/include/CustomModsGlobal.h, amend the release commit to include the changed file, then build normally.
> +A. Build the 43 Civ version of the game core using the Release config: Uncomment "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)" in CvGameCoreDLLUtil/include/CustomModsGlobal.h, amend the release commit to include the changed file, then build normally.
>  
> diff --git a/NEW TEST VERSION INSTRUCTIONS.txt b/NEW TEST VERSION INSTRUCTIONS.txt
> --- a/NEW TEST VERSION INSTRUCTIONS.txt
> +++ b/NEW TEST VERSION INSTRUCTIONS.txt
> @@ -31,3 +31,3 @@ If changes were made to the DLL since the latest release commit, you'll need to
>  
> -If you're testing the 43 Civs version, uncomment "#define MAX_MAJOR_CIVS (43)" in CvGameCoreDLLUtil/include/CustomModsGlobal.h before building the DLL.
> +If you're testing the 43 Civs version, uncomment "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)" in CvGameCoreDLLUtil/include/CustomModsGlobal.h before building the DLL.
> ```
>
> Text only; no code changes.

Notes for the owner (not for the PR text):
- Checked at b0bce8c2f [C, `git grep -n MAX_MAJOR_CIVS`]: `NEW RELEASE INSTRUCTIONS.txt:122`,
  `NEW TEST VERSION INSTRUCTIONS.txt:32` (wrong name); `CvGameCoreDLLUtil/include/CustomModsGlobal.h:11` (the real
  line, commented); `CvDLLUtilDefines.h:27-31` (the `#if defined(MOD_GLOBAL_MAX_MAJOR_CIVS)` / `#else (22)` pair);
  `DEVELOPMENT.md:17` and `scripts/release.py:408-409, 425-426` (right name). The path the two texts name,
  `CvGameCoreDLLUtil/include/CustomModsGlobal.h`, is right.
- "Redefinition warning and a 22-civ DLL" is [I] (preprocessor order, as in `player-limits.md` 1.1), not built.
- Left out on purpose: step 7 A's "amend the release commit to include the changed file" is how three release tags
  came to carry the 43 line uncommented (`player-limits.md` 1.1), and `scripts/release.py` now uncomments and
  re-comments the line itself. Whether the manual step should still exist is upstream's call; the draft only fixes the
  name. A sentence could be added if the owner wants it raised.

### F2. The victory screen's city-state loop

> **Title:** VictoryProgress: loop over every city-state slot when counting allied capitals
>
> `PopulateDomination()` in `(2) Vox Populi/Core Files/Overrides/VictoryProgress.lua` counts original capitals held
> by city states allied to a player with
> `for iCityStateLoop = GameDefines.MAX_MAJOR_CIVS, GameDefines.MAX_MINOR_CIVS-1, 1 do`. `MAX_MINOR_CIVS` is the
> number of city-state slots, not the last slot id: city states occupy `MAX_MAJOR_CIVS` to `MAX_CIV_PLAYERS - 1`.
> With 22 majors the loop covers slots 22-40 and misses 41-62; with the 43-civ DLL (`MAX_MINOR_CIVS` = 20) it runs
> from 43 to 19 and does nothing. Ending at `MAX_CIV_PLAYERS-1`, as the other player loops in this file do, covers
> every city state in both builds. Only the capital count shown on the domination panel changes; victory itself is
> decided in the DLL.
>
> ```diff
> diff --git a/(2) Vox Populi/Core Files/Overrides/VictoryProgress.lua b/(2) Vox Populi/Core Files/Overrides/VictoryProgress.lua
> --- a/(2) Vox Populi/Core Files/Overrides/VictoryProgress.lua
> +++ b/(2) Vox Populi/Core Files/Overrides/VictoryProgress.lua
> @@ -338,3 +338,3 @@ function PopulateDomination()
>  					end
> -					for iCityStateLoop = GameDefines.MAX_MAJOR_CIVS, GameDefines.MAX_MINOR_CIVS-1, 1 do
> +					for iCityStateLoop = GameDefines.MAX_MAJOR_CIVS, GameDefines.MAX_CIV_PLAYERS-1, 1 do
>  						local pCityState = Players[iCityStateLoop];
> ```
>
> **Testing:** not run in a game. The loop body already checks `IsAlive()` for every slot, and the file's other
> loops (`PopulateDomination`'s player loop at line 372 and three more) index `Players` up to
> `MAX_CIV_PLAYERS-1`, so the new bound reads no slot those loops do not.

Notes for the owner (not for the PR text):
- Checked at b0bce8c2f [C, `git grep -n "MAX_MINOR_CIVS\|MAX_MAJOR_CIVS\|MAX_CIV_PLAYERS"` over
  `*VictoryProgress.lua`]: line 339 is the only `MAX_MINOR_CIVS` use in the file; lines 372, 684, 819 and 868 loop to
  `MAX_CIV_PLAYERS-1`. A `git grep` of the whole tree for the same `MAX_MAJOR_CIVS, ... MAX_MINOR_CIVS` loop finds
  only this line.
- Values: 22 build `MAX_MINOR_CIVS` 41, `MAX_CIV_PLAYERS` 63 [C, `player-limits.md` 1.1]; 43 build `MAX_MINOR_CIVS`
  = 63 - 43 = 20 [C, `CvDLLUtilDefines.h:33`].
- Effect "only the displayed count" is [I] from the variable's use (`iNumDisplayCapital`), not checked on screen;
  the "Testing" paragraph says so. Line 372 lies in `PopulateDomination` (lines 265-472) [C, function list of the
  blob]. If the owner wants the claim tighter before sending, a one-game check on a
  map with more than 19 city states would do.
