# Build and install the VP DLL

## 1. Build

Always clang-cl, never MSVC (vp-dll-dev SKILL). Two entry points:

| Command | What |
|---|---|
| `python .claude/skills/vp-dll-dev/scripts/vp_build.py --config debug\|release` | build + install; handles the PATH quirk; refuses to install while the game holds the DLL open (`--force` kills it first) |
| `python build_vp_clang.py --config debug\|release` | build only, from the repo root; needs the PATH prefix below |

**PATH quirk (clang-build-env-quirk.md):** `build_vp_clang.py` dies at once with `FileNotFoundError: [WinError 2]`
from `subprocess.run('update_commit_id.bat')`, because this environment sets `NoDefaultCurrentDirectoryInExePath=1`
and `CreateProcess` will not search the working directory. Prepend LLVM and the repo root:

```powershell
$env:PATH = "C:\Program Files\LLVM\bin;$env:USERPROFILE\Documents\GitHub\Community-Patch-DLL;$env:PATH"
python build_vp_clang.py --config release
```

If the build fails at `update_commit_id.bat` anyway, check LLVM is at `C:\Program Files\LLVM\bin` or set
`VP_LLVM_BIN`.

| Config | Defines | Build time | Use for |
|---|---|---|---|
| debug | `VPDEBUG`, asserts on, no LTO | ~50 s - 1 min | debugger, asserts, logic bugs; turns much slower, memory numbers not representative |
| release | `FINAL_RELEASE` + `STRONG_ASSUMPTIONS` + LTO | ~50 s compile + ~235 s LTO link (~5 min) | performance, memory, long autoplay runs, every live shell-gates run |

If the request does not make the choice obvious, ask - a wrong pick costs a build cycle plus a game run.
Output: `clang-output/Debug|Release/CvGameCore_Expansion2.dll` (+ PDB), log `build.log` beside it.
`EXTERNAL_PAUSING` is compiled into every build (the `TurnByTurn` mutex freeze, run-and-drive-the-game.md).

New `.cpp` files must be added to `build_vp_clang.py`'s `CPP` list **and** all five `.vcxproj` files in
`CvGameCoreDLL_Expansion2/`. Link order matters for instruments that replace global operators: lld-link with
`/FORCE:MULTIPLE` keeps the first definition, so `MemoryHooks.obj` is in a `LINK_FIRST` list
(civ5-alloc-hook-instrument.md; see instrumentation-in-the-dll.md).

Other toolchains verified on the machine (TOOLING.md, GATES.md): VS 2022 Community (`vcvarsall.bat`), Windows SDK
10.0.22621.0 and 10.0.26100.0, clang-cl 19.1.7 (`-m32` and `-m64`), Python 3.11 (anaconda). x86 and x64 D3D11 test
programs (`tooling/d3d11_hello`) built clean with /W4. The alloc-trace self-test uses "the same VS2008 + clang-cl
toolchain" as the DLL.

## 2. Install: two paradigms, check which is live

Vox Populi can be installed as a DLC **modpack** or as **mods** in the MODS folder; only one is active at a time,
and `vp_common.py` detects it (`vp.PARADIGM`, override `VP_PARADIGM`; the modpack wins when both are present).
**Check `vp.PARADIGM` (`vp_modgame.py status`) before believing any path.**

| | modpack | mods |
|---|---|---|
| Where | `<install>\Assets\DLC\VP_MODPACK` | `<USER_DIR>\MODS\(1) Community Patch` etc. |
| Engine sees | DLC; game "not modded" | mods, activated through the Mods menu |
| DLL goes to | `...\VP_MODPACK\Mods\(1) Community Patch\CvGameCore_Expansion2.dll` | `%USERPROFILE%\Documents\My Games\Sid Meier's Civilization 5\MODS\(1) Community Patch\CvGameCore_Expansion2.dll` |
| Saves | `Saves\single` | `ModdedSaves\single` |
| Launcher | `vp_game.py` | `vp_modgame.py` |

The shell-gates live runs (2026-11-09) were in the **mods** paradigm: the experiment DLL was installed over the owner's
DLL at `MODS\(1) Community Patch\CvGameCore_Expansion2.dll` and launched with
`vp_modgame.py launch --mods cp,vp,eui` (GATES.md). The 09-06 memory note's "installs into
`Assets/DLC/VP_MODPACK/...`" describes the modpack paradigm of that time; the modpack is normally not in the install
now (a 10-01 junction to `Documents\vp_modpack\VP_MODPACK` was removed by the owner later; check `status` - "paradigm:
modpack" means a link is back).

Never mix save trees: a `ModdedSaves` save carries a required-mod list whose reconciliation crashes under the
modpack; a `Saves\single` save has no mod list for a Mods-folder game.

## 3. The Release-5.4.8 matching rule

An experiment DLL must match the installed mods and the save. The owner's installed mods and the test saves are
**5.4.8**, so the shell-gates branch is `f181104a2` (memory-diagnostics: Lua channel, MemoryImports,
EngineQueueGuard) **merged with `Release-5.4.8`** (commit eeb9305e1, "Merge Release-5.4.8 so experiment DLLs match
the installed mods") - "a master-based DLL would not match" (civ5-shell-gates.md). The vp-dll-dev SKILL says the
same in general form: if the game crashes on startup or on load, or a run reports `startup_timeout`, first confirm
the mods/modpack are savegame-compatible with the DLL; a mismatch is the single most likely cause and retrying does
not help. For headless runs the same rule bites at the database level: post-5.4.8 upstream master traps on the
pinned 5.4.8 DB (`no such table: UnitPromotions_TerrainHeal`) - headless-and-parallel-testing.md.

## 4. Backups and restore by hash

Pattern used on 2026-11-09 (GATES.md status line, `tools/disable_systems.py`, boundary-benchmark REPORT.md):

- Back up into `%USERPROFILE%\Documents\shell-gates-backup` before the first change: `config.ini`,
  `GraphicsSettingsDX11.ini`, the installed `CvGameCore_Expansion2.dll`. `disable_systems.py backup` writes
  `config.ini.orig` + `SHA256.txt` and refuses to overwrite a backup whose hash differs from the live file (unless
  they are already equal).
- Restore and **verify the SHA-256**: `disable_systems.py restore` for config.ini; recorded reference hashes after
  restore: config.ini `7b53b17c3935702d...`, DLL `262AEE88...` (`262aee88c5312c4b...` in boundary-benchmark
  REPORT.md).
- Front-end Lua: `vp_modgame.py restore-lua` (or `vp_game.py restore-lua`) puts the stock `*.vpdev-orig` copies
  back.
- Marker files: remove every marker from `<install>\crashlogs`; move instrument logs out (GATES moved them to
  `live/`).
- `vp_build.py` copies the DLL **and its PDB**; what to do with the PDB on restore is not covered by the sources.

Sources: `research/sources/memory-notes/clang-build-env-quirk.md`, `civ5-vp-dll-dev-skill.md`,
`civ5-shell-gates.md`, `civ5-alloc-hook-instrument.md`, `civ5-exe-message-queue-overflow.md` (junction),
`civ5-memory-investigation-log.md` (line ~443: "The owner removed the junction", 10-06),
`research/sources/skills/vp-dll-dev-SKILL.md` (Building, Two paradigms, Prerequisites),
`research/sources/shell-gates/GATES.md`, `BRANCH-COMMITS.txt`, `TOOLING.md`, `tools/disable_systems.py`,
`alloc-trace-README.md` section 5, `research/sources/boundary-benchmark/REPORT.md` line 218.

## 5. The VC9 toolchain's limits (2026-11-10, epic 1200, appended by MC grant)

Measured by epic 1200 while building `ipc/` for the proxy side, on this machine (i9-13900KS, Windows 11 26100,
clang-cl 19.1.7, CMake 4.1.1). They apply equally to the 32-bit DLL build and to any 32-bit tool linked against msvcr90.

| Item | Value | Conf. |
|---|---|---|
| Language level of `build_vp_clang.py`'s mode (clang-cl `-m32`, VS 2008 `vsvars32.bat` environment, no `-std` flag) | `__cplusplus` 201103 (C++11), `_MSC_VER` 1500; a C++14 relaxed `constexpr` is rejected | [C] |
| Same with `/std:c++14` | compiles, but the link fails with an undefined `operator delete(void*, unsigned int)` (C++14 sized deallocation; msvcr90 lacks it) unless `/Zc:sizedDealloc-` is added | [C] |
| VC9's C++ library | no C++11 headers (`<atomic>`, `<thread>`, `<mutex>`, `<chrono>`, `unique_ptr`, `vector::data()`); `stdint.h` comes from clang's own headers | [C] |
| CMake 4.1.1 with clang-cl reporting `_MSC_VER` 1500 | adds `/INCREMENTAL:YES` to Debug links, which lld-link rejects ("could not open '/INCREMENTAL:YES'"); fix: rewrite the flag after `project()` and run the compiler check in Release (`CMAKE_TRY_COMPILE_CONFIGURATION`), see `ipc/cmake/clang-cl-x86.cmake` and the top-level `CMakeLists.txt` | [C] |
| Executables against msvcr90 | need the `Microsoft.VC90.CRT` side-by-side manifest embedded (the headers request 9.0.21022.8; WinSxS has 9.0.30729.9635, redirected by policy). `lld-link /MANIFEST:EMBED` embeds it directly; under CMake, which embeds with `mt.exe -outputresource`, llvm-mt cannot (the executable then fails to load) and the SDK v6.0A `mt.exe` (`C:\Program Files\Microsoft SDKs\Windows\v6.0A\bin\mt.exe`) works | [C] |

Details and the rules derived for the transport library: `research/03-dll-engine-boundary/ipc-design-and-measurements.md`
section 7.3, `ipc/PROTOCOL.md` section 11.

- 2026-11-10 (worker 2705, [C]) i0-ship e6f904ad6 + instance-aware instruments (fork `7307aefdf..440878983`, paths
  CvGameCoreDLL_Expansion2 and build_vp_clang.py) applies with `git apply` onto a `git archive` copy with no conflicts
  and builds clean (clang-cl 19.1.7 release, 5 min). A plain archive copy needs `update_commit_id.bat` stubbed (it exits 1
  outside git and stops build_vp_clang.py). Result: <pins_dir>/dlls/i0ship-e6f904ad-ia/ (README.txt), SHA-256
  7F815D9CE26C74FCBABA31A871198918B2AAA4A7F48415B291B14FFE88B13371, version "Release-5.4.8-11-ge6f904ad6+ia440878983 Copy",
  dll.names instance-aware; not run. Tracker 2705.
- 2026-11-14 (queue ticket 0164, [C, dumps]) **An `-ia` build must not be unloaded:** its instruments MemoryImports
  (every module's MSVCR90 allocator import slots) and MemoryDiagnostics' Lua allocator hook (`lua_setallocf` on the
  engine's Lua universe) are not undone at `DLL_PROCESS_DETACH`, so a mod change that unloads the game DLL ends the EXE at
  the next allocation through either (`VP_MEMIMPORTS=0` turns off only the first). Runs that change the mod set after an
  activation use the plain `i0ship-<pin>` build. Details: `../03-dll-engine-boundary/proxy-design.md` section 21.
