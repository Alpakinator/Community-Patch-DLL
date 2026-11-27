# Building the DLL for x64

Everything known about compiling `CvGameCore_Expansion2.dll` as a 64-bit binary. **The first x64 trial compile and
census link ran on 2026-11-10 (epic 1000, section 0).** Sections 1-7 are the earlier sources' view; where section 0
corrects them, they say so. (Corrected 2026-11-10; replaced "No x64 trial compile has been attempted".)

## 0. First x64 trial compile and census link (2026-11-10, epic 1000)

Source: tag `Release-5.4.8` (7307aefdf), DLL fork branch `x64`. Debug-style flags (`/Od`), all 173 `.cpp` of
`build_vp_clang.py`'s list. Evidence and scripts: `tools/x64-dll/evidence/2026-11-10-probe/` (this repo).

| Item | Value |
|---|---|
| Toolchain | clang-cl 19.1.7 `-m64`, lld-link, MSVC 14.43.34808 STL/CRT headers and libs (VS 2022 Community, `vcvars64.bat`), Windows SDK 10.0.26100.0, `/std:c++14`, `_HAS_AUTO_PTR_ETC=1` [C] |
| VC9 for x64 | Not possible on this machine: VS 2008 is installed without `VC\lib\amd64` [C] |
| Precompiled header | 1 error (`std::swap` specialisation for `SUnitIDValueContainer`, `CvUnit.h:392`, noexcept mismatch with the modern STL) [C] |
| Include path | The 16 `Lua\CvLua*.cpp` need `CvGameCoreDLL_Expansion2` as an include dir to find the PCH header under clang-cl `/Yu` [C] |
| Compile errors, all sources | **29 in 5 files** [C]: `CvTeam.cpp` 21 int->uint aggregate narrowing (x86 clang-cl in VC9 mode only warns; the x64 build uses `-Wno-c++11-narrowing` for parity), `CvHomelandAI.cpp` 5 `make_pair<T1,T2>(lvalue)`, `CvTacticalAI.cpp` 1 and `CvEspionageClasses.cpp` 1 `max`/`min` of `uint` with `size_t`, `CvGlobals.cpp` 1 crash handler reading `CONTEXT::Esp` |
| After fixes | 173/173 compile (commit b25fd45c0 on `x64`; fixes are valid for the x86 VC9 build too) [C] |
| Pointer-to-int casts (`-Wpointer-to-int-cast`, `-Wvoid-pointer-to-int-cast`) | 30 warnings at 7 sites: `FBatchAllocate.h` lines 62-158 (the "three reinterpret_casts" of the study), `CvGlobals.cpp` 2690/2701 (crash handler), `CvDllGame.cpp:641` (hard-coded EXE address block) [C] |
| Implicit 64->32 (`-Wshorten-64-to-32`, off by default) | 794 unique sites in 73 files; 59% in `Lua\CvLua*.cpp`; types mostly `size_type`/`size_t` -> `int`/`uint` [C]; triage is issue 1003 |
| Census link (`/FORCE:UNRESOLVED`, no closed libs) | **336 unresolved symbols** [C] |

Unresolved symbols by closed group (the census of 2026-11-09 in section 2 counted 338):

| Group | 2026-11-10 link | 2026-11-09 census |
|---|---|---|
| CvGameCoreDLLUtil | 123 | 124 |
| FireWorks | 68 | 70 |
| CvGameDatabase | 46 | 46 |
| lua51 | 46 | 42 |
| CvWorldBuilderMap | 32 | 32 |
| CvLocalization | 16 | 16 |
| FLua | 5 | 8 |
| **Total** | **336** | **338** |

The full list with demangled signatures is `tools/x64-dll/evidence/2026-11-10-probe/unresolved-336-grouped.txt`.
The grouping is by name pattern [I]: `ModDataEntry`/`ModType`/`ModEnum` count as CvWorldBuilderMap, and
`FireMallocAlignedNoTracking`/`FireFreeAlignedNoTracking` as FireWorks.

Consequence: the "pointer truncations a grep cannot see" (section 3) are now bounded. The compiler sees 30 explicit
casts and 794 implicit narrowings. The DLL's own code is as close to x64-clean as the study said.

### 0.1 The scripted build (issue 1001, 2026-11-10)

`tools/x64-dll/build.py` (usage in `tools/x64-dll/README.md`; evidence `tools/x64-dll/evidence/2026-11-10-build-1001/`)
builds the fork's `x64` checkout with the toolchain above plus `/MD`, `/DVP_OUT_OF_PROCESS`, without the x86-only
`clang.cpp`, into a separate `--out`; `--census` links with `/FORCE:UNRESOLVED` and writes `unresolved.txt`,
`unresolved-grouped.txt` and `unresolved.tsv` (mangled + demangled + first reference); `--stubs DIR` links every `.lib`
in DIR; `--shorten` adds `-Wshorten-64-to-32`. Measured on this machine (32 cores, other builds running, so upper
bounds) [C]:

| Build | Result |
|---|---|
| Debug, clean, b25fd45c0 | 173/173, 336 unresolved, byte-identical to the probe; PCH 13 s, compile 24.8 s wall, link 2.6 s, 51 s total; `--link-only` relink 2-12 s |
| Debug, 8e26515e8 (after 1002) | 336 unresolved, 0 pointer-to-int warnings |
| Release (`/Ox`, `-flto`, `/LTCG`), clean | 173/173, **326** unresolved (a subset of 336: LTO drops dead references, `NDEBUG` drops assert paths); full-LTO link **664 s single-threaded**; ~13 min total. Stubs and replacements must cover the debug 336 |
| `--shorten`, f3d2f5669 | 799 unique `-Wshorten-64-to-32` sites (probe: 794 at b25fd45c0). The +5 are header sites reported only by the PCH TU, which the probe and issue 1003 did not count (explained 2026-11-10, issue 1405, section 3.2; was "+5 not explained") |
| Sizes | debug DLL 27.2 MB (PDB 105 MB, map 47 MB); release DLL 23.6 MB |

**Build hygiene (issue 1405, 2026-11-10)** [C unless marked; evidence `tools/x64-dll/evidence/2026-11-10-1405/`]:

- **Release (LTO) link with all replacements, first run:** `build.py --config release --stubs` (stubs of
  `build_stubs.py --config release`, `lua51_x64.lib`; CvGameDatabase still the stub group) from a plain `git archive`
  copy of e9d0c4837: 173/173 compiled (84 s wall), full-LTO link **506.8 s** (a debug build ran concurrently for
  about 80 s of it), 616 s total; **0 unresolved, link rc 0, no `/FORCE` of any kind**; DLL 23,614,464 bytes. Smoke
  host (release build) boots it **16/16 steps** [L], the same steps as the debug DLL (section 8.1).
- `--src` is checked against `deps/dll.lock` (git checkout at the commit without tracked changes, or a plain copy
  without `.git`); `commit_id.inc` is generated in `--out` and mapped over the source with clang's `-vfsoverlay`
  (works whether or not the source has a stale file) [C, tested with clang-cl 19.1.7]. The fork's
  `update_commit_id.bat` is no longer run; it failed on plain copies. A plain copy's version string is
  `Release-5.4.8-6-ge9d0c4837 Copy`, a clean checkout's `... Clean` (both found in the built DLLs).
  *Note 2026-11-10 (issue 1508): a plain copy was accepted unchecked until then, so a copy of the wrong commit
  built as e9d0c4837. Now `deps/dll.lock` carries a source fingerprint (v1: SHA-256 over the 473
  `*.h/.hpp/.inl/.c/.cpp/.def` of the build-input directories plus `build_vp_clang.py`, CRLF folded to LF;
  `tools/x64-dll/src_fingerprint.py`) and `resolve_src` refuses a copy that does not match, about 1 s, before
  compiling [C]: a `git archive` copy of 47d7664e9 (differs from e9d0c4837 in one header,
  `FStlContainerSerialization.h`) is refused by all seven scripts that take `--src`, one edited byte or one extra
  header is refused, the e9d0c4837 copy and the git worktree both match. `build_db.py`, `run_ab.py` and
  `gen_ifaces.py` had no lock check before; all now use `resolve_src`.*
- One command `tools/x64-dll/build_all.py`; debug run from a plain copy, offline: 65 s, 0 unresolved, smoke 16/16.
- Narrowing gate and `/WX`: section 3.2.
- Third-party sources are found offline first through `tools/x64-dll/thirdparty.py` (`deps/third-party.lock`, cache
  outside the tree); a missing tarball offline fails `build_stubs.py` with exit 1 and one line naming URL, SHA-256
  and target path (tested with an empty `CV64_DEPS_CACHE` and `CV64_OFFLINE=1`).

`vcvars64.bat` takes 6-62 s here, so the script caches its environment. The probe's "30 pointer-to-int warnings"
excluded the PCH TU; counting it, 38 occurrences at 7 sites (same sites).

ABI choice (Minor decision, team-1000 coordinator, 2026-11-10): MSVC 14.x x64 ABI, `/MD`. Today the EXE and the
32-bit DLL share one msvcr90 heap and exchange `CvString`, STL and FireWorks allocations. The server must likewise
share the UCRT and vcruntime140/msvcp140 with the x64 DLL, so it builds with MSVC 14.x `/MD` as well. Recorded by the MC
as **D15**. Distribution consequence: the patch must ship the MSVC 14.x x64 runtime (`vcruntime140.dll`,
`vcruntime140_1.dll`, `msvcp140.dll`, app-local next to the server, as Microsoft's redistribution terms allow) or
require the Microsoft Visual C++ 2015-2022 x64 redistributable on players' machines; the UCRT itself is part of
Windows 10 and later. For the packaging epic. Since issue 1009 the patch also ships `lua51_x64.dll`.

A second drift source besides floating point (section 5): the STL changes from VC9 to MSVC 14.43. Unordered-container
iteration order and `std::hash` values differ. The full inventory (epic 1600, section 9) found that no unordered
iteration reaches a decision (the three that reached saves are fixed by D18) and that `std::sort`, the heap algorithms,
`nth_element` and `random_shuffle` behave identically in the two libraries [C]. (Updated 2026-11-10; was "[I;
inventory in issue 1004]".)

## 1. History of the verdict

| Date | Verdict |
|---|---|
| 2026-09-03 | x64 DLL blocked for **4-8 months** by five closed Win32 static libraries; boundary judged too large for RPC; 3-6 person-years overall. |
| 2026-11-09 | Thin-client study: the closed-lib dependency is **338 symbols**, ~6-11k lines to replace; "the blocker that shrank". Package A (x64 DLL) 125-200 agent-hours. |
| 2026-11-10 | REVIEW.md: same figures; adds that save identity on 64-bit is unvalidated ("one known trap, possibly more"). |

## 2. Closed-lib census (2026-11-09)

A symbol census over the DLL's 173 objects (tooling in that session's scratchpad, `libcensus/`; not in this repo).

| Closed dependency | Symbols the DLL uses | What it is | Replacement estimate |
|---|---|---|---|
| FireWorksWin32 | 70 | Mostly header templates compiled from source; the lib supplies 16 members, several just `GetProcAddress` trampolines into the EXE's logger, file system and allocator | 1.5k-2.5k lines; `FDataStream` must be byte-exact |
| CvGameDatabase (import lib) | 46 | Thin wrapper over SQLite 3.7.17; the EXE owns the connection today | 0.8k-1.2k lines |
| CvLocalization (import lib) | 16 | `TXT_KEY` lookup and argument formatting over ICU | 1k-2.5k lines |
| CvWorldBuilderMap | 32 | `.Civ5Map` reader and writer | 2k-4k lines; byte-exact format |
| FLua | 8 | Header templates | (in the "under 1k" group) |
| CvGameCoreDLLUtil | 124 | 121 trivial enum serializers (study) | (in the "under 1k" group) |
| lua51 | 42 | Stock Lua 5.1.4 | (in the "under 1k" group) |
| **Total** | **338** | | **~6k-11k lines** |

Line estimates are judgement from symbol counts and code bytes (study caveat).

The headless host's `why-32-bit.md` (independent, earlier) frames the same work: the database and localization DLLs
"would have to be rewritten against their interfaces. The game DLL uses the database layer heavily, so this is the bulk
of the work", and Lua 5.1 "would need a 64-bit build that behaves like the game's copy". It calls a 64-bit host "weeks
of work, mostly the database layer". The SDK ships only headers and 32-bit import libraries for the Firaxis DLLs.

## 2.1 Stub libraries and replacements (epic 1000, 2026-11-10)

Under `tools/x64-dll/closedlib-stubs/`: one folder per group, built by `build_stubs.py` into `<group>.lib`; every
stub calls `STUB_CALL(group, signature)` from `common/stublog.h` (first call of each symbol logged in order with
sequence number and thread id, later calls counted, flushed at DLL unload or process exit; file from `CV64_STUBLOG`,
default `stubcalls.log`). Per-group symbol lists with signatures: `SYMBOLS-database-localization-lua.md` (issue 1005),
`SYMBOLS-fireworks-dllutil-worldbuilder-flua.md` (issue 1006).

Issue 1005 (CvGameDatabase 46, CvLocalization 16, lua51 46):

- **lld-link 19.1.7 does not satisfy a `dllimport` reference `__imp_X` from a static library's `X`** [C]. All 62 database
  and localization symbols stayed unresolved until `build_stubs.py` generated a selectany `__imp_X` pointer per
  definition (found with `llvm-nm`). Applies to every group.
- Database stubs return false / 0 / `""` (`GetText` returns `""`, never NULL: `CvTypes.cpp:124` assigns it to a
  `std::string`); localization stubs return the key itself. (Database stubs replaced 2026-11-10 by the real library,
  section 10.) Replacement estimates: database over SQLite 0.5-0.8k lines,
  localization without ICU 1-1.5k lines [I].
- **lua51 is real, not stubbed**: stock Lua 5.1.4 from lua.org (SHA-256 `b038e225...af3a`, 216,679 B, matches the
  published hash) [C]. The game's `lua51_Win32.dll` is stock 5.1.4 compiled as C [C: version string, `longjmp` import,
  stock `ipairs`]. Therefore the `ipairs` that visits index 0 and the hidden `debug` library (headless
  `engine-contract.md` 10.5) come from the EXE, not from Lua: the server must install them itself [C].
- Number-to-integer: the game's `lua_tointeger` (lua51_Win32.dll VA 0x10001610) uses x87 `fistp`, round-half-to-even;
  stock x64 Lua truncates [C]. The x64 build patches a copy of `luaconf.h` to use `cvtsd2si`, which matches the game
  bit for bit [L self-test], assuming the EXE's x87 control word is at its default [I]. Remaining differences (x87 vs
  SSE2 arithmetic, msvcr90 vs UCRT `strtod`/`printf`/math, GC timing) are listed in the symbol file [I].
- **Only one copy of Lua may touch a Lua state** [C]: Lua 5.1 compares against its own statics (`dummynode_`,
  `luaO_nilobject_`), so a table created by one copy and resized or freed by another corrupts the heap. With lua51 linked
  into the DLL, the server (I1-B) must use the DLL's Lua or export one copy to both.
Issue 1006 (FireWorks 68, CvGameCoreDLLUtil 123, CvWorldBuilderMap 32, FLua 5 = 228). With all seven libraries the
debug DLL links with **0 unresolved, no `/FORCE`** (lld-link rc 0, 27,386,368 B) [C]. Of the 228, 195 are **real
implementations** written from the x86 disassembly of the fork's own closed libs (`FireWorksWin32.obj`,
`CvWorldBuilderMapWin32.obj`, `FLuaWin32.lib`, `CvGameCoreDLLUtilWin32.lib`), 7 init-only (run in DLL static init),
1 partial (`FDebugHelper::GetInstance`, no dbghelp), 25 logging stubs (24 CvWorldBuilderMap/`Mod*` members, `.Civ5Map`
Load/Save return false; `FDebugHelper::LoadSymbols`) [C]. x64 self-test 19/19 [L]. Byte formats confirmed [C]:

- Enum serializers write a 4-byte int, **except `GameTypes`: 1 byte** (char, sign-extended on read).
- `FDataStream`: bool/char/uchar 1, short/ushort 2, int/uint/unsigned long/float 4 (unsigned long stays 4 on x64),
  int64/uint64 8, GUID 16 raw; streams always `ENDIAN_LITTLE`; uint64 never byte-swapped (x86 quirk). `std::string` =
  4-byte length + bytes, no terminator (member `Write` uses a signed length, free `operator<<` unsigned; same bytes);
  `ReadString` throws on `len < 0` or `len > GetSizeLeft`. No `wstring` stream member is referenced by the DLL.
- `FCRC`: polynomial 0xEDB88320, reflected table, init ~0, **no final XOR** (`g_CRC32.Calc` = bitwise NOT of standard
  CRC-32).
- `FAutoArchive::saveDelta`: count, then (index, delta) per changed variable in `std::set<pointer>` order.
- EXE trampolines (`GetProcAddress(GetModuleHandleA(NULL))`, cached): `FIFileSystem::GetInstance` -> `GetFileSystemMgr`,
  `FILogFileMgr::GetInstance` -> `GetLogFileMgr`, `FStringA/W_GetStringInitData` -> exports of the same names,
  `FireMalloc/FreeAlignedNoTracking` and `FireMalloc/FreeNoTracking` -> `FSBABind` (needs `GetSmallObjectAllocator` +
  8 `FSBA*` exports, falls back to the CRT). The server must export these names; a missing one aborts with a FATAL
  stub-log line.
- `FStringW::ExtractFileTitle` on x86 does not strip the extension (it calls `Mid` on itself, a no-op); kept.
- Stub and DLL builds must use the same defines: `VPDEBUG` adds members to `FAutoVariableBase` (fixed in
  `build_stubs.py`, 2026-11-10).

- Three FLua members whose signatures mention `lua_State` (`Value::Value(lua_State*,int)`, `Value::Set`,
  `Details::CCallWithErrorHandling`) live in `lua51.lib`; the name-pattern census filed them under lua51.

## 3. The DLL's own code

From the study:

- No inline assembly.
- Three `reinterpret_cast<unsigned int>` of pointers in one batch-allocator header.
- A crash handler that reads 32-bit registers.
- One block of hard-coded EXE addresses that becomes moot out of process.
- Pointer truncations a grep cannot see are uncounted.

From headless `why-32-bit.md`: "about 19 rough hits" of pointer-to-integer casts in a Vox Populi fork's DLL; the fork's
only inline assembly is in its headless digest additions. (Different count from the study; see Contradictions.)

### 3.1 Result on branch `x64` (issue 1002, 2026-11-10, commit 8e26515e8)

Details and inventory: `tools/x64-dll/dll-own-64bit-code.md`; evidence `tools/x64-dll/evidence/2026-11-10-w1002/`.

| Item | Result |
|---|---|
| Pointer-to-int casts | All 7 sites fixed (30 warnings; the 3 `FBatchAllocate.h` sites gave 27 of them, one per template instantiation): now `size_t`/`UINT_PTR`. x64 compile, 173/173: 0 errors, 0 pointer-to-int warnings; 186 `-Wdeprecated-declarations` remain [C] |
| Crash handler (`CvGlobals.cpp`) | Ported: `AppendStackScan` walks 8-byte slots from `Rsp`; addresses print full width (`CRASH_ADDR_FMT`); memory sizes are no longer truncated to 32 bits; on x64 the "address space low" class uses the largest free block overall [C build; not exercised by a real crash]. The DLL's `SetUnhandledExceptionFilter` does not chain a previous filter; the server must decide who owns the process filter [I] |
| `VP_OUT_OF_PROCESS` define | Defined by the x64 build. `CvDllGame::InitExeStuff` (`CvDllGame.cpp:595-711`) is compiled out: it found the EXE's force-resync byte by hard-coded VA (DX11 0x02DD2F68, DX9 0x02DC2D68, Tablet 0x02DD4F50) and the EXE binary type; out of process `BIN_UNKNOWN` keeps the feature off. The shell-side proxy would have to write that flag for the server. With the define, the object no longer imports `GetModuleHandleA`/`GetModuleFileNameA` [C]. Error popups (asserts, preconditions, crash dialogs) are off in the server; logs and minidumps still written [C] |
| Instruments | `EngineQueueGuard`, `EngineAllocTrace`, `MemoryImports`, `ShellHelperLoader` are not on 5.4.8 / `x64`; they exist only on `claude/shell-gates-d8e422`. Whoever ports them puts them under `#if !defined(VP_OUT_OF_PROCESS)` [C] |
| Still EXE-dependent | `writeSaveGameDB` opens the EXE's `Civ5SavedGameDatabase.db` in the cache folder; the server must provide that path [C] |
| Bitness grep beyond the compiler | No live `GWL_*`, `_beginthread`, `HWND`-as-`int`, `long`-as-pointer or `Interlocked` issues; `%u` with `size_t` truncates only above 4G and only in log text [C grep] |
| x86 build | Builds and links; `AppendStackScan`, `CustomFilter`, `InitExeStuff` are instruction-identical to the baseline; only `__LINE__` constants moved [C llvm-objdump] |

### 3.2 Implicit 64->32 narrowings (issue 1003, 2026-11-10)

Table of all sites: `tools/x64-dll/truncation-triage.md`; evidence `tools/x64-dll/evidence/2026-11-10-w1003/`.

- `-Wshorten-64-to-32` at f3d2f5669: 948 warnings, **794 unique sites in 73 files**, identical to the probe and to
  8e26515e8 [C]. 1004's pins removed none (they were 64-bit writes, not narrowings).
- Classification: **773 benign, 19 cast-for-clarity only** (12 `size()-1` wraps to -1 on both builds, 6 `FFastVector`
  pointer differences, 1 `std::count`), **2 real**: `CvDatabaseUtility.cpp:125` allocation size narrowed to
  `unsigned int`, fixed to `size_t` (fork commit on `x64`); `CvPlayer.cpp:45842` a save count already declared
  `uint32` (the narrowing is the intended pin) [C]. No site narrows a pointer, handle, time, file size or hash [C: every
  non-Lua site read].
- **L1, the 492 Lua integer sites** (`lua_tointeger`, `luaL_checkinteger`, `luaL_optinteger`): the game's
  `lua51_Win32.dll` converts with `fld; fistp dword` (RVA 0x1610): round-half-even, 0x80000000 for NaN and out of range.
  **Stock** x64 Lua truncates to 64 bits and the DLL then narrows to 32, which differs for 1.5, 2.7, -1.5, 3e9,
  4294967295 and NaN [C test program]. Resolved by issue 1005's `lua51.lib`: its x64 `lua_number2integer` is
  `_mm_cvtsd_si32`, which matches the game on all 27 test inputs [C], while `LUA_INTEGER` stays `ptrdiff_t` in both
  the library and the DLL's header (consistent ABI); values come back in int32 range and narrow losslessly.
  Integers in `int` range round-trip identically; no 64-bit value or seed crosses into Lua [C]. (1003 proposed
  `LUA_INTEGER int` under `_WIN64` in the fork's `luaconf.h`; not needed given 1005's conversion, and it would have
  required the header and library to change together.)
- Warning policy (recommended): `-Wshorten-64-to-32` off for upstream DLL sources; `-Werror` for project-added code
  (closed-lib replacements, Lua build, server, proxy, new DLL files); a census gate on `build.py --shorten` that fails on
  any site not in the baseline `sites.tsv` (keyed by file, column, types and text, so line shifts do not count).
  **Implemented 2026-11-10, issue 1405:** `build.py --shorten` gates against `tools/x64-dll/narrowing-baseline.tsv`
  (key: file relative to `--src`, column, warning text, source line text; multiset; per config) and exits 3 naming
  each new site; `/WX` for the stub, `common/` and smoke-host sources (fork headers as `/external:I`), not for the
  fork, Lua or SQLite; usage in `tools/x64-dll/README.md`. Baseline at e9d0c4837 (deps/dll.lock): **798 unique sites**
  (952 occurrences), debug and release identical in count [C]. 798 = 1003's 794 - 1 (`CvDatabaseUtility.cpp`, fixed
  in 47d7664e9) + 5 header sites that only the PCH TU `_precompile.cpp` reports (`CvAStarNode.h:237`,
  `CvCultureClasses.h:67`, `CvTacticalAI.h:1295`, `FLuaCommon.h:213`, `FLuaTypes.h:114`); 1003 and the probe counted
  without the PCH TU, `build.py` counts it [C, per-TU `compile.log`]. The 5 are `size()`/`size_t` to `int`
  (3) and `long long` to `int` in the FLua conversion templates (2), the patterns triaged as benign above [I: not
  re-triaged one by one].

Toolchain: the DLL is built today with VC9 (MSVC 2008) ABI, `/MD` msvcr90; `build_vp_clang.py` is the clang build
(`--config release`, LTO ~5 min). The unit-test study notes VC9 headers lack `<chrono>/<atomic>/<mutex>/<thread>`. The
sources do not say which x64 toolchain package A would use.

## 4. The `size_t` serialization trap

`FStlContainerSerialization.h:71` and `:132` write `size_t` to the save stream. On x64 `size_t` is 8 bytes, so every
existing save breaks unless those widths are pinned to 32 bits. REVIEW.md: "one known trap, possibly more; verified by
byte-comparing saves from the 32-bit and 64-bit DLL on the same game". Package A's gate is "loads and round-trips
existing saves".

### 4.1 Result on branch `x64` (issue 1004, 2026-11-10, commits 21f396027 and f3d2f5669)

Full audit: `tools/x64-dll/save-layout-audit.md`; census tool `tools/x64-dll/save-audit/save-width-census.py`;
evidence `tools/x64-dll/evidence/2026-11-10-save-census/`.

- The two `FStlContainerSerialization.h` sites were not the only trap: a compile census over all 173 sources
  (instrumented `FDataStream` 64-bit overloads) found **9 64-bit stream sites on x86 and 327 on x64, i.e. 318 x64-only**
  [C]. All 318 are pinned: STL container counts through `FStlSerializedCount()` (x64: narrow + `FAssertMsg` range check;
  x86: no-op macro), 4 `CvInfosSerializationHelper.h` writes, and 18 sites in `CvGame.cpp/.h` (saved `size_t`
  containers and their string hash become `uint`), `CvAStar.cpp` (path count) and `CvEspionageClasses.cpp` (14 `size()`
  writes whose readers read 32 bits: without the pin an x64 DLL could not read its own saves) [C].
- After the pins: 0 x64-only sites; the 9 remaining are genuinely 64-bit members on both builds
  (`m_lJONSCultureEverGeneratedTimes100`, `m_lLastTurnLifetimeCultureTimes100`, `m_ullRandomSeed`) [C].
- **x86 save bytes unchanged, tested** [L, issue 1008, 2026-11-10 03:11-03:36 EST]. Headless host, Small Continental
  Drift with fixed seeds played by the official 5.4.8 DLL (SHA-256 4E91C58A...), saves at turns 60 and 150, each
  loaded and immediately re-saved by clang x86 release builds sharing one version string: official vs official
  byte-identical (procedure deterministic); official vs clang Release-5.4.8 identical except the version string;
  **Release-5.4.8 vs `x64` f3d2f5669 (1002 + 1004 pins) byte-identical at both turns**. (Replaced 2026-11-10: "[I] a type
  argument plus a clean x86 link, not a test".) *Qualified 2026-11-10 by the librarian: "byte-identical" here means
  load-and-resave round trips. One input save per turn (the final save of a new-game run with the official DLL), loaded
  by each DLL in its own host process and saved before any turn is played; whole file compared, header raw and body
  inflated, nothing masked. This tests the writers on identical state, not the play. Final saves of two independent
  plays are never byte-identical, even with the same DLL (epic 1300 task 1307: unseeded game GUID, uninitialised bytes,
  great-person names, a hash-ordered list). Methods and a proposed D10 test:
  `../06-tooling-and-workflows/headless-save-comparison.md`.*
- **D18 sorted writes** (fork commit e9d0c4837 on `x64`, one self-contained 4-line change in
  `FStlContainerSerialization.h`, cherry-pickable onto the increment 0 branch): the `tr1::unordered_map`/`unordered_set`
  writers write through a `std::map`/`std::set` copy, so elements are in key-value order; count and element bytes
  unchanged, readers untouched. Headless [L]: f3d2f5669 vs e9d0c4837 saves have the same length and every differing byte
  lies in a re-ordered container holding the same records in ascending key order (turn 60: 8,776 B in 18 `m_aValues`;
  turn 150: 7,402 B in 15 `m_aValues` + 31 `m_miInstantYieldsTotal`; 0 unattributed); the official 5.4.8 DLL loads the
  sorted save. `m_AdvisorMessagesViewed` is empty in autoplay games, so its sorted write is covered by the code only.
  Evidence: `tools/x64-dll/evidence/2026-11-10-save-sorted-1008/`.
- Headless byte-compare as a regression gate [L]: about 1 min per save once the DLLs are built (~6 min per LTO
  release build). Working setup: game folder = the toolkit's `VP_MODPACK_5_4_8_NEUTRALS` copy under
  `%USERPROFILE%\Documents\verns_civ5_vp_toolkit\verns_civ5_vp_toolkit\...` (doubled folder name; the Steam install no
  longer has the modpack), `--no-mods`, scratch `user_dir`/`workspace`/`db_cache`, `--dll` per run. Not done yet:
  any x64 save (needs the x64 DLL in a 64-bit host, package I1-E).
- **Byte identity still fails on hash iteration order** [C source]: `CvCity::m_miInstantYieldsTotal`
  (`CvCity.cpp:32427`), `CvExplorationPlotSet::m_aValues` (three per player, `CvEconomicAI.cpp:4765`) and
  `CvGame::m_AdvisorMessagesViewed` (`CvGame.cpp:10887`) are serialized in `tr1::unordered_*` iteration order,
  which differs between the VC9 and MSVC 14 STLs. Load is order-independent. Options sent to the MC as a decision
  (2026-11-10): sorted write on both builds (recommended), VC9-order emulation on x64, or an order-normalizing
  compare. Escalated to the owner as tracker issue 0074 (Large: touches D10), with the MC's and the team's recommendation A
  (sorted write) plus C (normalizing compare for saves from older x86 DLLs).
- Closed-library byte formats the replacements must reproduce (disassembly of `FireWorksWin32`) [C]: `Write(bool)` 1
  byte; `Write(unsigned long)` 4 bytes (a distinct overload on x64) with byte swap when stream endianness differs;
  `std::string` = 4-byte length + bytes, no terminator; `std::wstring` = 4-byte count + 2 bytes per char; `FCRC::Calc`
  feeds every saved info-type hash and the save header's data hash (final-xor unverified). The sync archive's
  `m_deltas` is a pointer-ordered `std::set`, but all variables of one archive live in one `SyncVars` object, so the
  order is member declaration order on both builds and in every run [C source, corrected 2026-11-10 by epic 1600;
  was "network delta order depends on heap addresses"].

Related save-format facts from the unit-test study (not x64-specific, but they constrain any serializer change):
`CvEnumsUtil.h:223-227` sizes `SpecialistTypes` by promotion count and `:73-77` sizes `CivilizationTypes` by feature
count; two oversized `CvCityCitizens` maps are serialized, so fixing those also changes the save format.

## 5. Floating point: x86 vs x64

- **Corrected 2026-11-10 (epic 1600):** the official VP 5.4.8 DLL does its float arithmetic in **SSE**, not x87. It is
  built by `build_vp_clang.py` with clang-cl `-m32 -msse3 /fp:precise` (fork `build_vp_clang.py:305`), and clang uses
  SSE scalar instructions for `float`/`double` when SSE2 is enabled. Disassembly of the 5.4.8 modpack DLL (SHA-256
  `4E91C58A...`, `(1) Community Patch\CvGameCore_Expansion2.dll` in the toolkit's `VP_MODPACK_5_4_8_NEUTRALS` copy;
  `llvm-objdump -d`, 2026-11-10) has **0** x87 arithmetic instructions (`fadd`/`fmul`/`fdiv`/`fsub` and variants) and
  1,620 SSE scalar arithmetic ones (`addsd`, `mulsd`, `divss`, ...); the x87 instructions left are loads/stores for the
  32-bit return-value ABI (`fstpl` 331, `fstps` 112) and int64 conversions (`fildll` 282) `[C disassembly]`.
  (Replaced: "The 32-bit DLL builds with VC9 defaults: `/fp:precise`, no `/arch`, so it uses the **x87** unit", from
  `why-32-bit.md`, which describes a VC9-compiled DLL. A VC9 build would use x87; the shipped 5.4.8 DLL is not one.)
- 64-bit code always uses **SSE2**.
- So the arithmetic itself need not drift. What can still differ: the C runtime's `pow`, `log`, `log10`, `atan2`
  (`MSVCR90.dll` on x86, imported by the 5.4.8 DLL; UCRT on x64; 29 call lines, section 9 / `stl-drift-sites.md`
  category 7), int64-to-double conversions (`fildll`, exact below 2^53), and code generation differences between the
  two clang targets (FMA contraction is not possible on either baseline target without FMA enabled) `[I]`. Whether a
  64-bit DLL's games **drift from the 32-bit game's after some turns** (the earlier statement, from `why-32-bit.md`) is
  therefore open, not given: it is now a measurement to make (section 9.4). The consequences below assumed drift.

Consequences:

| Area | Effect |
|---|---|
| Multiplayer | Mixed 32/64-bit lockstep is broken: all players must run the same build ("fatal for mixed multiplayer"). REVIEW.md: multiplayer with original clients is lost in both the shell and the pure port. |
| Replays / A/B | A 64-bit DLL cannot be checked turn-for-turn against the real 32-bit game; `c5h verify` against the real game "would then fail by design", only the host against itself. Fixed-seed A/A determinism within one x64 build is still expected and is package A's gate ("A/A determinism in a 64-bit host"). |
| Existing saves | Loading works if serialization widths are pinned; play continues on a different float path from that point. |

The study's recommendation: decide on the thin client "only once [package A] exists and its floating-point drift is
understood".

## 6. What the EXE still owns

Unchanged since the 2026-09-03 note and still true on 2026-11-09:

| EXE-owned piece | Consequence for an x64 DLL in another process |
|---|---|
| Lua VM | UI Lua (and today GameEvents listeners) runs in the EXE's 32-bit VM; DLL-registered libraries must be marshalled or re-hosted |
| DB connection | The EXE passes its SQLite connection to the DLL; the server opens its own copy (the headless database builder does this); UI GameInfo stays in the EXE. Done for the smoke host 2026-11-10 (section 10): the host owns a `Database::Connection` from `CvGameDatabase_x64.dll` and passes it with `SetGameDatabase` |
| Netcode | `send*`/`Response*` loopback and multiplayer messaging go through the EXE |
| Serialization streams | The EXE hands the DLL its own stream objects for save/load |
| Allocator | FireWorks lib members trampoline into the EXE's allocator, logger and file system via `GetProcAddress` |
| Plot storage | Templated on the engine allocator pool |

Plus the 22 C exports from the EXE the DLL resolves at load (section 1 of interfaces-and-traffic.md).

## 7. Package A as defined

| Field | Value (study, 2026-11-09) |
|---|---|
| Scope | Replace the closed libraries, pin serialization widths, x64 toolchain, fix what the compiler finds |
| Sessions | 25-40 |
| Agent-hours | 125-200 |
| Gate | Loads and round-trips existing saves; A/A determinism in a 64-bit host |
| Standalone value | A 64-bit DLL in a 64-bit headless host; "valuable without the client" (64-bit headless simulator, dedicated-server building block) |

The memory note's "how to apply": recommend the in-place DLL growth fixes and the slot-poll lock fix first, then package
A as a standalone milestone.

## 8. Smoke run: the x64 DLL in a minimal x64 host (issue 1007, 2026-11-10)

Host: `tools/x64-dll/smoke-host/` (generator, build, runner, README); evidence
`tools/x64-dll/evidence/2026-11-10-smoke/` (`run3` normal, `run4-crashfilter` crash-handler run). DLL: x64 debug, fork
`x64` @ f3d2f5669, all seven stub libraries, SHA-256 63158239...a5564. Not a game run; outputs confined to a scratch
run directory.

- Host: typed logging overrides for every pure virtual of `ICvEngineUtility4` (260), `ICvUserInterface2` (152),
  `ICvEngineScriptSystem1` (13), `FIFileSystem` (15), `FILogFileMgr` (7), `FILogFile` (11), generated from the fork
  headers (typed because on x64 a `float` or by-value `CvString` return breaks an untyped "return 0" thunk); all 22
  host exports, the 22nd under its x64 mangling `??4_Init_locks@std@@QEAAAEAV01@AEBV01@@Z`; one SEH guard per step [C].
- **Steps passed** [L]: `DllMain`, `DllGetGameContext`, `SetDLLIFace` (true), `SetGameDatabase`, `Init`, `QueryInterface`
  (`ICvGameContext2`) + `SetEngineUserInterface`, `GetDatabaseLoadUtility`, `FlushGameDatabaseData`,
  `PerformDatabasePostProcessing`. `FreeLibrary` succeeds even after the crash below.
- **First failure** [L]: `CacheGameDatabaseData` -> access violation reading 0x20 in `index2adr` <- `lua_pushvalue` <-
  `LuaSupport::InitLuaFramework` (`CvLuaSupport.cpp:92`) <- `CvDllDatabaseUtility.cpp:189`, because
  `CreateLuaThread("VP_LUAAPI")` returned NULL (the host had no Lua state).
- **Lua packaging** [C]: the x64 DLL links no `luaopen_*` and no `luaL_openlibs`, so a host cannot open the standard
  libraries through the DLL's static Lua copy, and a second copy is forbidden (section 2.1). The x86 DLL links
  `lua51_Win32.lib`, the import library of the shared `lua51_Win32.dll` the EXE also uses. Decision (Minor,
  coordinator, 2026-11-10): on x64 Lua becomes a shared `lua51_x64.dll` used by the DLL and the server (issue 1009).
- Host exports actually called [L]: `FStringA_GetStringInitData` (12) and `GetLogFileMgr` (221) during `DllMain`;
  `GetSmallObjectAllocator` (1) and `FSBADeallocate` (2) at unload; the other 18 not at all in this boot slice.
  Engine-interface slots called: `QueryInterface` 0, `InitGlobals` 122, `GetCacheFolderPath` 143,
  `ShouldValidateGameDatabase` 258, `GetScriptSystem` 3, `CreateLuaThread` 0, `GetLog` 3 (221 calls) [L].
- Stub log [L]: 25 distinct closed-lib stubs, 4,174 calls; heaviest `Database::Results::Bind` and `Reset` (1,880 each).
  Counts of stubs first called during unload were lost (flush ran before them); fixed in issue 1009.
- **Vtable order is not declaration order** [C]: overload sets come out reversed (`IsModActivated` 10/11,
  `endTurnTimerLength` 187/188, `CreateArgs`, `CallAccumulator`, every `FIFileSystem` pair, `GetLog`/`DeleteLog`), and
  `ICvUserInterface2::AddPopup` precedes `PublishRemotePlayerTurnStart`. Generated proxies (I1-B/C) must take slots from
  the compiler; the full x64 slot table is `run3/interface-slots.tsv`.
- The DLL's x64 crash handler, exercised with `--crash-to-dll-filter` [L]: `CustomFilter` wrote `crashlogs/crashes.log`
  and an 89 MB minidump; crash class right, addresses full 64-bit, sizes untruncated, the 8-byte stack scan found real
  return addresses; "Location (in file)" = RVA - 0xC00 holds for the x64 DLL too (`.text` VA 0x1000, raw 0x400) [C].
- `FTempHeap` x64 layout (static_assert against `FTempHeap.h`) [C]: lock +0x00, `m_pMemHead` +0x10, offset +0x18,
  capacity +0x1C, flags +0x20, size 0x30.
- System32 dbghelp loads only the DLL's export table, not its PDB, in this host; symbolization is done offline with
  llvm-symbolizer [L].

### 8.1 Boot completes with a shared Lua DLL (issue 1009, 2026-11-10)

Evidence `tools/x64-dll/evidence/2026-11-10-smoke-lua/`; DLL from fork `x64` @ 47d7664e9, debug.

- `lua51_x64.dll` (`LUA_BUILD_AS_DLL`, 1005's rounding patch, 123 exports incl. all `luaopen_*` and `luaL_openlibs`) +
  import library `lua51_x64.lib`; `build_stubs.py --lua dll` (default). The game DLL imports `lua51_x64.dll` and contains
  no Lua code; `lua51.lib` now holds only the FLua members. Debug DLL links with 0 undefined, 0 duplicate symbols and
  **no `/FORCE` of any kind** (`build.py` passes `/FORCE:MULTIPLE` only in census mode now) [C].
- Smoke host loads `lua51_x64.dll` by full path before the DLL, checks after `LoadLibrary` that the DLL's import resolved
  to the same module (one Lua copy), creates the main state, opens the standard libraries, answers
  `CreateLuaThread`/`FreeLuaThread` as civ5-headless does, calls `RegisterScriptLibraries` via
  `GetScriptSystemUtility`; added the rest of phase A (`GetPreGame`/`setVersionString`/`GetDLLVersion` "1.0.0",
  `GetGame`->`GetGameDeals`->`CreateDeal` = deal 0, pregame reset `setGameMode`/`setGameType`/`resetSlots`/`resetGame`/
  `resetPlayers`). **All 16 boot steps pass; `CacheGameDatabaseData` returns true and the DLL's embedded `VP_LUAAPI`
  script logs "Embedded script ran without errors!"** [L].
- ~~Next blocker: the database stubs~~ Resolved 2026-11-10 by epic 1400 (section 10): with the real library and the
  VP 5.4.8 database the 67 warnings, the missing defines and `Colors` table and the `Civ5SavedGameDatabase.db` failure
  are all gone [L]. (Was: "`Gamecore.log` has 67 ... warnings, two missing defines, a missing `Colors` table, and
  `Civ5SavedGameDatabase.db` cannot be created".)
- Stub-log flush [C cause, L fix]: clang-cl 19.1.7 ignored `#pragma init_seg(lib)` (the initializer landed in
  `.CRT$XCU`), so the flusher ran before `gGlobals`' destructor. Fixed with `atexit(ExitFlush)` registered from a
  `.CRT$XCB` initializer (runs before every other static initializer, so the flush follows every static destructor).
  After the fix: one COUNT block after the last call, 26 stubs, 4,320 calls, no late calls.
- Not reproduced and not needed by the boot: the EXE's index-0 `ipairs`, hiding `debug` after boot, `include()` and the
  VFS; phase-A calls that need EXE data (`loadFromIni`, `setDLCAllowed`, last-session setup, `SetLogging` flags).
  Release config of the Lua DLL and a release link without `/FORCE:MULTIPLE` not run.

## 9. STL drift audit (epic 1600, 2026-11-10)

Read-only audit of the fork at `x64` @ `e9d0c4837` against the VC9 and MSVC 14.43 library headers, 2026-11-10
03:52-04:16 EST (background agent of the MC, issue 1600). Full site list, search patterns and sample check:
[`stl-drift-sites.md`](stl-drift-sites.md). Answers open question Q14's source half.

### 9.1 Result

- **The order-producing algorithms are identical in the two libraries** [C, headers read side by side]: `std::sort`
  (same introsort: `_ISORT_MAX` 32, same ninther, same fat-pivot partition, same recursion budget), `push_heap` /
  `pop_heap` / `make_heap` (same sift steps, hence `std::priority_queue` too), `nth_element`, `random_shuffle` with a
  caller RNG (same N-1 draws with arguments 2..N, same permutation), equal-key insertion into `multimap`. With a
  strict-weak-order comparator they give the same permutation on the same input, ties included. Every comparator in
  the DLL is one (72 reviewed; none compares pointers or uses `<=`). So the Q14 suspects "heap and `priority_queue` tie
  order" and "`random_shuffle` drawing a different number of RNG values" are **not drift**. `TContainer::Shuffle`
  (`TContainer.h:219`) is not even called.
- **What does differ**: hash-table iteration order (different hash functions), heap addresses (between any two runs,
  and between builds), and the CRT math functions (`MSVCR90.dll` vs UCRT).
- **Unordered containers**: 25 containers; 8 iterations. The three that reached save bytes are fixed by D18; the only
  remaining effect is the insertion order of Lua's `GameInfoTypes` (`Lua/CvLuaGameInfo.cpp:104`), which no Lua file in
  the fork iterates. Everything else is lookup or deletion.
- **Pointer-ordered containers**: 15. `CvPlot*` keys follow plot-index order (one array, `CvMap.cpp:600`) and tactical
  positions follow creation order (preallocated pool), so those agree across builds. **One site changes outcomes**:
  squad moves send their move missions in `CvUnit*` address order (`CvUnit.cpp:15441`, iterated `:15542`). This is
  pre-existing run-to-run nondeterminism on x86 as well. `CvCityManager`'s address-ordered map (`CvCityManager.cpp:13`)
  only changes tie order in a list whose sole consumer asks "does any route exist", so it is decision-neutral. The
  sync archive's `m_deltas` is ordered by member offset, not by heap address (correction in 4.1).
- **CRT math**: 29 call lines of `pow` (21), `log` (6), `log10` (1), `atan2` (1) can differ in the last bit between
  `msvcr90` and the UCRT [I] and feed game state: combat damage (`CvUnitCombat.cpp:3105`, `pow(r, 4.0)` in
  `DoDamageMath`), growth threshold (`CvPlayer.cpp:46538`), policy, purchase, upgrade and maintenance costs, minor-civ
  influence decay, diplomatic-victory vote count, trade-route pressure. A difference changes an integer result only
  when the exact value is within one ulp of a boundary, so rarely, but each such flip changes game state and saves.
  `sqrt`, `floor`, `ceil` agree (correctly rounded / exact). Together with the section 5 correction (the 5.4.8 DLL
  already uses SSE arithmetic), these calls are probably the main source of x86/x64 float divergence [I].
  **Corrected 2026-11-10 (epic 1800, 9.5):** measured on two played games, the CRT maths differs only at the combat
  `pow` by 1 ulp and never changes an integer; the x86/x64 divergence comes from the `AttackKey` equality bug
  (`CvTacticalAI.h:1071`), a hash-container behaviour this audit did not cover (lookup with an incomplete `==`).

### 9.2 Counts by category

| Category | Pattern hits | Sites | Changes outcomes / RNG | Changes saves | Lua only | No effect |
|---|---|---|---|---|---|---|
| 1 Unordered containers (`hash_*`, `tr1::unordered_*`) | 52 | 25 containers, 8 iterations | 0 | 0 (3 fixed by D18) | 1 (`GameInfoTypes`) | rest |
| 2 Sorts, heaps, selection | 4 `sort`, 245 stable, 10 `nth_element`, 9 heap ops, 1 `priority_queue` | 269 calls | 0 | 0 | 0 | all (identical algorithms, strict comparators) |
| 3 Shuffles / RNG in library code | 1 `random_shuffle`, 3 `shuffleArray` calls | 2 | 0 | 0 | 0 | 2 (`random_shuffle` uncalled; `shuffleArray` is own code) |
| 4 Ordered by pointer value | 47 | 15 containers | **1** (`CvUnit.cpp:15542`, squad move order) | 0 | 0 | 14 |
| 5 Hash values stored or compared | 15 + named | 5 groups | 0 | 0 | 0 | 5 (`CvUnit.h:294` hash differs only by width, not in practice) |
| 6 Lua-visible order | - | 1 (= category 1's `GameInfoTypes`) | 0 | 0 | 1 | 0 |
| 7 CRT math and other implementation-defined | 46 math + 11 floor/ceil | 29 transcendental lines | **29 (rare, boundary cases)** | through game state | 0 | 28 (`sqrt`, `floor`, `ceil`); `rand()` and `<random>` unused |

### 9.3 Sites that reach decisions, RNG draws or saves

| Site | What | Cheapest fix that makes both builds agree | Changes x86 behaviour or saves? |
|---|---|---|---|
| `CvUnit.cpp:15441` / `:15542` (`DoSquadMovement`) | squad move missions sent in `CvUnit*` address order | iterate in unit-ID order (key by `GetID()`, or copy and `stable_sort` by ID) | x86 behaviour: order becomes ID order instead of address order (today not reproducible anyway). Saves: no |
| `CvUnitCombat.cpp:3105` | `pow(r, 4.0)` in `DoDamageMath` | `r2 = r*r; r2*r2` (plain SSE arithmetic, identical on both builds) | x86: damage changes where `msvcr90`'s `pow` differs from the product (rare); hence saves of later turns |
| other 28 transcendental lines (`stl-drift-sites.md` M1a-M1d) | `pow` with XML exponents, `log`, `log10`, `atan2` | measure first (9.4); then either link one deterministic implementation (for example a correctly rounded libm such as CORE-MATH, MIT licence) into **both** builds, or, x64-only, accept the residue | the shared-libm fix changes x86 results wherever `msvcr90` is not correctly rounded; the x64-only option leaves x86 untouched but keeps a rare divergence |
| `Lua/CvLuaGameInfo.cpp:104` | `GameInfoTypes` insertion order | push from a sorted copy | x86: `pairs(GameInfoTypes)` order changes for mods that iterate it; no save effect |
| `FStlContainerSerialization.h:185` | `stdext::hash_map` writer still in hash order, 0 callers | D18-style sorted write | none today (no caller) |

### 9.4 Proposed fix epic (for the MC; not opened)

"STL and CRT drift fixes", 14-24 agent-hours, on the fork `x64` branch, each fix cherry-pickable to the increment 0
branch like D18:

| Step | Hours |
|---|---|
| Squad move order by unit ID (`CvUnit.cpp:15542`) | 1-2 |
| `GameInfoTypes` from a sorted copy; sorted `hash_map` writer | 1 |
| CRT harness: a 32-bit test linked against `msvcr90` and an x64 one against the UCRT evaluate `pow`/`log`/`log10`/`atan2` over each call site's real argument domain (integer bases 0..10^6 with the XML exponents, combat ratios, vote counts) and report the mismatching inputs and whether any flips an integer result | 3-5 |
| Fix by the harness's answer: integer-exponent `pow` replaced by products; for the rest, one shared implementation in both builds (or documented acceptance) | 4-8 |
| Verification: headless x86 A/B (current vs fixed DLL, fixed seeds, turns 60 and 150, the issue 1008 procedure) to show what changed on x86; x64 A/A 100+ turns once the x64 host exists (I1-E); then x86-vs-x64 turn-for-turn compare, now worth trying given section 5 | 5-8 |

**Need the owner's decision** (they change the 32-bit DLL's behaviour, as D18 did for save bytes): the squad move order
(behaviour only), the `pow(r, 4.0)` rewrite and any shared math library (behaviour and, from the first differing
turn, save contents), and the `GameInfoTypes` order (Lua-visible only). The `hash_map` writer change has no effect
today. None of the fixes changes the save *format*.

### 9.5 Measured cause of the x86/x64 divergence (epic 1800, 2026-11-10)

Measured 2026-11-10 06:19-07:40 EST (background agent of the MC, epic 1800; `tracker/issues/1800-x86-x64-drift-cause.md`).
Builds: fork `x64` @ e9d0c4837 plus throwaway branches `throwaway/1800-*` (never pushed). Hosts: the 32-bit headless
host (c5h, as a tool, official and fork x86 DLLs) and `server/run_server.py`. Database: VP 5.4.8 cdrift
(`bd9b8aa1...`), `--no-mods`, `VP_MEMIMPORTS=0`. Specs: Tiny Continents 30017/40017, Standard Continents 1001/2002.
Tools: `tools/drift/` (README there). **Correction (2026-11-10, epic 1800): the inference of 9.1 and of
`server-host.md` 1/8 that CRT maths in combat damage (`CvUnitCombat.cpp:3105`) causes the divergence is wrong.**

**Cause [C source + L]: a stray `;` in `AttackKey::operator==` (`CvTacticalAI.h:1071`).** The comparison ends after
`iGarrisonId == rhs.iGarrisonId;`; the three following lines (`iPrevSelfDamage`, `iPrevUnitDamage`,
`iPrevCityDamage`) are dead code, while `AttackKeyHash` hashes all seven fields. The tactical simulation's attack cache
(`CAttackCache`, a `std::tr1::unordered_map`) therefore returns an entry stored for **another** previous-damage value
whenever the two keys land in the same bucket. Bucket placement depends on the hash value (the `size_t` mixing differs
between 32 and 64 bits) and on the library's bucket mapping (VC9 vs MSVC 14 tr1), so x86 and x64 get different hits.
It is a logic bug on both builds (stale simulated damage) and a cross-build nondeterminism; it is deterministic within
one build, which is why A/A runs agree. Not maths, not uninitialised memory, not heap addresses.

First diverging operation, **Tiny** [L] (debug builds with cache logging, identical game to release, see below):
turn 24, Persian unit 1033's simulated attack from plot index 407 on unit 1080 (plot 16,6).
`CAttackCache::findAttack(attacker 1033, plot 407, defender 1080, garrison -1, prevSelf 0, prevUnit 0, prevCity 0)`:
**x86 hit** (the entry stored one lookup earlier with prevUnit 54), **x64 miss** (computes attack 1080 vs defense 500).
Score of the candidate (unit 1033, attack, 407 -> 352): **902 on x86, 641 on x64**. The RNG draws, unit moves and
damage events stay identical until turn 33, where the AI picks a different attack on barbarian unit 1081 (damage
19 -> 57 by a melee attack on x86, 19 -> 43 by a ranged attack on x64); that is the turn-34 `MapUnitsState` row.
**Standard** [L]: turn 57, `findAttack(1307, plot 1384, defender 1218, prevUnit 0)` after three lookups with
prevUnit 29: x86 hit, x64 miss. Moves and damage stay identical until turn 100 (an extra combat on x64, units 2061 and
2341), giving the turn-101 `MapUnitsState` row. Same mechanism.

**Fix trial [L]** (`throwaway/1800-key`, one line: `;` -> `&&`): x86 and x64 then agree on **Tiny turns 0-50 and Standard
turns 0-200, all 15 `stats.db` tables identical** (`IsReligionOwner` and `MostEndangeredCity` excluded, see below). The fixed x86 DLL equals **today's x64** on Tiny 50 (identical) and differs from today's x86 DLL from turn 34
(the turn-34 HP row); on Standard the fixed x86 differs from today's x86 from turn 38 and the fixed x64 from today's
x64 from turn 38 as well (both builds hit stale entries today, in different places). So this fix changes today's
32-bit results; that is the user's decision (tracker 0077).

**Maths, measured [L]:** with every `pow`/`log`/`log10`/`atan2`/`exp`/`sin`/`cos`/`tan` call of the DLL wrapped and
logged bit-exactly (Tiny 0-36: 94,741 calls; Standard 0-102: 1,007,890 calls), the only results that differ between
msvcr90 (x86) and the UCRT (x64) are `pow(r, 4.0)` in `DoDamageMath`, 1 ulp, x86 lower: Tiny 2 inputs (turn 28:
`pow(0x3FF840EFC40EFC41, 4)` = `0x40151EA8F768BC52` x86 vs `...BC53` x64; turns 31/32/35: `pow(0x3FF63784A062B2E4, 4)` =
`0x400DBD726362F612` vs `...F613`), Standard 13 of 13,746 distinct inputs. **None changes an integer**: the damage of
every logged `DoDamageMath` call recomputed in both runtimes is equal (Tiny 77,933 calls, Standard turns 95-102 137,711),
and a sweep of all strength pairs 1.00-40.00 (7,610,851 pairs x 1,200 damage rolls) gives 7,522 pairs with a 1-ulp
`pow` difference and **0 damage flips** (a 1-ulp ratio change moves `iDamage * ratio` by about 1e-12). Harness table:
`stl-drift-sites.md` section 11. Other sites: 0 differing inputs on the captured data.

**Other measured facts [L]:**
- The fork's x86 build of e9d0c4837 (release, SHA-256 `61602ea7...`) plays Tiny 36 turns identically to the official
  5.4.8 DLL (`4e91c58a...`); the instrumented builds (release and debug) play identically to their uninstrumented
  builds (Tiny, all tables), so instrumentation does not move the divergence (turn 34 in every pair).
- x86 debug (fork, no LTO) = official release through Tiny turn 34; only the logging column
  `ReligionBeliefYields.IsReligionOwner` differs (the uninitialised pantheon holy-city coordinates, server-host.md 5).
- **Pantheon holy-city coordinates** (`CvReligionClasses.cpp:185`): initialising them to -1 (`throwaway/1800-holy`)
  changes nothing but that logging column on x86 (Tiny 50 identical to today) and does **not** remove the x86/x64
  divergence (still turn 34). Clang's `-ftrivial-auto-var-init=zero` on both builds (all uninitialised stack
  variables zero) likewise changes only that column and leaves the divergence at turn 34.
- `compare_stats.py` at HEAD reports `MilitarySummary.MostEndangeredCity` different from turn 1 (x86 writes
  `Rio_de_Janeiro`, the x64 server's key translates to `Rio de Janeiro`): a text artefact, not a game difference;
  the comparisons here exclude that column.
- x64 with the amd64 `msvcr90.dll` from WinSxS (9.0.30729.9635) would be **worse**: it differs from the x86
  msvcr90 on 80 of 2,093 Tiny inputs (UCRT x64: 2). The x86 UCRT differs from the x64 UCRT on the same 2 inputs.
- The x64 UCRT has real `powf`/`log10f`/`logf`; the VC9 x86 headers implement the float overloads as
  `(float)f((double)x)`. On the captured inputs (the `log10(float)` at `CvFlavorManager.cpp:350`, `log(float)` at
  `CvGame.cpp:5740/5749`) the results agree.

### 9.6 Fix candidates (epic 1800, 2026-11-10) [L]

"Agree" = all 15 `stats.db` tables identical between the x86 host and the x64 server (`IsReligionOwner`,
`MostEndangeredCity` excluded). "Today" = official 5.4.8 x86 DLL.

| Candidate | x86 vs x64 | Fixed x86 vs today's x86 | Notes |
|---|---|---|---|
| key: `AttackKey::operator==` compares all 7 fields | **agree: Tiny 0-50 and Standard 0-200**, all 15 tables | Tiny: differs from turn 34 (equals today's x64); Standard: differs from turn 38 (`MapUnitsState`, a Venetian Slinger 28 vs 42 HP), and the fixed x64 also differs from today's x64 from turn 38 | the cause; one character; debug builds |
| a: `pow(r, 4.0)` -> `r2 = r*r; r2*r2` | not run with the key bug present (it cannot remove it) | product differs from msvcr90 `pow` on 51 % of combat inputs (up to 2 ulp); damage flips 851 (stronger side) and 171 (weaker side) of 9.1e9 pair x roll cases (about 1e-7) | changes today's x86 rarely; not needed for agreement |
| b: vpmath (musl 1.2.5 pow/log/log10/atan2/exp) in both builds | musl x86 = musl x64 on all captured inputs and the sweep (0 differences) | differs from msvcr90 on 12 `pow` + 1 `log` of 13,762 Standard inputs and 8,335 of 7.6 M sweep pairs, 1 ulp, 0 damage flips | removes the residual maths difference; x86 binary built and linked (`650f1181...`), game not run |
| c1: x64 uses the amd64 msvcr90 | worse (80/2,093 inputs differ) | none | rejected by the harness |
| c2: x86 against the x86 UCRT | same 2 inputs differ as today | 2 inputs differ | no gain |
| c3: `-ftrivial-auto-var-init=zero` | still diverges at Tiny turn 34 | only the `IsReligionOwner` column | uninitialised stack is not the cause |
| holy: initialise pantheon holy-city X/Y | still diverges at Tiny turn 34 | only the `IsReligionOwner` column | logging-only effect in these games |

### 9.7 The turn-354 difference on the huge save: a server net-loopback gap, not a DLL build difference (epic 2200, 2026-11-10)

Measured 2026-11-10 09:20-09:59 EST (background agent of the MC, `tracker/issues/2200-turn-354-difference.md`).
Input: copy `E2200-HUGE_0350.Civ5Save` of the owner's `VP8P-HUGE_0350` (SHA-256 `c499d3c6...5188c3`), the epic 2000 setup
(`server-host.md` 10: pinned VP 5.4.8 database, the save's two mods for the 32-bit host, toolkit game folder).
DLL sources: `throwaway/2000-fixes` (`x64` e9d0c4837 + 040b063ea + 27176dbfe) and `throwaway/2000-fixkey` (+ the
`AttackKey` fix); instrumented copy `throwaway/2200-instr` (fork, local, never pushed). Tools: `tools/drift/run_huge.py`,
`instrument_huge.py` (stages `army`, `targets`, `supply`, `net`), evidence `tools/drift/evidence/2200/`.

- **Reproduced [L]**: epic 2000's release DLLs, 350 -> 355: first difference turn 354, Spain (player 2)
  `RecNavyOffensive` -27 (x86) vs 0 (x64), plus Aksum `Danger` 115/114 and a German "Won Battle" yield only on x86.
  The instrumented debug builds play identically to these release builds (all tables, 350-355, both architectures).
- **Walk back [L]** (bit-exact logs, `evidence/2200/i1-*`, `i3-*`): Spain's naval weights, offence weights, flavours
  and target lists are equal on both sides in turn 354; what differs is `iMaxPossibleUnits` =
  `CvEconomicAI::GetSoftSupplyCap()`: **10 (x86) vs 39 (x64)**, so after subtracting 18 explorers -8 vs 21 and the
  final `m_iRecNavalUnits` -27 vs 0 (`CvMilitaryAI.cpp:1822` and the adjustment below it; the int64-to-float
  quotient is computed identically: `QN` = `BF06463D` vs `3FB03C30` from different numerators). The soft cap
  differs because Spain's non-unit costs differ: `GetExpensePerTurnFromVassalTaxes` = **132 (x86) vs 0 (x64)**
  (non-unit costs 520 vs 388, revenue 538 vs 531), and Egypt's revenue 2541 vs 2408. The tax comes from turn 353:
  Egypt (team 0, Spain's master) decides in `CvDiplomacyAI` (the vassal-tax block around `CvDiplomacyAI.cpp:26869-27120`)
  to set Spain's tax to 25 % and calls `CvTeam::DoApplyVassalTax` -> `NetMessageExt::Send::DoApplyVassalTax` ->
  `gDLL->sendMoveGreatWorks(0, 0, 2, 25, -1, -1, 1004)`. Log on both sides: `T353 NET send MGW 0 0 2 25 -1 -1 1004`;
  only the 32-bit host then logs `T353 NET recv MGW ...` (the DLL's `ResponseMoveGreatWorks` ->
  `NetMessageExt::Process`, case 1004 -> `Response::DoApplyVassalTax`). **The server never delivers it.**
- **Cause [C source + L]: host difference, not a DLL build difference.** VP's `NetMessageExt` (`CvDllNetMessageExt.cpp`)
  multiplexes its own messages through the engine's `sendMoveGreatWorks`/`ResponseMoveGreatWorks` pair (last argument
  999-1006: event choice, city event choice, minor bully annex, faith-purchase flag, **vassal tax (1004)**,
  **liberate vassal (1005)**, deal renewal id). The server's `SrvEngineUtility` (`server/src/srv_ifaces.cpp`) has no
  override for `sendMoveGreatWorks`, so the generated stub swallows it; its loopback only queues PlayerOption,
  PledgeMinorProtection, MinorCivBullyGold/Unit, GiftTileImprovement and Buyout (`research/sources/civ5-headless/engine-contract.md`
  6.4 lists only the three exercised pairs and says the full list is in `ICvDLLUtility.h`). The 32-bit host loops it
  back, as the real game must: VP's AI vassal taxes, liberation and bully-annex only take effect through the response
  (the `Send` functions change no state themselves), so a game without the loopback would never apply them [C]; that
  the EXE delivers it in single player is [I] from that design (not traced in the EXE). AI callers of these sends:
  `CvDiplomacyAI.cpp:26869/26920/26928/27081/27120` (tax), `:35348` (liberate), `:36737` (bully annex via
  `CvGame::DoMinorBullyAnnex`); event choices by the AI pass `bSendMsg=false` and do not use the net path. New games to
  Standard turn 200 have no vassals, which is why epic 1800 never met it. Not in the `stl-drift-sites.md` inventory
  (it is not a library or build difference).
- **No other difference before it [L]**: the only bit difference in the logged inputs up to then is a 1-ulp `pow` in
  `CvTreasury::CalculateUnitCost` (`CvTreasury.cpp:539`, Egypt turn 353, 85 units: `pow(0x4071BE665EC00000,
  0x3FF245A1C0000000)` = `0x4083C94B8D5588F4` msvcr90 vs `...F3` UCRT), which rounds to the same unit cost (569) on
  both. The x86 host also makes about 60 extra `CalculateUnitCost(-1)` calls per turn (its checkpoint Lua reads
  `CalculateGoldRate` etc.); they change nothing measured here.
- **Trial fix [L]** (server only; `tools/drift/server-2200-sendMoveGreatWorks.patch`: queue `sendMoveGreatWorks` and
  deliver it through `ICvNetMessageHandler3::ResponseMoveGreatWorks`, like the other loopback pairs; release
  `server.exe` `7cb0585f...a36ea`, built in a scratch copy, `server/` unchanged). Huge save 350 -> 400, release DLLs:
  **`fixkey` pair (key fix + this server change): all 14 stats tables identical, turns 350-400** (682,731 rows each;
  3 `MoveGreatWorks` messages delivered in 50 turns). `fixes` pair (no key fix): Spain now agrees, first difference
  still turn 354 (Aksum `Danger` 115/114, the German "Won Battle"), the `AttackKey` mechanism of 9.5. So on this save
  there is **no second DLL build difference** up to turn 400 once the key bug is fixed.
- **Effect on today's 32-bit game**: none from this fix (the DLL is unchanged; it changes the server only). For the
  record, the key fix on this save changes the x86 game from turn 354 (x86 `fixes` vs x86 `fixkey`, both in the
  32-bit host: first difference turn 354, the German combat) (`evidence/2200/x86-fixes-vs-fixkey-350-400.compare.txt`).
- **Not verified**: the EXE's own delivery of `sendMoveGreatWorks` in single player (inferred, not traced); the other
  `send*` without a server override (e.g. `sendSwapGreatWorks`, `sendSetSwappableGreatWork`, `sendNetDealAccepted`)
  are used only for humans in the code read here, not checked exhaustively (**done 2026-11-10 by issue 9007:
  `server-host.md` section 11 audits every send; the trial patch is now server code and deleted; AI-reachable sends
  besides the NetMessageExt channel: `sendDoTask` (capital relocation) and `sendLaunch`, both looped back now**); the round-trip post-load differences of
  `server-host.md` 10 (player options, the per-player int = turn) had no measured effect on 350-400 but were not
  isolated; turns past 400.

### 9.8 The pinned builds: D25 fixes shipped, a replay-count width defect, x86 = x64 (epic 2400, 2026-11-10)

Team-2400 coordinator, 2026-11-10 12:04-16:15 EST (`tracker/issues/2400-*.md`).

- **Branches [C]**: fork `i0-ship` = `i0-bugfixes` 27176dbfe + 50a389682 (the `AttackKey::operator==` fix, upstream's
  2bc6d34a1), 1512a94b4 (squad moves in unit-ID order), 5810b3c71 (`GameInfoTypes` from a sorted vector), e6f904ad6
  (below). `x64` 29f15ac38 = merge of `i0-ship` into e9d0c4837, no conflicts; width pins and D18 untouched. Pinned in
  `deps/i0.lock` and `deps/dll.lock`; both pushed to origin.
- **Pins moved 2026-11-10 21:42 EST (one-off 9013) [C]**: `i0-ship` ccfd6dd4d = e6f904ad6 + 40ce81aee (e9d0c4837, the
  D18 sorted writes, cherry-picked unchanged: decision D32) + ccfd6dd4d (the two statistics database pragmas in
  `SqliteLogger::EnsureOpen`: decision D35); `x64` c43ea1b9f = two further merges of `i0-ship` (e716240a7, which changes
  no file, then c43ea1b9f, which adds only the pragmas), both with an empty `--remerge-diff`. Builds
  `<pins_dir>/dlls/i0ship-ccfd6dd4` and `x64-c43ea1b9`; the three-part save test passes between them with no
  order exception (`../06-tooling-and-workflows/d10-save-identity-test.md` section 4c). (Replaced as the current pins:
  e6f904ad6 / 29f15ac38; their builds are kept.)
- **Pins moved 2026-11-11 14:04-14:10 EST (one-off 9029) [C]**: upstream fix batch 1 (`upstream-fix-batch-1.md`):
  fork `i0-ship` fast-forwarded ccfd6dd4d -> bbfacca1a (17 upstream cherry-picks), `x64` c43ea1b9f -> 76992d235 (the
  merge of bbfacca1a, empty `--remerge-diff`), fork `master` -> bbfacca1a (D46). Builds `i0ship-bbfacca1`,
  `i0ship-bbfacca1-ia`, `x64-76992d23` (with `proxy/bin`); hashes in `deps/i0.lock` and `deps/dll.lock`. Narrowing gate
  794 (baseline 798, 0 new). (Replaced as the current pins: ccfd6dd4d / c43ea1b9f; their builds are kept.)
- **Defect found and fixed [C, L]**: growth commit 4a8f99c79 (epic 1300) gave `CvReplayTurnData` its own stream
  operators (`CvPlayer.cpp:42575-42611` on i0-ship) that wrote and read the entry count as `size_t`: 4 bytes on x86 (the
  same bytes as the `std::map` it replaced), 8 on x64, outside the port's pinned-width helpers of section 4.1. The x64
  build of the first merge (3cfa98347, never pushed) could not load an x86 save (worker A, issue 2402,
  `tools/d10/evidence/2402/unfixed-x64-loads-x86-tiny.server.log`). Fixed by a new commit e6f904ad6 (count as
  `unsigned int` in both operators; MC decision, 4a8f99c79 not amended). x86 save bytes unchanged: load-and-resave of
  Tiny t100, Standard t200 and the huge t350 with the build before and after it identical except the version string
  [L] (`tools/d10/evidence/2404/roundtrip-summary-*.json`).
- **Search for the same class [C]**: every line added by the eleven commits since Release-5.4.8 that streams into a
  save (`operator<<`/`>>`, `visitor(...)`, `Serialize`, `Read`/`Write`) was listed from `git diff 7307aefdf e6f904ad6`:
  the only new stream code is `CvReplayTurnData`'s pair; the inline danger-list vector of ffecf47b9/c20761449
  (`CvDangerPlots.h`, its own `unsigned int` size type) is not serialized (`CvDangerPlots::Serialize` writes only
  `m_ePlayer`, `m_knownUnits`, `m_vanishedUnits`); a4678f41b and b9311e2ee touch no save code; the crash and D25 commits
  add no stream code. The x64 build's narrowing gate stays at 798 = baseline. Same-source x86 and x64 resaves are
  whole-file identical on three inputs (`server-host.md` 12), which a remaining width difference would break.
- **x86 vs x64 with the pinned builds [L]**: the three-part D24 test passes on Tiny 0-100, Standard 0-200 and the
  huge save 350-400 (`../06-tooling-and-workflows/d10-save-identity-test.md` section 4). This replaces "throwaway
  builds" in 9.6-9.7: the agreement now holds for the shipped sources.
- **Change against stock 5.4.8 [L]** (tracker 2404, `tools/d10/evidence/2404/`): first differing stats turn Tiny 34
  (`MapUnitsState`), Standard 38 (`MapUnitsState`, `HandicapYields`), huge save 354 (`MilitarySummary` Danger, then
  `HandicapYields`, `InstantYields`): the cost D25 accepted. Two runs and a `--heap-perturb` run of the x86 build agree
  on every table (Tiny 100, Standard 200).
- **Squad order [L]**: reached only from the Squads UI mod's Lua (`unit:DoSquadMovement`), never by the AI [C]. In the
  x64 server (which loops `sendPushMission` back) a probe moved an 8-unit squad: without 1512a94b4 three runs gave
  three move orders, with it unit-ID order each time (`tools/d10/evidence/2404/squad-order-x64.txt`). Not observable in
  the 32-bit host, whose `sendPushMission` is a generated stub.

### 9.9 The live x64-vs-i0-ship save difference is D18's sorted exploration sets (issue 2707, 2026-11-10)

Worker 2707 of team-2700, 2026-11-10 18:05-18:30 EST, offline on copies. Inputs: epic 2700's live c5h-a saves of the
same steps (Tiny turn-8 save, 10 AI turns, saved at turn 18): `T1-on-x64` and `T2-on-x64` through the proxy (fork `x64`
29f15ac38, `deps/dll.lock`) and `T1-B-x86` through the off switch (32-bit `i0-ship` e6f904ad6 + instance-aware
instruments 440878983). Tool: `server/tools/explore_sets.py` (new; bodies from `civ5save.py unpack`).

- **Attribution [C dump + source]**: the four differing body regions after the embedded save database (A offsets
  3836464 +449, 4040776 +1813, 4249754 +3869, 4467224 +2677) are exactly the `CvEconomicAI` exploration-plot sets
  `m_vPlotsToExploreLand/Coast/Ocean` (`CvExplorationPlotSet::m_aValues`, `tr1::unordered_map<int, ExploreEntry>`,
  record = plot index + `value[3]`, 16 bytes; `CvEconomicAI::Serialize`, `CvEconomicAI.cpp:353-355` at 29f15ac38,
  inside `CvPlayer::Serialize` `CvPlayer.cpp:45840`) of player slots 0-3 (the four live majors): slot 0 Land 23 +
  Coast 5 records, slot 1 64 + 40 + 9, slot 2 113 + 88 + 41, slot 3 86 + 65 + 16 (Ocean 0 for slot 0). In each set
  the count is equal and the records are the same multiset; the x64 save has them in ascending plot index, the x86
  save in hash-table order. 2,204 differing bytes, all inside these 11 sets; outside them only the version string
  (+12 bytes) and the two bytes that also differ x64 vs x64 (A offsets 84 and 12771609). x64 vs x64 (T1/T2): 0 bytes
  differ inside the sets. The slot numbers follow from save order: unused slots start with `m_eID`, personality,
  startX/startY = 0x80000001, and their triple lies 52,435 bytes later (slot 4 starts at A 4648520, its triple at
  4700955), so the four non-empty triples before it are slots 0-3 [C dump].
- **Cause [C source]**: D18 (fork e9d0c4837, sorted writes of `tr1::unordered_map/set` in
  `FStlContainerSerialization.h`, section 4.1) is on branch `x64` only; `git merge-base --is-ancestor e9d0c4837
  e6f904ad6` is false, so `i0-ship` still writes these containers in VC9 tr1 iteration order. Not floating point, CRT
  maths, pointers, padding, `size_t` width or RNG: the game state is identical (all 377 statistics keys equal, and the
  records equal as multisets); only the serialisation order differs. Consistent with this, the common input save
  `input-t8` (turn 8, an older 32-bit DLL without D18) also has every non-empty set unsorted [C dump]. The reader inserts each record, so either DLL
  loads either save [C source, section 4.1].
- **Not differing here [I]**: `CvCity::m_miInstantYieldsTotal` and `CvGame::m_AdvisorMessagesViewed`, D18's other two
  containers, gave no differing byte in these saves (small or empty containers whose hash order happens to equal key
  order, or empty; not checked element by element).
- **Consequence**: a byte compare of x64 against the current 32-bit off switch must either normalise these sets (sort
  the records of each set before comparing; `explore_sets.py A B` reports per set whether it is a pure re-ordering
  and lists every differing byte outside the sets, here 84 and 12771609, so it exits 1 on these saves) or ship D18 on `i0-ship` too (it changes 32-bit save bytes in these containers only, as
  D18 already accepted for `x64`). Decision for the coordinator; not taken here. My attempt to reproduce the x86 order
  from the VC9 hash (`k ^ 0xdeadbeef`, with and without the `ldiv` randomiser, low-bit buckets) did not match: the
  exact VC9 order is not modelled [I], and is not needed for the attribution.

### 9.10 Bug 0116 is the 32-bit host's per-turn Lua, not the build: a getter that writes state (one-off 9044, 2026-11-13)

Measured 2026-11-13 04:40-05:10 EST on the quiet testbed (`oldpc`, `laptop`), pins x64 76992d235 (DLL `fa75f483...`,
server.exe `3a35330a...`) and i0-ship bbfacca1a (`f7c64840...`, c5h), AI logging off, statistics logging on, pinned VP
database, `--no-mods`; inputs 9037's x86 repro saves (`D:/c5-64bit-work/9037/repro-0116/`: S3 small Archipelago 4141/5151
turn 328, `b8f1c1ee...`; S1 tiny Oval 2121/3131 turn 200, `a6b51c2b...`). Comparison `tools/d10/statscmp.py`, every table.
Evidence `D:/c5-64bit-work/9044/<job>/out/`; tracker 9044 / 0116.

- **The builds play the same game; the hosts differ [L].** c5h runs its checkpoint Lua after every turn
  (`--checkpoint-every`, default 1; civ5-headless `python/civ5headless/run.py` CHECKPOINT with the STRENGTH metrics:
  for each living major `GetScore`, `GetMilitaryMight`, `CalculateGoldRate`, `GetScience`, `GetExcessHappiness`,
  `GetTourism`, ...). The server runs no per-turn Lua (its `lua_probe` files run once, at arming). The 32-bit DLL in c5h
  with `--checkpoint-every=1000` plays **exactly the x64 game**: S3 identical to the x64 continuation in all 15 tables
  (569,688 rows, Science 406; jobs `20261113-044016-471e`), S1 identical (`-044027-c047`); with checkpoints every turn
  it differs from turn 334 (S3) and 254 (S1), as 9037 found. So every 0116 case seen so far is c5h's own Lua changing
  the 32-bit game, and the "x64 vs x86" label was a host difference.
- **Which getter [L]** (S3, c5h through `tools/testbed/jobs/c5h_ckpt.py`, which keeps only named STRENGTH fields;
  `longgames.py --run-ckpt`; rounds R1/R2, jobs `20261113-044819-a15c`, `-044829-c551`, `-044838-4472`, `-044851-1ac3`,
  `-045648-950a`, `-045658-e23a`, `-045709-e4d0`, `-045717-66be`): the base CHECKPOINT alone (no STRENGTH, no score),
  `GetScore`, happiness + tourism, influence + votes, culture + faith, gold + wonders + spaceship parts each leave the game
  equal to x64; `might + goldRate`, the group with `science`, and `science` alone each reproduce the x86 game (turn 334);
  all fields reproduce it exactly (the launcher's control).
- **Cause [C source]:** `CvTreasury::CalculateTotalCosts()` stored `m_iExpensePerTurnUnitMaintenance =
  CalculateUnitCost()`, and `GetExpensePerTurnUnitMaintenance()` returned that stored value. `CalculateTotalCosts` runs
  inside every gold-rate read: `CalculateGoldRate` -> `CalculateBaseNetGoldTimes100`, and `GetScience` ->
  `GetScienceFromBudgetDeficitTimes100` -> `calculateGoldRateTimes100`. So a Lua read stored the unit maintenance of
  that moment, and the game's own readers later in the turn saw it: `CvUnitProductionAI.cpp:230` (gold spent on units ->
  which unit class to build), `CvTreasury.cpp:995` (a master's vassal maintenance from the vassal's unit maintenance ->
  gold totals -> `GetSoftSupplyCap` -> recommended unit counts, the S3 first value `RecNavyOffensive`),
  `CvEconomicAI.cpp:1501`, the replay data `REPLAYDATASET_UNITMAINTENANCE`. Without the read the stored value is the
  one of the last `CalculateTotalCosts` the game itself ran. Same mechanism on both builds: the 32-bit DLL under the real
  EXE has it too, wherever UI Lua reads the gold rate (top panel, EUI's civ ribbon). The civ5-headless author found and
  fixed the same defect in their own fork (`docs/history/host-ledger.md`, "Second frame-count dependence", commit
  3241633ea in hh-host, not in our fork). Upstream VP master 16596561f (2026-11-11, `git fetch upstream`) still has
  it (`CvTreasury.h:84`, `CvTreasury.cpp:593`) [C].
- **Fix (fork branches, 2026-11-13):** `x64-fix-0116` 824b84d5f on 76992d235 and `i0-ship-fix-0116` c4f3c7b34 on
  bbfacca1a (cherry-pick `-x`): `GetExpensePerTurnUnitMaintenance()` computes `CalculateUnitCost()` (made `const`) on
  each call; `CalculateTotalCosts()` no longer writes the member; the saved member is set once per turn in `DoGold()` so
  the save does not depend on who read the gold rate. Changes game results on both builds against today's pins (the AI
  reads current maintenance instead of the last stored one). Builds (2026-11-13 05:09-05:19, workstation, under the
  quiet-machine lock, `D:/c5-64bit-work/9044/build.sh`): x64 `D:/c5-64bit-work/9044/bin-x64-fix` (DLL `016792bc...adc2`,
  server.exe unchanged `3a35330a...`; narrowing gate 794, baseline 798, 0 new), 32-bit
  `D:/c5-64bit-work/9044/i0-fix/CvGameCore_Expansion2.dll` (`dee91cc3...ba79`, `build_vp_clang.py --config release`).
- **Other read-filled caches c5h's checkpoint touches, measured harmless here [L]:** `GetMilitaryMight` (lazy once-per-turn
  `updateMightStatistics`, `CvPlayer.cpp:32574`) alone leaves S1 and S3 equal to x64; the great-work yield cache
  (`CvCity::m_GwYieldCache`, reached by culture/faith/science yields) did not show (culture + faith equal to x64). Not
  proven harmless in other games; the nine-game verification below is the wider check.
- **Fix verified on the repro saves [L]** (job `20261113-051948-5aec`, `oldpc`, 05:20-05:40): fixed x64, fixed 32-bit in c5h
  with its checkpoint Lua every turn, and fixed 32-bit with `--checkpoint-every=1000` play the same game: S3 all 15 tables
  identical in both pairs (594,339 rows, Science 408 on all three), S1 identical (371,578 rows, Cultural 331). The reads
  no longer change the game, and x64 = x86 holds with c5h's default checkpoint.
- **Bug 0104 (AI logging on, std Continents 1001/2002, x64 Cultural 310 vs x86 312 from turn 257) is the same host
  effect [L]** (job `20261113-054910-97ab`, `oldpc`, pins, logging on): the pinned 32-bit DLL in c5h with
  `--checkpoint-every=1000` equals the pinned x64 game in all tables but `IsReligionOwner` (1,024,513 rows, Cultural 310),
  and differs from the x86 game with checkpoints from turn 257. The bugfix team's candidate (CRT `pow` in
  `CalculateUnitCost`, 1 ulp on 2,130 of 2,020,101 inputs) is not what moved this game. With the fix (both builds, logging on,
  c5h's default checkpoint) the game is identical on x64 and x86 to the end (Cultural 344, 1,304,765 rows but
  `IsReligionOwner`; job `20261113-054921-e34f`, `laptop`).
- **Second cause, the World Congress votes cache (huge Pangaea 1717/2828) [L, C source].** With the first fix, 8 of the
  9 bug-0116 games are identical on x64 and x86 to the end (table below); huge Pangaea is not (x64 Diplomatic 359, x86
  348; `WorldStateLog` opinion counts from turn 270, `MilitarySummary.Danger` 112/84 at 288, the same values as on the
  pins). It is c5h's Lua again: the fixed 32-bit DLL with `--checkpoint-every=1000` equals the fixed x64 game through
  turn 299 (job `20261113-063632-ca67`). From the x86 save of turn 262 all continuations agree to 300 (`-063646-4dae`):
  the state is not saved. Bisection on the direct game to turn 300 (fixed 32-bit DLL, c5h keeping only the named
  STRENGTH fields; jobs `20261113-071811-9b50` might, `-071820-7dc9` all but might, `-074330-4048` score, `-074339-75b0`
  happiness + tourism + influence + votes, `-074416-6190` culture + faith, `-075800-fa6d` votes): **votes alone**
  (`league:CalculateStartingVotesForMember(owner)`) reproduces the x86 game exactly; might, score, culture + faith leave
  it equal to x64. Mechanism: `CvLeague::CalculateStartingVotesForMember` (`CvVotingClasses.cpp:4046`) caches its result
  per member for the current `CvGame::getTurnSlice()` (members `m_startingVotesCache*`, not saved). The turn slice
  advances near the end of `CvGame::update()` (`changeTurnSlice(1)`, `CvGame.cpp:1712`), so a call made between two
  updates, as c5h's checkpoint is, runs in the slice of the next update: it stores the vote count of that moment, and the
  next update's own calls (diplomacy opinions and approaches, world congress AI) read it although the game state has
  moved on (votes depend on city-state alliances, diplomats, religion, ideology). Every cache keyed on the turn slice has
  this exposure to calls between updates: `CvPlayerCulture::GetInfluenceTrend` (`CvCultureClasses.cpp:3858`),
  `CvDealAI` item values (`CvDealAI.cpp:924`), `CvPlayerTrade::UpdateTradeStats` (`CvTradeClasses.cpp:4657`; route changes
  reset it), the A* last-path cache (`CvAStar.cpp:439`) [C source; not shown to change a game].
  Fix (fork, second commit, 2026-11-13): `x64-fix-0116` abe1f726b, `i0-ship-fix-0116` d0fb1fbe3: `CvGame::GetCacheStamp()`,
  a counter (not saved) that changes where the turn slice changes and also at the start of every `CvGame::update()`; the
  votes cache keys on it, so a value cached between updates is not used by the next update. Within an update the cache
  lives exactly as before. X4 (gold rate, science and the rest, `-074428-8f75`) and X6 (happiness, tourism, influence, `-075810-8f02`)
  also equal x64 with the first fix, so votes is the only remaining reader in this game.

Nine bug-0116 games with the first fix (fork 824b84d5f / c4f3c7b34; x64 `016792bc...`, 32-bit `dee91cc3...` in c5h with its
default per-turn checkpoint; AI logging off; to the end) [L], jobs `oldpc` `20261113-052003-6443`, `-052012-a67c`,
`laptop` `-052021-d0bb`, `-052036-4e45`:

| Game | Pins x64 / x86 (9037) | Fix 1 x64 / x86 | x64 = x86 with fix 1 (every table but `IsReligionOwner`, rows compared) |
|---|---|---|---|
| tiny Oval 2121/3131 | Domination 298 / Cultural 319 | Cultural 347 / 347 | identical (602,744) |
| small Archipelago 4141/5151 | Science 410 / 405 | Science 412 / 412 | identical (1,572,858) |
| std Continents 1212/3434 MARATHON | Domination 931 / 926 | Domination 970 / 970 | identical (3,210,295) |
| std Fractal 5757/6868 | Science 418 / 410 | Science 414 / 414 | identical (2,255,970) |
| huge Pangaea 1717/2828 | Diplomatic 359 / 348 | Diplomatic 359 / 348 | **differs from 270** (votes cache, above) |
| large SmallContinents 2627/2829 | Science 377 / Diplomatic 357 | Diplomatic 357 / 357 | identical (2,041,019) |
| large Oval 4243/4445 | Cultural 357 / Science 383 | Science 380 / 380 | identical (2,406,000) |
| std Archipelago 5859/6061 | Science 400 / 418 | Science 418 / 418 | identical (2,474,168) |
| std Pangaea 6263/6465 EPIC | Cultural 447 / 452 | Cultural 476 / 476 | identical (1,880,475) |

With fix 1 + 2 (fork abe1f726b / d0fb1fbe3; x64 `d887c4f4...`, 32-bit `3c565e51...`) [L]: huge Pangaea **identical** but
`IsReligionOwner` (Diplomatic 359 on both, 2,970,630 rows; `laptop` jobs `20261113-084636-4372` / `-084653-712a`); S3 and S1
saves IDENTICAL, tiny Oval and small Archipelago identical (602,744 / 1,572,858 rows, the same outcomes as fix 1), the 0104
game with AI logging on identical (Cultural 344; `oldpc` `-084704-4349`, `-084719-8ff8`).
The other six games, fixed x86 (fix 1 + 2) in c5h against the fix-1 x64 runs above (the second fix changes only calls made
between updates, which the server does not make; on tiny Oval, small Archipelago and huge Pangaea the x64 games of fix 1
and fix 1 + 2 end alike with equal row counts): large SmallContinents identical (Diplomatic 357, 2,041,019 rows; `oldpc`
`20261113-092608-621e`), std Pangaea EPIC identical (Cultural 476, 1,880,475; `-092619-f114`), large Oval identical (Science 380,
2,406,000), std Fractal identical (Science 414, 2,255,970; `laptop` `-092628-c7f0`), marathon Continents identical
(Domination 970, 3,210,295), std Archipelago identical (Science 418, 2,474,168; `-092640-66e5`). **All nine bug-0116 games
and the 0104 game: x64 = x86 to the end with fix 1 + 2** (every table but `IsReligionOwner`).

Not verified / open (2026-11-13):
- The other turn-slice caches (influence trend, deal item values, trade stats of the UI thread, the A* last path) keep the
  turn slice as key; none changed a game seen here, but each is open to a call between updates in the same way. The real
  game's UI Lua calls between updates too, so the 32-bit game under the EXE depends on what the player has open; that is
  upstream behaviour and the same through the proxy on x64.
- Both fixes change today's results on both builds (outcomes in the table above differ from the pins in 7 of 9 games).
  Not run: the 30 games that were already identical on the pins, on the fixed builds.
- The statistics logger's MilitarySummary row fills the lazy unit-supply cache (bug 0103's side note): it runs at the
  same point on both hosts, so it is not a cross-host difference; not examined further.
- No upstream report opened (upstream master 16596561f still has both defects).

## 10. Database layer over SQLite (epic 1400, 2026-11-10)

Code: `tools/x64-dll/cvgamedatabase/` (its `README.md` has every member's behaviour and the decompile notes). Evidence:
`tools/x64-dll/evidence/2026-11-10-1403/` (A/B), `2026-11-10-1404/` (smoke host with the real database). Issues
1401-1404. Fork `x64` @ e9d0c4837, clang-cl 19.1.7, MSVC 14.43, `/MD`.

### 10.1 What replaced the 46 stubs

- **`CvGameDatabase_x64.dll` + import library, not a static library** (coordinator decision): the server opens the
  `Database::Connection` and hands it to the DLL with `SetGameDatabase`, so the host and the DLL must share one copy of
  the wrapper and one SQLite (two SQLite copies on one `sqlite3*` share no mutexes or allocator; same reasoning as
  `lua51_x64.dll`, 8.1). Built by `cvgamedatabase/build_db.py --arch x64|x86` into the stubs folder, so `build.py
  --stubs` links it; the `closedlib-stubs/CvGameDatabase/` group is gone.
- Every member of `Database.h` and `DatabaseResults.h` is implemented (104 exports; the DLL imports 46 of them) [C link:
  0 undefined]. Not implemented: `DatabaseIO.h`, `XmlSerializer.h`, `ResultsCache.h`, `LuaExposures.h` (31 closed
  exports; the DLL uses none). Behaviour taken from a decompile of a copy of the closed DLL [C] and checked by the A/B [L].
- Notable closed-library behaviours, reproduced on purpose [C]: `TotalChanges()` returns `sqlite3_changes()`;
  `Count()` reuses its remembered value whenever the last change count is 0, whatever `bCache` says; a `SingleResult`
  (and any `Results` with `SingleQuery(true)`) is stepped to its first row inside `Execute`/`Select*` (`SetCommand`), so
  callers must not `Step()` again (the smoke host did, and read nothing, 2026-11-10); `Release` forgets the columns;
  `Close` finalizes statements of live `Results`; `ValidateFKConstraints` ignores its argument; `Open` does not close an
  earlier handle.

### 10.2 SQLite version: 3.7.17, the closed library's own build options (Minor decision, epic 1400)

Version 3.7.17 (source id `118a3b35...7668`), the version inside the closed DLL [C strings]. The closed DLL's
compile-option table is `THREADSAFE=1 TEMP_STORE=2 OMIT_TCL_VARIABLE OMIT_PROGRESS_CALLBACK OMIT_DEPRECATED
OMIT_AUTOINIT OMIT_AUTHORIZATION` [C]; the replacement uses exactly these (so `sqlite3_initialize()` is called
explicitly). Why not newer: same query planner, collation, type affinity and `sqlite_stat1` contents with no work, and
the A/B shows identical rows; a newer SQLite would need its own A/B and gains nothing the DLL uses. Archive locked in
`deps/third-party.lock` (URL, size, SHA-256 `022ef41b...ef30`; sqlite.org published no SHA-256 for 2013 files).
Not reproduced: the closed library's own allocator, its 5000 x 1168 B static page cache, 7008 B scratch buffer and
lookaside-off (`sqlite3_config` 4, 7, 6, 13) [C]; only `CalculateMemoryStats` numbers show it [L].

### 10.3 x86 A/B against the closed library [L]

`cvgamedatabase/ab/run_ab.py --out DIR` (one command, non-zero exit on an unexplained difference): one 32-bit query
program, layout-agnostic (objects built by the DLLs' exported constructors in 4 KB buffers, because the closed DLL uses
the VC9 STL), linked once against the fork's `CvGameDatabaseWin32.lib` + a copy of `CvGameDatabaseWin32Final Release.dll`
(SHA-256 `f9563739...6949563`, from the headless toolkit's game copy) and once against `CvGameDatabase_x86.lib`; a fresh
copy of the VP 5.4.8 database per run (`vp548-cdrift-Civ5DebugDatabase.db`, SHA-256 `bd9b8aa1...addfadd6f`, copy of
civ5-headless `pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db`). The script covers every call the DLL makes during
boot, every overload, error paths, writes in transactions, `Analyze` + `sqlite_stat1`, over all 832 tables.

| Comparison (release and debug, 2026-11-10 04:30) | Lines | Rows | Cells | Differing lines |
|---|---|---|---|---|
| closed x86 vs ours x86 | 59,599 | 45,602 | 539,368 | 2, both explained |
| ours x86 vs ours x64 | 59,599 | 45,602 | 539,368 | 0 |

Same rows, same order, same column types and values. Differences between the closed library and ours [C decompile]:
(1) `SelectAt(R, t, col, double)` formats the double with `%d` in the closed library (a crash for most values, `= 0`
for 0.0; no caller in the DLL), ours with `%!.15g`: the 2 A/B lines; (2) `CalculateMemoryStats` numbers (same layout);
(3) over-long log messages crash the closed library (`vsprintf_s`), ours truncates; (4) memory configuration (10.2).
Export names: our 104 x86 exports are all names in the closed import library [C].

### 10.4 x64 boot with the real database [L]

`build_all.py --config debug` pieces, then `run_smoke.py --db <copy of the 5.4.8 database>` (2026-11-10 04:39 EST,
DLL fork `x64` @ e9d0c4837 debug). The host loads `CvGameDatabase_x64.dll` beside the DLL, `Connection::Open`s the
run's copy with `OPEN_READWRITE`, passes it with `SetGameDatabase`; **all 18 steps pass**, no exception.

- `CacheGameDatabaseData` true; `Gamecore.log` not even created (0 warnings, was 67 with stubs); `CvAssert.log` empty
  (was 4 asserts); `Civ5SavedGameDatabase.db` created in the cache folder (3,072 B); `xml.log`: ID remapping done, "Validation
  Success" for the prefetch; `VP_LUAAPI` embedded script ran without errors.
- **Info counts, 24 types: x64 = SQL row count = the x86 DLL's counts** (civ5-headless run on the same database, x86
  VP 5.4.8 DLL + closed library, `Headless.log` of 2026-11-07, copied to `evidence/2026-11-10-1404/x86-reference-c5h-counts.txt`):
  Units 215, Buildings 284, Technologies 82, Policies 116, Civilizations 46, Promotions 419, Improvements 48, Resources
  57, Leaders 45, Eras 8, Builds 54, MinorCivilizations 120, Terrains 9, Features 25, Victories 5, Worlds 6, GameSpeeds 4,
  Handicaps 9, GameOptions 48, Climates 5, SeaLevels 3, Colors 246, PlayerColors 151, UnitCombatClasses 27; playable
  civilizations 44, AI-playable 44, playable minor civs 120 (x86 identical).
- Time: `CacheGameDatabaseData` 17.0 s in the debug (`/Od`) DLL, of which the prefetch 16.7 s (`xml-perf.log`:
  UnitPromotions 4.0 s, Buildings 3.9 s, MinorCivilizations 2.7 s, Units 2.1 s). Release: 11.7 s (10.6); the x86
  release DLL with the closed library takes 11.9 s for the same call on the same database (`Headless.log` phase
  `cache` 0.899 s -> `CacheGameDatabaseData: ok` 12.838 s) [L], so the database layer is not slower than the original.
- Closed-library stubs still called during this boot: CvLocalization `Lookup` 3,285, `toUTF8(len, form)` 3,285,
  `String()` 64, `~String` 3,349; FireWorks `FDebugHelper` 2. The lua51 entries in the stub log (`CCallWithErrorHandling`
  8, `FLua::Value::Value` 630, `Value::Set` 17,640) are **real implementations that log** (`closedlib-stubs/lua51/FLua_luastate.cpp`
  calls `STUB_CALL` and then does the work), so epic 1000's count of lua51 46 and FLua 5 as real stands [C source,
  checked 2026-11-10 at the MC's request].

### 10.5 The next blocker past the database

The boot (engine-contract phase A) no longer has a blocker. What stops the next milestone, exactly:

1. **No new-game driver** (phases B and C of `research/sources/civ5-headless/engine-contract.md` 4-5): the smoke host
   stops after `CacheGameDatabaseData`. The first new-game calls need engine services the host answers with
   zero/empty today [I from the contract, not run]: `NetInitInfo::Commit` -> `ICvEngineUtility1::GetEvaluatedMapScriptPath`
   (map script path), `closeInactiveSlots` -> `sendPlayerInfo` (net loopback), and `Init2`, which runs the Lua map
   script in the engine's Lua with `include()`, the VFS and `MapModData` (none in the host, 8.1). This is the x64
   host/server work, not a closed library.
2. (Resolved 2026-11-10 by epic 1700, section 11.) **CvLocalization (16 stubs) can stay stubbed for the database milestone and for a first new game, not for save
   identity** [I]: the boot calls `Lookup` 3,285 times and gets the key back; text reaches logs and UI, and city names
   pass through `CvCity::setName`/`isCityNameValid`, so a stubbed library can change which names are chosen and
   therefore saved strings. Needed real before any D10 x86-vs-x64 save comparison.
3. Not blocking: CvWorldBuilderMap stubs (`.Civ5Map` maps only; Lua map scripts do not use them), `FDebugHelper`.

### 10.6 Release build with the real database

`build_all.py --config release` (2026-11-10 04:40-04:49 EST, fork `x64` @ e9d0c4837, git checkout at the locked
commit, offline): **all five steps pass in 538 s** (stubs 8 s, CvGameDatabase 27 s, DLL 479 s of which the full-LTO
link 383 s, host 11 s, smoke 12 s). DLL 23,605,760 B, 0 unresolved, no `/FORCE`, narrowing gate 798 sites = baseline
(0 new) [C]. Smoke run with the real database: 18 of 18 steps, `CacheGameDatabaseData` 11.7 s (prefetch 11.45 s),
the same 24 info counts and 44/44/120 [L]. Evidence `tools/x64-dll/evidence/2026-11-10-1404/release/`
(`build_all.json`, `build_all-release.out`, `smoke-report.md`, `stubcalls.log`, `xml-perf.log`). Release game DLL
SHA-256 `b0658c85...5d942b`, `CvGameDatabase_x64.dll` `00b1e512...b7c2ed`.

## 11. Localization over the localization database (epic 1700, 2026-11-10)

Code: `tools/x64-dll/cvlocalization/` (its `README.md` has every member's behaviour, the layout and the differences
table; `ab/README.md` the A/B method and the closed library's measured behaviour). Issues 1701-1705. Fork `x64` @
e9d0c4837, clang-cl 19.1.7, MSVC 14.43, `/MD`. Evidence: `tools/x64-dll/evidence/2026-11-10-1702/` (closed reference,
A/A), `2026-11-10-1705/` (end-to-end A/B log), `server/evidence/1704/` (server comparisons).

### 11.1 The closed library [C decompile, L A/B]

- `CvLocalizationWin32Final Release.dll` (822,784 B, SHA-256 `9b783bc3...c50eac4`, toolkit game copy): 87 exports;
  **ICU 4.4 linked in statically**, its data read whole from the file passed to `Initialize` (`Locale/icudt44l.dat`,
  10,107,824 B); imports 12 `CvGameDatabase` members and runs its SQL through them. Decompile: Ghidra 12.1.4 headless
  on a copy (method of `reverse-engineering-setup.md`), functions 0x10001000-0x10010000 (ICU lies above).
- Dictionary: languages from the `Languages` table (Type, Name, PluralRule); lookup SQL `SELECT Text, Plurality, Gender
  from LocalizedText where Language = ? and Tag = ? LIMIT 1` (fallback disabled) or a `LanguagePriorities`-ordered
  query (fallback on, what the 32-bit host passes: `false`); at every language change it preloads every
  `SELECT DISTINCT Tag from LocalizedText`. Missing key: the String holds the key (exact, case-sensitive).
- The format syntax is parsed by the library itself (`{n_Tag}`, `:textkey`, `{@n}`, nested `{TXT_KEY_..}`, `<<`
  injection, `plural`, `gender` with noun traits, `number "pattern"`, `spellout`, `ordinal`, `roman`, case); multi-form
  text is split at `|` with per-form Plurality/Gender columns. Plural rules 1-16 are plain C (rule k = Mozilla rule
  k-1; en_US = 2). ICU only formats numbers (DecimalFormat from 15 significant digits of the double, half-even: 2.675
  as a double gives "2.68", as a float "2.67"; plain ints grouped `1,234,567`, plain floats `1,234.568`),
  spellout/ordinal/roman (RBNF) and case.
- Any missing argument or failed evaluation makes the whole composed string `""` (the DLL's `GetLocalizedText` then
  falls back to the key). Closed-library crashes, not reproduced: message overflow of 512/1024-byte `vsprintf_s`
  buffers (0xC0000417, 612 times in the corpus), `{KEY << {n_X}}` with its argument missing (access violation, 22 en_US
  keys), cyclic keys (stack overflow), Lookup before Initialize, Initialize after Shutdown (undefined).

### 11.2 The replacement

- `CvLocalization_<arch>.dll` + import library (as `CvGameDatabase_<arch>.dll`: the server initialises the dictionary,
  the DLL looks up through it, one copy per process), built by `build_loc.py` (6-7 s) for x86 and x64 from the same
  sources, linked against our `CvGameDatabase_<arch>.lib`; the same SQL text gives the same rows on the same SQLite
  3.7.17. **No ICU and no third-party source** (Minor decision, team 1700): DecimalFormat for the patterns the text
  uses, English spellout/ordinal, roman numerals and `LCMapStringEx` case mapping reproduce every corpus result; nothing
  was added to `deps/third-party.lock`. `Initialize` still requires the ICU data file to exist (closed behaviour) but
  does not use it.
- Exports: the closed library's 100 names except the 12 `ParameterTransforms` members (their exported signatures take
  an extra `vector<ParameterArgument>` the fork header lacks; no caller) and `SharedString::operator==`; all 16 imports
  of the DLL are real.
- `build_all.py` builds it after `cvgamedatabase`; the stub group `closedlib-stubs/CvLocalization/` is deleted (issue
  1703); `build_server.py` links it delay-loaded and ships it.

### 11.3 A/B against the closed library [L]

`tools/x64-dll/cvlocalization/ab/run_ab.py --out DIR` (one command, exit 0/1/2): a layout-agnostic 32-bit query
program linked against the closed import library plus a copy of the closed DLL, and against ours; corpus of 253,660
commands: every key of 10 languages (en_US 33,676 and 113,733 of the 9 others, enabled in a test copy of
`Localization-Merged.db`, SHA-256 `ff9a0997...56be0`), 67,728 formatted keys with synthetic arguments matched to their
placeholders (plus too few and too many), a 31,700-line spec sweep (ints, floats, doubles, int64, wrong types), 902
`stats.db` keys, 7,758 literal-key `Locale.ConvertTextKey`/`Lookup` calls from 744 game Lua files, 424 edge cases,
plural rules 0-17 for n = -200..1200.

| Comparison (2026-11-10; debug end to end 08:10, 297.9 s; release 197 s, same counts) | Blocks | Differing | Unexplained |
|---|---|---|---|
| closed x86 vs ours x86, main | 253,132 | 369 | 0 |
| closed vs ours, `nofallback` / `dictinit` / `cycles` | 453 / 187 / 17 | 2 / 2 / 6 | 0 |
| ours x86 vs ours x64, all five corpora | 253,847 | 0 | 0 |

Every difference is a closed-library crash or overflow (11.1), the per-run totals line, or Lookup before Initialize;
`reinit` is informational (closed undefined behaviour; 47 blocks differ). Closed A/A: identical after masking one
message that reads past the end of its string.

### 11.4 Effect in the server [L] (`server/evidence/1704/`, release, Tiny Continents 50 turns, seeds 30017/40017)

- `stats.db` x86 vs x64 without `--loc-db` translation: the first differing turn per table equals epic 1500's
  translated comparison (earliest `MapUnitsState` turn 34, the combat drift); 0 `TXT_KEY_` values (before: every table
  with names differed from turn 0/1). A/A of the x64 run identical (9,257 rows). 0.23 s/turn (epic 1500: 0.265).
- Round trip of x86 `Final_0050`: body 13,592,775 vs 13,592,880 B (was 13,592,883), 49 regions (was 50); the replay
  text ("Session 2 starts" vs `TXT_KEY_MISC_RELOAD`) is gone; left: version string +17 B, player 0's option list +88 B
  and same-length values (`server-host.md` 6). No measured x86/x64 difference comes from localization any more.

### 11.5 Not verified

- `Language::Compare` (`CompareStringEx` instead of the ICU collator) and the `ConvertUTC*` date helpers (Windows
  formats): outside the corpus, not imported by the DLL. The server's `Locale` members other than
  `Lookup`/`ConvertTextKey` follow the 32-bit host's documents, not a decompile of the EXE.
- Spellout/ordinal in languages other than English (no game text uses them); number patterns outside the corpus; Lua
  arguments as the EXE converts them (the corpus pushes them as the 32-bit host does).
- A longer game (Standard 200 turns) with real text; a save writer-identity test with one host for both builds.

## 12. The port in upstream shape and on upstream master (issue 3505, 2026-11-13)

Clone `D:/c5-64bit-work/3500/w4/fork` (`upstream` = LoneGazebo, master **ba8aafdca**, 2026-11-14 01:49 UTC, fetched
2026-11-13 21:48 EST). Series and branch tool: `tools/release/branches.py`, `tools/release/series/`
(`research/06-tooling-and-workflows/release-driver.md`, section "W4 (3505)").

### 12.1 Upstream shape of the port commits [C]

(Hashes: the commits were rewritten once at 23:05 to carry the attribution trailer, trees unchanged; the rewritten
ones are 00bef5695, 12a80396e, ceaf0c413, 15bb0a0b2, 891ec17e0, which existed only in the W4 clone, deleted at 3505's
hand-back; the pushed branches carry their picks 972cf559f, 2f83d30a7, a632ba4fb, c8c72011b, bc6c97daa on
`x64-release-master-ba8aafdc-r2` (corrected 2026-11-16, issue 3510: this sentence said the five were on the pushed
branches; see 12.4). The pushed branches are `i0-release-master-ba8aafdc-r2`
8be73b6f5 and `x64-release-master-ba8aafdc-r2` 76b3a5f70, whose trees equal the first pushed pair
`i0-release-master-ba8aafdc` df7dafc79 / `x64-release-master-ba8aafdc` ce47ef342 (branches.py `--compare-*`). The
table names the first hashes, which the builds below used.)

Five commits written on upstream master replace the pins' five port commits (b25fd45c0, 8e26515e8, 21f396027 +
f3d2f5669, 47d7664e9). The code is the pins' code apart from what master needs. The messages and code comments
carry no project ids (D95):

| Commit (master port) | Replaces | What changed against the pins' commits |
|---|---|---|
| 2b994f7bf Compile with clang-cl -m64 and a modern STL | b25fd45c0 + 8e26515e8's `FBatchAllocate.h` | all **7** `make_pair<CvUnit*, BuilderDirective>` sites (master has 2 more than 5.4.8); without b25fd45c0's crash-handler stub; **two master-only x86 constructs** found by the first compile: `TContainer.h` reads the thread id with `__readfsdword(0x24)` (upstream #13396's lookup cache; an implicit declaration on x64, so a link error or a wrong id) -> `__readgsdword(0x48)` on `_M_X64`; `!CvLuaApi.h` `luaEx_pushtag` `(void*)tag` -> `(void*)(UINT_PTR)tag` (warning only, same value) |
| 17126b7ac Crash handler: scan the stack and report addresses on x64 | 8e26515e8's crash hunks | keyed only on `_M_IX86`, `_M_X64`, `_WIN64`; master's newer crash code (config.ini opt-outs, crash log folder) merged around it |
| 47c6a3ce6 Out-of-process build (VP_OUT_OF_PROCESS) | 8e26515e8's `InitExeStuff` and popup hunks | master has no `InitExeStuff` (upstream PR 13379 replaced it by `Exe/`). With the define `CvExeApi.cpp`, `CvExeBuild.cpp`, `CvExeSymbols.cpp` compile to nothing and `Exe::*` answers as master does for an unknown EXE (`GetBuildName()` "Unknown"; `Can*`/`Try*` `bin_hooks_off` when BIN_HOOKS is off, else `unsupported_exe`; `Try*` logs the refusal); `CvLuaExe` registers the `Exe` table unchanged. Error dialogs: master's opt-out code (`ReadCrashOptOuts`: marker file, `VP_ERROR_POPUPS`, config.ini) plus a forced "out-of-process build" source |
| 292e2771a Keep save counts and saved hashes 32-bit on x64 | 21f396027 + f3d2f5669 | none (comments without "D10") |
| d9bcbefea Keep the allocation size of ... Initialize2DArray in size_t | 47d7664e9 | none |

Width fixes stay single expressions valid on both builds (`min<size_t>`, `pair<...>`, `(uint)`, `UINT_PTR`); the
only macros are the compiler's (`_MSC_VER >= 1900` for the swap overload, `_WIN64` for `FStlSerializedCount`).

**Crash handler review** [C, reading of 8e26515e8 and the master port; not exercised by a real crash]:
- Types and formats agree on both builds: every `CRASH_SIZE_FMT` argument is `size_t` (`maxMemory`, the six
  committed/reserved/free sums and both largest-free sizes, each `>> 10`); every `CRASH_ADDR_FMT` argument is
  `UINT_PTR`/`ULONG_PTR` (`uTargetAddr` is `ULONG_PTR` from `ExceptionInformation[1]`). x86 keeps `%08x`/`%u`.
- `AppendStackScan` on x64 is a heuristic scan of 8-byte slots from `Rsp` (16k slots, 128 KB), not an unwinder:
  false positives as on x86, no allocation. The call-site test covers `E8 rel32` and `FF /2` (REX prefixes sit
  before `FF`). `RtlVirtualUnwind` would give exact frames on x64 (unwind data is always present) but allocates
  nothing either; not done (optional, the draft's W4 note).
- The module RVA is kept 32-bit (`(DWORD)(dwVal - hMod)`), correct for any PE image. "Location (in file)" =
  RVA - 0xC00 assumes `.text` at RVA 0x1000 and file offset 0x400 [I for the lld-linked x64 DLL; check with
  `llvm-readobj --sections` before trusting x64 file offsets].
- The Sub2G memory statistics are printed on x64 too; they are meaningless there but harmless. On x64 the
  "address space low" class keys on the largest free block overall.
- Any architecture other than x86/x64 compiles the scan out (returns 0 frames).

### 12.2 Upstream master ported once [C, L 2026-11-13]

`tools/release/series/carry-master-ba8aafdc.tsv` (the release copy: port rows -> the commits of 12.1; two i0 rows
-> their resolutions; since 2026-11-16 the seven rows name the pushed picks of those commits, 12.4). `branches.py --base upstream/master --name master-ba8aafdc`: 23 applied, **19 dropped** (the 17
batch-1 picks, 50a389682 = 2bc6d34a1, ccfd6dd4d = 232ebaf72 #13552; all by "the base contains upstream ..."), 3
skipped: 4a8f99c79 (growth: replay history in one block; conflict in `CvPlayer.h`, as 0081 found), 27176dbfe (0078;
master removed the divisions, 0081), e6f904ad6 (needs 4a8f99c79). Two hand resolutions, both keeping both sides:
5201480f1 (`SqliteLogger.cpp`: master's `MOD_SQLITE_LOGGING_DURABLE_WRITES` guard around the two pragmas, issued
through `ExecuteWithRetry`) and ee893bc18 (`CvGame::update()`: master's `g_ulTContainerCacheThread` line, then
`m_iCacheStamp++`). Branches (local in the W4 clone): `i0-release-master-ba8aafdc` df7dafc79,
`x64-release-master-ba8aafdc` ce47ef342.

x64 debug build of ce47ef342, `python -I tools/x64-dll/build.py --src D:/c5-64bit-work/3500/w4/wt-x64m --unlocked
--config debug --out D:/c5-64bit-work/3500/w4/b-master-dbg [--stubs D:/c5-64bit-work/9045/bF/deps/stubs] [--shorten]`
(2026-11-13 22:13-22:19 EST):
- compile: 178/178 TUs, **0 errors** (PCH 21 s, 177 TUs 27 s wall on 32 jobs);
- before the TContainer/`!CvLuaApi.h` fixes: 1 `-Wimplicit-function-declaration` (`__readfsdword`) and 19
  `-Wint-to-void-pointer-cast` (one site), and the link without stubs listed 327 unresolved closed-library symbols;
- narrowing gate (`--shorten`): 799 sites against the baseline's 798, **10 new, 9 gone**. All 10 new are
  `lua_tointeger` -> `int` in new Lua bindings (`!CvLuaApi.h:51`, `CvLuaPlayer.cpp` 2528-13771 x9, `CvLuaPlot.cpp:2330`):
  Lua integer arguments, the class of 554 baseline rows (benign). Gone: lines master deleted. The baseline is not
  rewritten here (`tools/x64-dll/` is held by task 0131); the release build of master needs `--write-narrowing-baseline`
  or `build_all.py --no-gate` [the release build's result is in 12.3];
- link with the 9045 build's stubs (`D:/c5-64bit-work/9045/bF/deps/stubs`, built for 04b1211b8): **0 unresolved**,
  lld-link 106 s; DLL sha256 24f66826...168d, 22,460,928 B, version "Release-5.4.8-176-gce47ef342 Clean". So master
  uses no closed-library symbol the 5.4.8 stubs lack.
- save-width census (checklist step 8) on ce47ef342: `python -I tools/x64-dll/save-audit/save-width-census.py
  D:/c5-64bit-work/3500/w4/wt-x64m D:/c5-64bit-work/3500/w4/census-master --arch both --jobs 12` under `quietlock.py
  run`, 2026-11-13 22:45-22:52 EST (beside the 0158 capacity window): 178 TUs per architecture, 0 failed, 9 unique
  records on each, **0 x64-only**: master adds no 64-bit-only save write, and U4 still covers every one.
- x86 build of the same commit (the fork's `build_vp_clang.py --config debug` in a worktree of ce47ef342, under
  `quietlock.py run`, 2026-11-13 22:54:43-22:55:46 EST, 64 s): compiles and links (one LNK4099 PDB warning, as
  usual), DLL 2F22930A...0D7E, 21,842,944 B. So the master port's x86 branches of every `#if` compile (with the
  VC9 STL); behaviour on master is not run (the test setup has 5.4.8 game data only).
- P7 correction (`release-driver.md` section 1): master's x64 breaks are the four `Exe/` casts (compiled out by the
  define), 2 more `make_pair` sites, and **`__readfsdword` in `TContainer.h`**, which the pattern scan could not see.
  **Addition 2026-11-14 (3507) [L]:** our own code compiled against master's headers breaks too, which the link
  above (with 5.4.8's stubs) could not show. The closed-library stubs include `FLua.h`, which master (#13489) made
  `#error` (issue 3511, fixed). The proxy, `proxy_lua.cpp` and `proxy_luaexec.cpp`, and the hutest host got `lua.h` only
  through the DLL's `CvLuaSupport.h`, which on master no longer includes it (issue 3518).

### 12.3 Release (LTO) build of the master port (issue 3507, item 4a; 2026-11-14) [L]

Built by the release driver's `build` stage in the dry run on master (`release-driver.md` W7.2, rows 10-12).
- Source: `x64-release-5.4.9-ce4e6aae0` 363c337ce, made by `branches.py` from upstream master ba8aafdca with
  `carry-master-ba8aafdc.tsv`. Its tree `ffb89e4fd` equals `x64-release-master-ba8aafdc-r2` 76b3a5f70's tree.
- Command: `server/build_server.py --config release`, then `build_all.py`. The stubs were fixed (3511); the narrowing
  baseline got the 10 Lua sites of 12.2 in the dry run's scratch copy only; the smoke ran on master's own database
  (3513).
- Result: `build_all` OK in 460-554 s; the full-LTO link took 279-345 s, peak private 9.6 GB, with 0 unresolved.
  **DLL `7d074d9fa4a8eea743d1a5634ff420553e81f54edd26f5dc1516ed4f38ab626d`, 16,531,968 B.** Three builds in the same
  folder gave the same bytes; the link inputs carry no time stamps. Smoke: `CacheGameDatabaseData` 2.0 s, 43 playable
  civilizations, 120 minor. On build_all's former default, the 5.4.8 database, the DLL traps on upstream's release
  precondition "no such table: UnitPromotions_TerrainHeal".
- `server.exe` `6937dc71...`. The proxy builds once its Lua includes are fixed (3518).
- The 32-bit plain build of the i0 branch 6038f0746: `DBCA1FD3...` (271 s).
- Run on master's data: the save test's Tiny and Standard parts 1-3 pass (x86 = x64: 35,034 and 350,774 rows). The
  huge 5.4.8 save does not load in master's builds (issue 3517).

### 12.4 The release copy names pushed commits (issue 3510, 2026-11-16) [C, L]

The release copy named the five port commits of 12.1 and the two resolutions of 12.2 (0c27a9e08, aa9dd7322) by their
hashes in 3505's W4 clone, which was deleted at 3505's hand-back; only their **picks** were pushed. So `branches.py` on
any other clone stopped (3507's dry run, `release-driver.md` W7.2 row 3). The seven rows now name the picks:

| Was (gone) | Now (pushed pick) | On |
|---|---|---|
| 00bef5695 | 972cf559f | `x64-release-master-ba8aafdc-r2` |
| 12a80396e | 2f83d30a7 | `x64-release-master-ba8aafdc-r2` |
| ceaf0c413 | a632ba4fb | `x64-release-master-ba8aafdc-r2` |
| 15bb0a0b2 | c8c72011b | `x64-release-master-ba8aafdc-r2` |
| 891ec17e0 | bc6c97daa | `x64-release-master-ba8aafdc-r2` |
| 0c27a9e08 | d728590e3 | `i0-release-master-ba8aafdc-r2` (and x64-r2 through its merge) |
| aa9dd7322 | 46e1ef2f2 | `i0-release-master-ba8aafdc-r2` (and x64-r2 through its merge) |

Each pick's "(cherry picked from commit ...)" line names the old hash and its subject equals the series row's (branches.py
reports no subject mismatch). Verified 2026-11-16 18:42-18:48 EST, evidence in `evidence/3510/`:
- **Fresh clone** of the fork from GitHub (`git clone --filter=blob:none --bare`, no alternates) plus upstream master
  (then 1fb49429f; ba8aafdca is its ancestor): `git cat-file -t` gives `commit` for all 45 series rows and for the base;
  the seven old hashes are missing (also from the local fork checkout `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL`);
  each pick is an ancestor of the r2 branch above (`s03.log`, script `scripts/s03_verify_fresh.py`).
- **branches.py** on a clone that has the pushed branches (a `--shared` clone of the local fork checkout, origin and
  upstream fetched from GitHub; blobs are needed, which the partial clone lacks), `--base ba8aafdca --no-fetch`, no push:
  **23 applied, 19 dropped, 3 skipped** (the same rows as 12.2), 47 s; `i0-release-master-ba8aafdc-3510` tree
  `eb8ef8e73` and `x64-release-master-ba8aafdc-3510` tree `ffb89e4fd` **equal** the r2 branches' trees (`s05.log`,
  `branches.json`). The branches stayed local in the scratch clone (deleted at hand-back).
- **Negative**: the copy as it was (main 6a3172a1b) on the same clone, fetch allowed: exit 10, "7 series commit(s) not in
  the fork clone: 00bef5695 (line 27) ... aa9dd7322 (line 82)" (`s06.log`).

**Rule (for `tools/release/README.md` section 3; the MC copies it at the merge, 3509 holds the README now):** a release
copy names only commits that are on a pushed branch of the fork. Any commit a release copy's maker writes (a port in a
new shape, a hand resolution of a conflict) is pushed before the clone that made it is deleted, either as the source
commit on a branch of its own or, as here, by naming the pushed pick in the series. Before deleting such a clone, run
`branches.py` with the release copy on a fresh clone (or check every series hash with `git cat-file -t` there): the
stop "N series commit(s) not in the fork clone" means a commit was not pushed. The stop's unblock text ("fetch the
fork's branches ... or correct the series") names this cause since 2026-11-16 (3509, MC #3021; the rule is in
`tools/release/README.md` section 3; was "could also name this cause; that is `branches.py`, outside 3510's files").

## 13. The DLL source location as one setting (ticket 4701, plan 4.8, 2026-11-17)

Plan 4.8 of `../07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md`: one setting for where the
DLL source is, read by every build tool, the upstream layout as its default, this repository's value here.

### 13.1 The setting [C]

`tools/x64-dll/dll_source.py` (no path of any machine in it) gives the location, first match wins:

1. the environment variable `VP_DLL_SRC`;
2. `src = <path>` in `deps/dll-source.ini` (this repository only: it holds a path of the workstation, so it stays
   out of any export; a relative value is taken from the repository root);
3. the upstream layout: `..` seen from the folder that holds `tools/`. With these tools under `64bit/` at the top of
   the DLL repository that is the DLL repository itself, where `../CvGameCoreDLL_Expansion2` is (test
   `test_upstream_layout` builds that tree and checks it).

Here the file says `src = %USERPROFILE%/Documents/GitHub/Community-Patch-DLL-x64` [path scrubbed 2026-11-17], the path `build.py` hard-coded
as `DEFAULT_SRC` until this ticket, so the behaviour here did not change. The setting names only where the source is;
the commit it must be at is still `deps/dll.lock`, checked by `build.py` `resolve_src` (git checkout at the pin, or a
plain copy whose fingerprint matches). That clone is at `e9d0c4837` today (2026-11-17), not at the pin `04b1211b8`,
so a build without `--src` is refused here exactly as before, now with the origin in the reason:
`--src C:\...\Community-Patch-DLL-x64 (the default, from deps/dll-source.ini): HEAD is e9d0c4837. deps/dll.lock pins
04b1211b8 ...`. `python tools/x64-dll/dll_source.py` prints the location and which of the three gave it.

Changed: `tools/x64-dll/build.py` (`DEFAULT_SRC` and `DEFAULT_SRC_ORIGIN` from the setting, loaded by path because
other scripts load `build.py` by path), `build_all.py` (`--src` no longer required: without it the setting, checked
the same way), `smoke-host/gen_ifaces.py` (its own copy of the old path removed; uses `build.py`'s). Tests:
`tools/x64-dll/tests/test_dll_source.py` (8: the order, a relative value, a bad file, the upstream layout, this
repository's value, `build.py` following the environment variable and the file, and no script of `tools/x64-dll`
outside `evidence/` and `tests/` naming `GitHub/Community-Patch-DLL...`).

### 13.2 Every tool that locates the DLL source (grep of 2026-11-17, main 3f520edf1) [C]

Grep over `*.py *.bat *.ps1 *.cmd *.sh *.ini` for `Community-Patch-DLL`, `DEFAULT_SRC`, `CvGameCoreDLL_Expansion2`,
`Documents/GitHub`, `--src`, `--fork`, outside `research/`, `tracker/` and `evidence/` folders. Mentions in comments
(the `Origin (rule 13 ...)` heads of `shell/`, `tools/harness/lua.py`) and paths inside a tree already given
(`<src>/CvGameCoreDLL_Expansion2/...`) are not locations and are left out.

**Read the setting now** (through `build.py` `DEFAULT_SRC` when `--src` is not given):

| Tool | How |
|---|---|
| `tools/x64-dll/build.py` | `DEFAULT_SRC` = the setting; `resolve_src` |
| `tools/x64-dll/build_all.py` | `--src` optional since 4701; `resolve_src` |
| `tools/x64-dll/smoke-host/gen_ifaces.py` | own literal removed (4701); `resolve_src` |
| `tools/x64-dll/smoke-host/build_host.py`, `closedlib-stubs/build_stubs.py`, `closedlib-stubs/_test/run_selftest.py`, `closedlib-stubs/_test/wx_check.py`, `cvgamedatabase/build_db.py`, `cvgamedatabase/ab/run_ab.py`, `cvlocalization/build_loc.py`, `cvlocalization/ab/run_ab.py`, `civ5map/run_ab.py` | `--src` default `None` -> `resolve_src`; no change needed |
| `server/build_server.py` | `--src` default `None` -> `resolve_src`; no change needed |
| `server/tests/db/run_db_write_test.py`, `server/tests/feed/run_remote_feed_test.py`, `server/tests/limits/run_player_limits_test.py` | `--fork` default `x64build.DEFAULT_SRC` (a git clone, read for `git archive` of the pin); no change needed |
| `proxy/gen/classify.py` | `resolve_fork` -> `resolve_src` without `--lock`/`--commit`; no change needed |
| `proxy/gen/toolchain.py` `FORK_DEFAULT` (ticket 4708) | loads `dll_source.py` by path; `FORK` = `VP_PROXY_GEN_SRC`, else the setting (unchecked, as before); so also `extract_model.py`, `verify_slots.py`, `proxy/proof/build_proof.py` through `tc.FORK` without `--src` |
| `proxy/gen/extract_lua.py` `FORK` (ticket 4708) | `toolchain.FORK` (own literal removed) |
| `server/tools/audit_engine_calls.py` `DEFAULT_SRC` (ticket 4708) | loads `dll_source.py` by path (own literal removed) |
| `proxy/tools/test_dbfp.py` `SRC` (ticket 4708) | `toolchain.FORK_DEFAULT` (the setting; was a scratch copy of epic 2500, gone by 2026-11-17) |
| `tools/release/stages/testdata.py` `FORK` (ticket 4709) | the fork clone key: `dll_source.fork_clone()` (`VP_DLL_FORK`, else `fork =`, else the source setting); a git clone read for tags and `git archive`, not a tree (section 13.5) |
| `tools/d10/run_d10.py` `FORK` (ticket 4709) | the same key (D18 ancestry checks, `git archive` of the mod folders) |

**Name a path of their own (outside this ticket's touches; grant asked from the MC, queue entry 3375; MC #3384, 2026-11-17 01:14: rows 1-3 go to ticket 4708 after 4701's merge, rows 4-5 wait for epic 2600's merge):**
All five are done (2026-11-17) and moved to the table above: rows 1-3 (`proxy/gen/toolchain.py` `FORK_DEFAULT` and
`extract_lua.py` `FORK`, `server/tools/audit_engine_calls.py` `DEFAULT_SRC`, `proxy/tools/test_dbfp.py` `SRC`) by
ticket 4708 (section 13.4), rows 4-5 (`tools/release/stages/testdata.py` `FORK`, `tools/d10/run_d10.py` `FORK`, each a
fixed path of the workstation's `Community-Patch-DLL` clone) by ticket 4709 (section 13.5).

**Name a path but not of the x64 build's source (left alone, for the export's cleaning, E1):**
`tools/instances/provision.py` `VP548_RELEASE_SRC` (the Release-5.4.8 checkout `Community-Patch-DLL-5.4.8-pragmas`,
env `C5_VP548_SRC`, ticket 0423); `tools/release/take_release.py` `ART_CLONES` (refuses a `--fork` under the owner's clones);
`tools/x64-dll/thirdparty.py` `DEFAULT_CACHE` and `tools/x64-dll/cvlocalization/ab/run_ab.py` `TEMP` (Temp paths,
not the DLL source); `tools/x64-dll/smoke-host/run_smoke.py` (finds `Community-Patch-DLL` in symbolizer output to
shorten a path, no location). Tools that take `--src` as required and have no default (`proxy/build_proxy.py`,
`proxy/gen/gen_runtime.py`, `proxy/gen/regen_model.py`, `proxy/tools/test_policy_table.py`, `proxy/tools/test_purity.py`,
`proxy/tools/derive_purity.py`, `server/tests/feed/gen_standins.py`, `perf/thinlto/thinlto_arm.py`,
`tools/drift/build_pair.py`, `tools/drift/instrument*.py` `--root`) hold no path; they could take the setting as a
default later.

### 13.3 The x64 DLL before and after [L]

Measured 2026-11-17 about 00:57-01:32 EST on the workstation, evidence `evidence/4701/` (scripts in `scripts/`). Source: a
plain copy (`git archive`) of the pin `04b1211b8` from the fork clone, fingerprint matching `deps/dll.lock` (473
files). Both builds `build_all.py --config release --offline`, `SOURCE_DATE_EPOCH=1794754471` (the same for both, so
the tools' commit time does not enter), one after the other (one LTO link at a time):

- **Before:** the tools of main `3f520edf1` (a `git archive` in `C:/c5-64bit-work/4701/before-repo`), `--src` the copy:
  every step rc 0, 981 s (`before-steps.txt`).
- **After:** the tools of this branch, **no `--src`**: `VP_DLL_SRC` pointed at the same copy, so the source came from
  the setting (`build_all: config release; src C:\c5-64bit-work\4701\src (plain source copy ...)`): every step rc 0,
  818 s (`after-steps.txt`).

Result (`compare.txt`): **`CvGameCore_Expansion2.dll` byte for byte equal**, SHA-256
`4a55e21f73492823f4710e6ff5279b1e5e9d1fa62cdf58d800bd2ca356aa7be4` (23,666,688 B), and its PDB equal (`472b2861...`);
`lua51_x64.dll` `30724d6d...`, `CvGameDatabase_x64.dll` `9c37b39e...`, `CvLocalization_x64.dll` `35863ede...` equal
(and equal to deps/dll.lock's release libraries). The DLL is not the lock's release `29F58E17...`: that one was built
at tools `fdee65668` without `LTO_DETERMINISM_LINK` and another epoch.

The test host `smoke_host.exe` (not shipped) differed. It embeds its own `--out` folder (the PDB path,
`before\host` vs `after\host`, which shifts later bytes: 4,797 bytes in 1,029 runs, `hostdiff.txt`). Built again
into one `--out` folder, the before tools against the after tools (no `--src`, the setting) still differed in 8 bytes,
all in the PDB GUID of the debug directory (`hostcmp-bytes.txt`): lld's `/Brepro` GUID is a hash of the PDB, which
holds the tools' own folder (`before-repo` vs this worktree). With the after tools extracted into the **same** folder
as the before tools, the host is **equal** (`3961c3f9...`, `hostcmp.txt`). So the host's difference was only the
folders, as before this ticket (0351's clean-vs-validated note found the same kind: stamps and PDB GUIDs).

Tests: `tools/x64-dll/tests` 108 passed (`tests-x64dll.txt`, 12 s); `tools/harness/tests/test_safety_static.py` 55
passed (2026-11-17, branch merged with main `e998c4908`; again at hand-back on main `76cacb53d`).

Not verified: a build through `server/build_server.py` (it follows `DEFAULT_SRC` by `resolve_src`, not run here);
the upstream layout in a real upstream checkout (only the crafted tree of the test); the debug configuration.

### 13.4 Part 2: the proxy generator, the engine-call audit and the dbfp test (ticket 4708, 2026-11-17)

**What changed [C]** (branch `task-4708-dll-source-part-2`, from main `af996789a`):

- `proxy/gen/toolchain.py`: `FORK_DEFAULT` = `dll_source.dll_source()` (loaded by path as `load_dll_source()`, the
  folder `x64-dll` is not a package name); `FORK` = `VP_PROXY_GEN_SRC`, else `FORK_DEFAULT`. **`VP_PROXY_GEN_SRC`
  keeps working** as before: it overrides the setting, and `use_source()` still sets it for the worker processes of a
  `--src` run. The default stays unchecked against `deps/dll.lock`, as before; its commit label `COMMIT_DEFAULT` is still
  the fixed `e9d0c4837` (the clone's HEAD), whatever tree the setting names (a run without `--src` at another tree
  records that label in `source.commit`, which the model comparison masks). `BUILD_ROOT` (compiler scratch) =
  `VP_PROXY_GEN_WORK`, else `tempfile.gettempdir()/claude/team2100`: the same folder on the workstation
  (`gettempdir()` is the user's Temp folder), no literal user path.
- `proxy/gen/extract_lua.py`: imports `toolchain` at module level; `FORK`, `PINNED_COMMIT`, `OUT` come from
  `toolchain.FORK`, `PINNED_COMMIT` and `BUILD_ROOT` (its own literals removed). `extract_model.py`, `verify_slots.py`
  and `proxy/proof/build_proof.py` already read `tc.FORK` / `tc.BUILD_ROOT`, so they follow the setting without `--src`.
- `server/tools/audit_engine_calls.py`: `DEFAULT_SRC` = the setting (loads `dll_source.py` by path). It never read
  `VP_PROXY_GEN_SRC` and still does not.
- `proxy/tools/test_dbfp.py`: `SRC` = `toolchain.FORK_DEFAULT` (the setting). It replaced the scratch copy of epic 2500
  in the user's Temp folder, which no longer existed on 2026-11-17, so the test's default was broken before this ticket.
  The test reads only `CvGameDatabase/include` and `CvGameDatabase/lib/CvGameDatabaseWin32.lib` from it.
- No absolute user path is left in the four files (the test below greps them). The tool paths of LLVM and Visual
  Studio under `C:\Program Files` stay: they are not the DLL source.
- Test `proxy/gen/tests/test_dll_source_setting.py` (offline, 5 tests, 3 s): no user path in the four files;
  `VP_DLL_SRC` reaches all four; `VP_PROXY_GEN_SRC` still overrides the generator but not the audit and dbfp test;
  without either, all four equal what `tools/x64-dll/dll_source.py` prints; the scratch root. Each probe runs in a
  fresh `python -I` because the defaults are read at import.

**Checks [L]** (2026-11-17, workstation; evidence `evidence/4708/`, scripts in `scripts/`; source: a plain copy
(`git archive`) of the pin `04b1211b8` from the fork clone, `C:/c5-64bit-work/4708/src`, 353 MB):

- `regen_model.py` at the pin, the tools of this branch, `VP_DLL_SRC` = the copy and `--src` = what `dll_source.py`
  printed (`regen_model.py` takes `--src` as required): **EQUAL in 217 s** (01:51:23-01:55:01 EST): `interfaces.json`,
  `slot-proof.json`, `lua.json`, `model.json`, `policy-table.tsv` and the generated runtime equal the committed
  `proxy/model` (`model-report.txt`, `regen-and-lua-nosrc.txt`).
- `extract_lua.py` **without `--src`** (the default path this ticket changed; `VP_DLL_SRC` = the copy): 3,467 methods in
  83.5 s; its `lua.json` records `fork_path` `C:/c5-64bit-work/4708/src` (the setting reached it) and is **equal** to
  the committed one under `regen_model.py`'s masked comparison (only `source.commit` differs: the fixed label above).
- `audit_engine_calls.py` without `--src` and without `VP_DLL_SRC`, main `af996789a`'s copy vs this branch: TSV and
  stdout **byte for byte equal** (286 methods, 53 served, 233 stubs; both read the clone at `e9d0c4837`). With
  `VP_DLL_SRC` = the pin copy the TSV differs (call sites of `04b1211b8`), so the setting moves it (`audit.txt`).
  Each audit run takes about 2 minutes (it reads every source file once per method).
- `test_dbfp.py` (x86 build of `dbfp_unit.cpp` against the closed database library, then the unit run): **PASS** without
  `--src` (the setting: the fork clone) and **PASS** with `VP_DLL_SRC` = the pin copy, 39 `PASS` lines and 0 failures
  each (`dbfp.txt`, `dbfp-default-unit.txt`, `dbfp-pin-unit.txt`).
- Unit tests: `proxy/gen/tests` 33 passed (18 `test_extract_lua`, 10 `test_gen_inout`, 5 new);
  `tools/x64-dll/tests/test_dll_source.py` 8 passed; `tools/harness/tests/test_safety_static.py` 55 passed.
  No test exists for `audit_engine_calls.py` or `test_dbfp.py` beyond the runs above.

Not verified: the upstream layout (`..`) for these four tools in a real upstream checkout; `extract_model.py` and
`verify_slots.py` run alone without `--src` (they read the same `tc.FORK`; `regen_model.py` runs them with `--src`).

### 13.5 Part 3: the fork clone of the release and save-identity tools (ticket 4709, 2026-11-17)

**Two checkouts, so a second key [C].** `tools/release/stages/testdata.py` and `tools/d10/run_d10.py` read a git clone
of the fork for release tags (`git archive` of the tag's mod folders), the D18 ancestry and cherry-pick checks
(`merge-base --is-ancestor`, `git cherry`) and `git archive` of the release commit. They never compile from it. Here it
is not the `src` checkout: `src` is the x64 work clone (`Community-Patch-DLL-x64`), the fork clone is the long-lived
`Community-Patch-DLL` clone that fetches the release tags. On 2026-11-17 the x64 clone also held `Release-5.4.8` and
`e9d0c4837` (`git tag -l`, `git cat-file -t`), but a later release tag reaches only the fork clone, so the tools keep
the clone they used.

`tools/x64-dll/dll_source.py` gained the fork clone, first match wins:

1. the environment variable `VP_DLL_FORK`;
2. `fork = <path>` in `deps/dll-source.ini` (beside `src`; a relative value from the repository root);
3. the DLL source setting (13.1) in its own order. Upstream (`64bit/` inside the DLL repository, no setting file) that
   is the DLL repository itself, which is both the source and the clone with the tags.

`fork_setting()` returns (value, origin), `fork_clone()` the absolute path (unchecked: the tools' git commands fail on a
wrong one, as before). `read_setting_file()` takes the key as an argument; `src` lookups ignore the `fork` line.
`python tools/x64-dll/dll_source.py` prints both lines (`fork clone: ... (from deps/dll-source.ini \`fork\`; ...)`).
The file here gives `fork =` the clone both tools named as `FORK` before, so the behaviour here did not change.

**Changed:** `testdata.py` and `run_d10.py` load `dll_source.py` by path and set `FORK = fork_clone()` (testdata's
`--fork` default and the driver's `ctx.fork` fallback follow it); the fixed clone path is gone from both. `run_d10.py`'s
`C5H` became `Path.home() / 'Documents' / 'civ5-headless'` (the same folder), so it names no user path at all.
`testdata.py` keeps its `C5H` and `CORE_DB` literals: the MC kept this ticket's edit there to the `FORK` lines because
ticket 0464 edits the same file (#3828); they are the next candidates for the export's cleaning.

**Tests:** `tools/d10/tests/test_fork_setting.py` (offline, no git, 9): the order, a blank or empty value, a relative
value, this repository's `fork` key, both tools' `FORK` following the setting and `VP_DLL_FORK` (each loaded fresh,
the default is read at import), no fork clone path in either file, no user path in `run_d10.py`.
`tools/d10/tests/test_run_d10.py`'s D18 class runs only where `run_d10.FORK/.git` exists, as before.

## Open items

- ~~No trial x64 compile: the real error count is unknown.~~ Resolved 2026-11-10: 29 errors, 30 casts, 794 implicit
  narrowings, 336 unresolved symbols (section 0).
- Byte-exact `FDataStream`: implemented from the x86 disassembly with its formats confirmed (section 2.1) and
  self-tested; x86 save identity of the DLL-side pins tested (4.1). **Not yet run: any save written by the x64 DLL**
  (needs the x64 database layer and host, follow-on epics A2-A4). `.Civ5Map` reader/writer still stubbed (A7).
  (Updated 2026-11-10; was "required but untested".)
- 64-bit Lua vs the game's copy: integer conversion now matches bit for bit (2.1, 3.2); the game's `lua51_Win32.dll` is
  stock 5.1.4, so the differences of headless `engine-contract.md` 10.5 come from the EXE and become server (I1-B) work;
  arithmetic (x87 vs SSE2) and CRT formatting differences remain [I]. (Updated 2026-11-10.)
- EXE-address code: the only such block on 5.4.8 (`CvDllGame::InitExeStuff`, force-resync flag) is compiled out under
  `VP_OUT_OF_PROCESS` (3.1); the shell-side proxy must write that flag for the server. `engine-queue-guard` and the
  buffer patches are not on 5.4.8/`x64` (they live on `claude/shell-gates-d8e422`). (Updated 2026-11-10.)
- STL drift (VC9 -> MSVC 14): open question Q14 in `07-architecture-decisions/open-questions.md`. Source inventory
  done 2026-11-10 (section 9): 1 pointer-order site and 29 CRT math lines can change outcomes; fix epic proposed (9.4).
- Floating point: section 5 corrected 2026-11-10 (the 5.4.8 DLL uses SSE arithmetic); whether x86 and x64 games drift
  at all is now a measurement to make, not a given.
- Database: done 2026-11-10 (section 10). Localization: done 2026-11-10 (section 11). Remaining: `.Civ5Map` replacements, the x64 host's new-game path
  (section 10.5), x64 save identity: the follow-on epics in `effort-and-packages.md` (2026-11-10 section).

## Contradictions and uncertainty

- Pointer casts: the study counts three `reinterpret_cast<unsigned int>` in one header; `why-32-bit.md` says "about 19
  rough hits" in a VP fork. Different greps on different trees; neither is a compile. Resolved 2026-11-10 by the
  compiler on 5.4.8: 30 explicit pointer-to-int warnings at 7 sites, the study's three `FBatchAllocate.h` casts among
  them (section 0).
- Symbol count: 338 (census, 2026-11-09) vs 336 (link, 2026-11-10). Per group, the differences are at most 4 and come
  from the census's counting base and the link's name-pattern grouping. The link figure is current.
- CvGameCoreDLLUtil: the memory note says "DLLUtil 124 trivial enum serializers"; the study says 124 symbols of which 121
  are trivial enum serializers.
- Object count: the study's census covers "173 objects"; the unit-test study says gGlobals-touching code "links 169 of 172
  objects". Probably different build configurations; not reconciled.
- Effort framing: `why-32-bit.md` says a 64-bit host is "weeks of work"; the study's package A + B is 175-300 agent-hours.
  Not necessarily contradictory (different units and scope).

Sources: `research/sources/thin-client-study.html` section 4 and caveats; `research/sources/memory-notes/civ5-64bit-architecture-options.md`;
`research/sources/memory-notes/dll-unit-test-study.md`; `research/sources/REVIEW.md` sections 1, 2, 4, 5;
`~/Documents/civ5-headless/docs/why-32-bit.md` (read in place).
