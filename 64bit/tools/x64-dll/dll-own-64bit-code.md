# The DLL's own 64-bit code (issue 1002)

Crash handler, explicit pointer casts, the code that only works inside the EXE's process, and a grep for bitness
assumptions the compiler does not flag. Written 2026-11-10 by worker 1002 of team 1000. DLL fork commit
**c7f42fb40** (detached, on top of `x64` tip b25fd45c0, a worktree `Community-Patch-DLL-x64-w1002` beside the DLL source clone).
Line numbers are at c7f42fb40. Evidence: `tools/x64-dll/evidence/2026-11-10-w1002/`.

## Result

| Check | Value |
|---|---|
| x64 compile, clang-cl 19.1.7 `-m64 /MD /DVP_OUT_OF_PROCESS`, probe flags otherwise | 173/173 files, 0 errors [C] |
| `-Wpointer-to-int-cast` + `-Wvoid-pointer-to-int-cast` | **0** (was 30 at 7 sites) [C] |
| Same two files without `VP_OUT_OF_PROCESS` (x64) | 0 pointer-to-int warnings [C] |
| x86 `build_vp_clang.py --config debug` | builds and links, exit 0 [C] |
| x86 code change | none: baseline and new `CvGlobals.obj`/`CvDllGame.obj` (x86 debug flags, same PCH) compared function by function with `llvm-objdump`; `AppendStackScan`, `CustomFilter`, `InitExeStuff` identical; the only differences are `__LINE__` immediates in 85 `CvGlobals` functions, shifted by the inserted lines [C] |
| Remaining 64-bit warnings | 186 `-Wdeprecated-declarations` only (`GetVersionExA`, tr1) [C] |

`-Wshorten-64-to-32` (off by default, issue 1003) was not re-run.

## 1. Crash handler (`CvGlobals.cpp`)

Ported, not compiled out [C]:

- `AppendStackScan` (2644): reads `Rsp` on x64 (`Esp` on x86), walks `ULONG_PTR` slots (8 bytes on x64), budget
  16384 slots (64 KB x86, 128 KB x64), RVA computed as `(DWORD)(value - module base)` (a PE image is under 4 GB).
  `IsPrecededByCall` needs no change for x64: `E8 rel32`, `FF /2` with REX prefixes before the `FF` byte, and
  `FF 15 disp32` (RIP-relative and CFG `__guard_dispatch_icall_fptr` calls) all put `E8`/`FF` at 2..7 bytes before
  the return address [I: from the encoding rules, not tested on a live x64 crash].
- `CustomFilter` (2719): `exceptionAddressAdjusted` is `UINT_PTR`; no `(DWORD)pointer` casts remain. Addresses print
  with `CRASH_ADDR_FMT` (`%08x` on x86, `%016I64x` on x64) and sizes with `CRASH_SIZE_FMT` (`%u` / `%I64u`): on x64 the
  old `%u` cut `maxMemory>>10` (128 TB -> 2^37 KB). On x64 "address space low" uses the largest free block overall,
  not below 2 GB. The minidump comment stream says `Architecture: x64` on x64.
- Not verified: a real x64 crash through the filter (needs the smoke host, 1007). The `Location (in file)` offset
  subtracts 0xC00 (`.text` raw/virtual delta of the 32-bit DLL); whether the x64 lld-link DLL has the same delta is
  unchecked [I].
- Server note: `CvGlobals::init` (3066) calls `SetUnhandledExceptionFilter(CustomFilter)` and does not chain the
  previous filter. In the server this replaces any filter the server installed first. Left as is; the server
  (I1-B) decides whether it owns the filter.

## 2. The 30 pointer-to-int casts

| Site (probe line) | Fix |
|---|---|
| `FBatchAllocate.h` 70, 99, 102 (3 sites, 27 warnings: one per template instantiation) | `reinterpret_cast<unsigned int>(p)` -> `reinterpret_cast<size_t>(p)` (alignment remainder; same type on x86) |
| `CvGlobals.cpp` 2696, 2707 x2 | `(DWORD)pointer` -> `(UINT_PTR)` (section 1) |
| `CvDllGame.cpp` 641 | `DWORD baseAddr = (DWORD)GetModuleHandleA(NULL)` -> `UINT_PTR`; compiled out under `VP_OUT_OF_PROCESS` anyway |

## 3. Code that only works inside the EXE's process

`VP_OUT_OF_PROCESS` is the define for the server build. Nothing was deleted.

| Block | What it does | Under `VP_OUT_OF_PROCESS` | What the proxy or server must do instead |
|---|---|---|---|
| `CvDllGame.cpp:595-711` `InitExeStuff` (called from `DoGameStarted` when `MOD_BIN_HOOKS`) | Detects the EXE variant from `GetModuleFileName(NULL)`; computes `GetModuleHandleA(NULL) + RVA` of the RNG force-resync byte flag (DX11 0x02dd2f68, DX9 0x02dc2d68, Tablet 0x02dd4f50 at ImageBase 0x400000) and hands the `int*` to `CvGame::SetExeWantForceResyncPointer`. Also holds a commented-out `VirtualProtect` NOP-patch example. | Whole body compiled out; sets `BIN_UNKNOWN`, so `CvGame::IsExeWantForceResyncAvailable()` (`CvGame.cpp:14517`) is false and `SetExeWantForceResyncValue` never dereferences the pointer. Lua `Game.IsExeWantForceResyncAvailable()` returns false; `Game.SetExeWantForceResyncValue()` is a no-op. [C: x64 `CvDllGame.obj` with the define references neither `GetModuleHandleA`, `GetModuleFileNameA` nor `SetExeWantForceResyncPointer`] | The shell-side proxy owns the flag: it detects the variant and writes the byte in the EXE when the server asks (a new server->proxy message, or a proxied `ICvEngineUtility` call). `CvGame` would then call through that channel instead of a raw pointer. Multiplayer-host only feature. |
| Error popups: `CvGlobals.cpp:2146-2153`, `3071-3073` (`g_bPopupsDisabledByUser`) | `MessageBoxA` for asserts, preconditions (`CvGameCoreUtils.cpp:256, 309, 371, 388`, via `AreErrorPopupsSuppressed()`) and the crash dialog (`CvGlobals.cpp` `CustomFilter`) | Popups off from static init on (the `nopopups.please` marker is not read). Logs (`CvAssert.log`, `crashlogs\crashes.log`) and minidumps unchanged. | The server must never block on a modal dialog or take focus (AGENTS.md section 4). The proxy/shell should surface server crashes and fatal asserts to the user. Files land in `crashlogs\` relative to the server's working directory. |

Instruments named in the brief [C: `git grep` on both branches, 2026-11-10]:

- `EngineQueueGuard`, `EngineAllocTrace`, `MemoryImports`/`MemoryHooks`, `ShellHelperLoader`, `MemoryDiagnostics`: **absent
  from `x64`** (5.4.8 base). They exist only on `claude/shell-gates-d8e422` (and `engine-queue-guard`,
  `memory-diagnostics`): `EngineQueueGuard.cpp:592` (`GetModuleHandleA(NULL)`), `:610-620` (`VirtualProtect` on EXE
  slots), `EngineAllocTrace.cpp:147, 899-904, 1075-1079`, `MemoryImports.cpp:478-484` (IAT patching). Whoever ports
  them onto `x64` must put them under `#if !defined(VP_OUT_OF_PROCESS)`; in the server they would patch the server's
  own image, not the EXE. Their job (protecting the EXE's UI queue, tracing EXE allocations) moves to the proxy in
  the shell.
- No other `GetModuleHandle(NULL)`, `VirtualProtect`, `ReadProcessMemory`/`WriteProcessMemory` or fixed EXE address
  in the DLL's own source on `x64` [C: grep].

Cross-process dependencies that are not memory and stay compiled in:

- `CvGame::writeSaveGameDB` (`CvGame.cpp:11140`) opens `<gDLL->GetCacheFolderPath()>Civ5SavedGameDatabase.db`, a file
  the EXE writes. The server needs the same cache path (or the proxy must pass the bytes).
- `ExternalPause` (`CvGame.cpp:1589`, `EXTERNAL_PAUSING`) polls the named mutex `TurnByTurn`; works across processes.
- The 22 C exports the DLL resolves from the EXE with `GetProcAddress(GetModuleHandle(NULL))` live in the closed
  FireWorks lib, not in the DLL's source; issue 1006's stubs replace them.

## 4. Bitness assumptions the compiler does not flag

Greps over `CvGameCoreDLL_Expansion2`, `FirePlace/include`, `CvGameCoreDLLUtil/include`, `CvGameDatabase/include`,
`CvLocalization/include`, `CvWorldBuilderMap/include`, `ThirdPartyLibs/Lua51/include` (2026-11-10) [C]:

| Pattern | Hits | Verdict |
|---|---|---|
| `sizeof(T*) ==` | `FastDelegate.h:786` (static assert that data and function pointers have the same size) | holds on x64; compiles [C] |
| `0xFFFFFFFF` as pointer | none. `FStlContainerSerialization.h:90, 153` compare a read `size_type`/`size_t` count with `0xFFFFFFFF` | not a pointer, but on x64 the sentinel never matches an 8-byte count: **issue 1004** (the width pin must keep the sentinel test on the 32-bit value) |
| `int`/`DWORD` holding `HANDLE`/`HWND`/`LPARAM`/`WPARAM` | `FFireTypes.h:96, 166` `typedef int HWND`, `LPARAM` as `LONG*` | `_PS3`/`_NDS` branches only; not compiled on Windows |
| `long` as pointer size, `(long)ptr` | only a commented-out line in `FastDelegate.h:791` | none live |
| `%x`/`%d` printing pointers | only in the crash handler (fixed, section 1). Hash and code `%08X` sites (`CvInfos.cpp:12753, 12870`, `CvImprovementClasses.cpp:1958`, `CvGame.cpp:11649`, `CustomMods.cpp:622`) print ints | fine |
| `%u` with `size_t` | e.g. `CvGameCoreUtils.cpp:2268` (`PrintMemoryInfo`, `WorkingSetSize/1024`) | on the MS x64 ABI each vararg has an 8-byte slot, so `%u` reads the low 32 bits without shifting later arguments: wrong only above 4 G units; log text only. Left for 1003 |
| `GWL_*`, `SetWindowLong`, `_beginthread`, `CreateThread` | none | - |
| `Interlocked*` on pointer-sized data | `FAtomic.h:43` uses `InterlockedCompareExchangePointer`; the 64-bit `_FAtomicIncr64` helpers map to `InterlockedIncrement64` under `_WIN64` (`FAtomic.h:18-25`), so they need no closed-lib symbol on x64 | fine |
| Inline asm | `FTimer.h:88-119` and `FDebugHelper.h:22-33` under `!_WIN64` / `_M_IX86` with x64 branches; `luaconf.h:557-563` under `_M_IX86`; `clang.cpp` (repo root, x86-only `_ftol3`/`_ltod3` naked asm) | **`clang.cpp` must not be in the x64 build** (issue 1001's script) |
| `memset`/`memcpy` with a literal 4 | `CvDllDatabaseUtility.cpp:870` (an `int` buffer) | fine |

Finding for issue 1005 (Lua): `luaconf.h:557-563` makes `lua_number2int`/`lua_number2integer` an x87 `fistp` (round to
nearest even) only for MSVC x86 without `__SSE2__`; every other build, including x64 and clang `-msse3`, truncates
with a C cast. A stock Lua 5.1.4 built for x64 therefore converts 2.5 or 2.7 to an integer differently from a Lua
built with VC9 x86 defaults, as the game's is [I: the game's lua51 build flags are not known]. The DLL itself does not
use the macro [C: grep].

## Not done here

- Implicit 64->32 narrowings (794 sites): issue 1003.
- Save-stream widths, including the `0xFFFFFFFF` sentinel above: issue 1004.
- A live x64 crash through `CustomFilter`: needs the smoke host (1007).
