# Closed-library symbols: CvGameDatabase, CvLocalization, lua51 (issue 1005)

The 108 symbols of these three groups that the x64 DLL leaves unresolved (46 + 16 + 46 of the 336 in
`tools/x64-dll/evidence/2026-11-10-probe/unresolved-336-grouped.txt`), what this folder puts in their place,
and what a real replacement costs. Written 2026-11-10 against the fork's `x64` branch at b25fd45c0
(Release-5.4.8 + x64 compile fixes). Confidence: `[C]` confirmed by build, link, disassembly or source;
`[L]` verified by running; `[I]` inferred.

## Result

- **All 108 resolve** `[C]`. Probe objects (x64, all 173, b25fd45c0, `/MT`) + `CvGameDatabase.lib`,
  `CvLocalization.lib`, `lua51.lib` built with `--crt MT`, linked with lld-link 19.1.7 `/FORCE:UNRESOLVED`:
  228 unresolved left, exactly the other four groups (CvGameCoreDLLUtil 123, FireWorks 68, CvWorldBuilderMap
  32, FLua 5), 0 from these three, 0 new, no duplicate or mismatch diagnostics. Evidence:
  `tools/x64-dll/evidence/2026-11-10-1005/probe-link-unresolved-summary.txt` and
  `probe-link-unresolved-228-grouped.txt` (the remaining list, for issue 1006).
  Repeated 2026-11-10 03:05 EST with issue 1001's `build.py --config debug --census --stubs <libs> --no-commit-id`
  (fork `x64` at f3d2f5669, `/MD`, libs built with the default `--crt MD`): 173/173 compiled, link 3.2 s, the same
  228 unresolved, 0 from these groups, an empty link log apart from the undefined-symbol reports `[C]`.
- **Stub calls write the log** `[L]`: `_test/run_selftest.py` builds the three libraries `/MD`, links
  `_test/stub_selftest.cpp` (compiled with the headers' `dllimport` intact, so every call goes through an
  `__imp_` slot as in the DLL) and runs it: all checks pass, the log has 26 FIRST lines, the on-demand COUNT
  block and the exit-flush block (`evidence/2026-11-10-1005/selftest-stubcalls.log`). Not yet seen inside the
  real DLL (that is the smoke host, issue 1007).
- **Lua is real**: stock Lua 5.1.4 from lua.org, one x64 change (number-to-integer rounding, below). The self-test
  runs Lua code, checks `lua_tointeger` against the 32-bit game's rounding, and exercises the three FLua members.

## Build

```
python tools/x64-dll/closedlib-stubs/build_stubs.py --out DIR [--config debug|release] [--crt MD|MT]
python tools/x64-dll/closedlib-stubs/_test/run_selftest.py --out DIR
```

`build_stubs.py` globs `closedlib-stubs/<group>/` (every folder with `*.cpp` except `common`, `_*`, `.*`) and
writes `DIR/<group>.lib` from the group's `*.cpp` + `common/stublog.cpp` + a generated `_imp_pointers.c`. Group
`lua51` also gets the Lua sources. Flags: clang-cl 19.1.7 `-m64`, `/std:c++14`, `/MD` (default), `/Zl`, the fork's
include dirs and issue 1001's defines plus `_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS` (the DLL sets it in
its PCH before `<hash_map>`) and `_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING`. `--crt MT` exists only to link
against the 2026-11-10 probe objects, which were compiled `/MT`; with `/MD` objects, use the default. 33 sources,
about 15 s on this machine when idle (48 s while other workers were compiling) `[L]`.

## Linking: static libraries, `__imp_` slots generated

The headers declare everything `__declspec(dllimport)` (`CvGameDatabaseAPI`, `CvLocalizationAPI`), so the DLL's
objects reference `__imp_?Open@Connection@Database@@QEAA_NPEBDH@Z`, a pointer slot that an import library
would supply, never the function itself. Choices:

| Option | Verdict |
|---|---|
| Static library, rely on lld-link binding `__imp_X` to a local `X` | **Does not work** `[C]`: lld-link 19.1.7 binds `__imp_X` to `X` only when `X` is already defined; it does not pull `X` out of a library for it. First link 2026-11-10: all 62 DB/Loc imports stayed unresolved. |
| Stub DLLs + import libraries | Works, but each stub DLL needs its own copy of the logger (or a shared logger DLL), adds three DLLs to load in the smoke host and the server, and gives nothing the server needs. |
| **Static library with generated `__imp_` pointers** (chosen) | `build_stubs.py` lists the C++ symbols each group's objects define (`llvm-nm -g --defined-only`, skipping `??_` compiler names, `std::` and `type_info`) and writes `_imp_pointers.c`: per symbol `extern char s __asm__("X"); __declspec(selectany) void* const p __asm__("__imp_X") = &s;`. A reference to `__imp_X` pulls that object, which pulls the stub defining `X`. `selectany` lets two groups define the same inline/template symbol without a clash. One DLL, one log. |

Issue 1006's groups get the same treatment automatically (they are built by the same script). The `/Zl` objects
carry no CRT default-library records; the DLL's own link chooses the CRT. The generated pointers are harmless
for symbols nobody imports.

## Stub logger (`common/stublog.h`, `stublog.cpp`)

Contract as issue 1005: `STUB_CALL("<group>", "<demangled signature>")` first in every stub; the first call of
each stub is written at once (`FIRST seq= tid= t= group signature`), later calls only counted; `StubLog_Flush()`
writes a `COUNT` block and is also run at DLL unload / process exit; file from `CV64_STUBLOG`, else
`stubcalls.log` in the current directory, truncated at the first stub call of the process; SRW lock; Win32 and
CRT only. Extra: `StubLog_Path()`. Design points:

- Each call site is a function-local `static StubLogSite` with constant initialisers: no guard, no constructor,
  so stubs may run from static initialisers (and under the loader lock: only `CreateFileA`/`WriteFile`).
- FIRST lines go straight to the OS (`WriteFile`, no user-space buffer): they survive a crash or a
  `TerminateProcess`; only the counts need the exit flush.
- The exit flush is `atexit(ExitFlush)` registered from a `.CRT$XCB` initialiser, so it runs after every static
  destructor of the DLL. (Corrected 2026-11-10, issue 1009; replaced "the exit flusher lives in
  `#pragma init_seg(lib)`, so it is destroyed after the DLL's own statics": clang-cl 19.1.7 put that object's
  initialiser in `.CRT$XCU` [C llvm-objdump], so with the stub libraries last in the link it was destroyed first and
  the stubs called from `gGlobals`' destructor (`CvGlobals::uninit` -> `Database::Results::Release`,
  `CvDiplomacyAI` -> `Localization::String::~String`) came after the flush [L smoke run]. Now one COUNT block at
  the very end, `~String` 64 calls, `Release` 1 [L].)
- `StubLog_Flush()` is not exported from the DLL (the fork's `.def` is not ours). A host that needs counts
  before unload gets them by unloading the DLL (`FreeLibrary`) or at process exit.

## CvGameDatabase (46 symbols): real implementation in `tools/x64-dll/cvgamedatabase/`

> Corrected 2026-11-10 (issues 1402/1403, worker-db): the 46 logging stubs (`closedlib-stubs/CvGameDatabase/Database_stubs.cpp`,
> a static `CvGameDatabase.lib` returning neutral values) are deleted. Replaced by a real implementation over SQLite
> 3.7.17, built by `tools/x64-dll/cvgamedatabase/build_db.py` as the shared `CvGameDatabase_x64.dll` + import library
> `CvGameDatabase_x64.lib` (and `_x86` for the A/B): `build_db.py --out <stubs dir> --arch x64` puts the import library
> beside the other group libraries, and `build.py --stubs` links it like any `*.lib`. A DLL, not a static library,
> because the server opens the `Connection` and hands it to the game DLL (`SetGameDatabase`): one wrapper and one
> SQLite per process, as `lua51_x64.dll`. Behaviour, the x86 A/B against the closed library (0 unexplained
> differences over 832 tables, 45,602 rows, 539,368 cells) and every difference: `tools/x64-dll/cvgamedatabase/README.md`.
> Replaced: "Every stub logs and returns the neutral value below; `Step()` = false means every table the DLL caches
> comes out empty", and the "Real behaviour (as known)" column, several entries of which were wrong (see below).

Real library: `CvGameDatabaseWin32Final Release.dll`, a thin C++ wrapper over SQLite 3.7.17 (compile options
`THREADSAFE=1 TEMP_STORE=2 OMIT_TCL_VARIABLE OMIT_PROGRESS_CALLBACK OMIT_DEPRECATED OMIT_AUTOINIT OMIT_AUTHORIZATION`
`[C]` strings). Corrections to what this table said before, from a Ghidra decompile of a copy and the A/B `[C]`/`[L]`:
no prepared-statement cache (`AcquireCommand` just prepares, `ReleaseCommand` finalizes); `Step()` does not hash
column names (`ColumnPosition` hashes lazily; a repeated name keeps its last position); `Begin/EndTransaction` are
`SAVEPOINT _;` / `RELEASE _;`; `Release()` also forgets the column list; `GetText` of NULL returns NULL;
`Count()` returns -1 on error and reuses a remembered value whenever `sqlite3_changes()` is 0, whatever `bCache`
says; `TotalChanges()` returns `sqlite3_changes()`, not the total; `GetBool` is true for integers != 0 and text
starting with `t`/`T`, false for floats; `ColumnType` reports 6 (bool) for an integer in a column declared exactly
`boolean`. `D.h` = `CvGameDatabase/include/Database.h`, `R.h` = `DatabaseResults.h`.

| Symbol (demangled, `__declspec(dllimport)` omitted) | Header |
|---|---|
| `public: __cdecl Database::Connection::Connection(void)` | D.h:64 |
| `public: __cdecl Database::Connection::~Connection(void)` | D.h:66 |
| `public: bool __cdecl Database::Connection::Open(char const *, int)` | D.h:68 |
| `public: void __cdecl Database::Connection::Close(void)` | D.h:69 |
| `public: void __cdecl Database::Connection::BeginTransaction(void)` | D.h:72 |
| `public: void __cdecl Database::Connection::EndTransaction(void)` | D.h:73 |
| `public: void __cdecl Database::Connection::CommitTransaction(void)` | D.h:79 |
| `public: int __cdecl Database::Connection::Count(char const *, bool)` | D.h:86 |
| `public: void __cdecl Database::Connection::ClearCountCache(void)` | D.h:89 |
| `public: bool __cdecl Database::Connection::SelectAt(class Database::Results &, char const *, int)` | D.h:92 |
| `public: bool __cdecl Database::Connection::SelectAt(class Database::Results &, char const *, char const *, char const *)` | D.h:93 |
| `public: bool __cdecl Database::Connection::SelectAt(class Database::Results &, char const *, char const *, int)` | D.h:94 |
| `public: bool __cdecl Database::Connection::SelectAll(class Database::Results &, char const *)` | D.h:97 |
| `public: bool __cdecl Database::Connection::SelectWhere(class Database::Results &, char const *, char const *)` | D.h:99 |
| `public: bool __cdecl Database::Connection::Execute(class Database::Results &, char const *, int) const` | D.h:102 |
| `public: bool __cdecl Database::Connection::Execute(char const *, int) const` | D.h:103 |
| `public: void __cdecl Database::Connection::Analyze(void) const` | D.h:112 |
| `public: char const * __cdecl Database::Connection::CalculateMemoryStats(void)` | D.h:135 |
| `public: bool __cdecl Database::Connection::ValidateFKConstraints(bool) const` | D.h:138 |
| `public: char const * __cdecl Database::Connection::ErrorMessage(void) const` | D.h:148 |
| `public: __cdecl Database::Results::Results(char const *)` | R.h:22 |
| `public: virtual __cdecl Database::Results::~Results(void)` | R.h:24 |
| `public: static void * __cdecl Database::Results::operator new(unsigned __int64)` | R.h:110 |
| `public: static void __cdecl Database::Results::operator delete(void *)` | R.h:111 |
| `public: bool __cdecl Database::Results::Execute(void)` | R.h:35 |
| `public: bool __cdecl Database::Results::Step(void)` | R.h:39 |
| `public: bool __cdecl Database::Results::Reset(void)` | R.h:42 |
| `public: void __cdecl Database::Results::Release(void)` | R.h:47 |
| `public: bool __cdecl Database::Results::Bind(int, char const *, int, bool)` | R.h:53 |
| `public: bool __cdecl Database::Results::Bind(int, char const *, bool)` | R.h:54 |
| `public: bool __cdecl Database::Results::Bind(int, double)` | R.h:59 |
| `public: bool __cdecl Database::Results::Bind(int, float)` | R.h:60 |
| `public: bool __cdecl Database::Results::Bind(int, int)` | R.h:61 |
| `public: bool __cdecl Database::Results::BindNULL(int)` | R.h:63 |
| `public: bool __cdecl Database::Results::HasColumn(char const *)` | R.h:72 |
| `public: int __cdecl Database::Results::ColumnPosition(char const *)` | R.h:73 |
| `public: char const * __cdecl Database::Results::GetText(int)` | R.h:75 |
| `public: bool __cdecl Database::Results::GetBool(int)` | R.h:77 |
| `public: int __cdecl Database::Results::GetInt(int)` | R.h:78 |
| `public: float __cdecl Database::Results::GetFloat(int)` | R.h:80 |
| `public: char const * __cdecl Database::Results::GetText(char const *)` | R.h:91 |
| `public: bool __cdecl Database::Results::GetBool(char const *)` | R.h:93 |
| `public: int __cdecl Database::Results::GetInt(char const *)` | R.h:94 |
| `public: float __cdecl Database::Results::GetFloat(char const *)` | R.h:96 |
| `public: void __cdecl Database::Results::GetValue(char const *, int &)` | R.h:100 |
| `public: __cdecl Database::SingleResult::SingleResult(char const *)` | R.h:131 |

ABI note `[C]`: `Results` holds a `stdext::hash_map<std::string,int>` and `Connection` a `std::string`, two
`std::tr1::unordered_map`s and a `std::auto_ptr`. The DLL allocates both, so their layout is whatever the x64
STL makes of them; the replacement is built with the same STL (MSVC 14.4x, same defines per config, `/MD`) by
`build_db.py`.

## CvLocalization (16 symbols): real implementation in `tools/x64-dll/cvlocalization/`

> Corrected 2026-11-10 (issue 1703, worker-srv): the 16 logging stubs (`closedlib-stubs/CvLocalization/Localization_stubs.cpp`,
> a static `CvLocalization.lib` that kept the base text and dropped arguments) are deleted. Replaced by the real
> library of issue 1701, built by `tools/x64-dll/cvlocalization/build_loc.py` as the shared `CvLocalization_x64.dll`
> + import library `CvLocalization_x64.lib`; `build_all.py` runs it right after the `cvgamedatabase` step with
> `--out <stubs dir>`, so `build.py --stubs` links it like any `*.lib`, and the smoke host and the server load the DLL
> beside the game DLL. A DLL for the same reason as CvGameDatabase: the server initialises the dictionary
> (`Localization::Initialize`) and the game DLL looks text up in the same instance. Behaviour, the x86 A/B against
> the closed library and every difference: `tools/x64-dll/cvlocalization/README.md`. The text and table below are
> the history of the stub (the "Stub returns" column no longer applies). The self-test `_test/` covers lua51 only.
> Replaced, as the current state: "The stubs keep the base text ... no substitution, no forms."

Real library: `CvLocalizationWin32Final Release.dll`, text-key lookup in the database's language tables plus
ICU-based argument formatting (plural, gender, number, `{1_Name}` substitution). `Localization::String` is a value
type the DLL builds on its own stack (layout from the header: two `SharedString`s, two vectors, a language
pointer), so the special members must really work. The stubs keep the base text: `String("abc")` reads back
`"abc"`, `Lookup("TXT_KEY_X")` returns `"TXT_KEY_X"` (what a missing key shows), arguments are logged and
dropped, no substitution, no forms. `SharedString` (not imported) is implemented for real because `String`'s
members use it. Header: `L.h` = `CvLocalization/include/CvLocalization.h`.

| Symbol | Header | Real behaviour | Stub returns | Repl. |
|---|---|---|---|---|
| `class Localization::String __cdecl Localization::Lookup(char const *)` | L.h:158 | dictionary lookup, current language with fallback | String holding the key | 40 |
| `public: __cdecl Localization::String::String(void)` | L.h:287 | empty | empty | 2 |
| `public: __cdecl Localization::String::String(char const *)` | L.h:288 | base string, parsed lazily | holds the text | 5 |
| `public: __cdecl Localization::String::String(class Localization::String const &)` | L.h:290 | copies base, args, forms | copies base text | 5 |
| `public: virtual __cdecl Localization::String::~String(void)` | L.h:292 | - | - | 1 |
| `public: bool __cdecl Localization::String::IsEmpty(void) const` | L.h:297 | base string empty | length == 0 | 2 |
| `public: class Localization::String & __cdecl Localization::String::operator=(class Localization::String const &)` | L.h:301 | copy | copies base text | 5 |
| `public: class Localization::String & __cdecl Localization::String::operator=(char const *)` | L.h:302 | new base string, args cleared | new base text | 5 |
| `public: char const * __cdecl Localization::String::toUTF8(void)` | L.h:309 | composes form 1 (args, plural, gender), cached null-terminated | base text | 10 |
| `public: char const * __cdecl Localization::String::toUTF8(unsigned __int64 &, int)` | L.h:310 | composes form n, returns byte count | base text and its length | 10 |
| `public: bool __cdecl Localization::String::PushArgument(char const *)` | L.h:335 | appends string argument | false | 3 |
| `public: bool __cdecl Localization::String::PushArgument(char const *, unsigned __int64)` | L.h:336 | appends string argument (length) | false | 3 |
| `public: bool __cdecl Localization::String::PushArgument(int)` | L.h:337 | appends int argument | false | 3 |
| `public: bool __cdecl Localization::String::PushArgument(float)` | L.h:339 | appends float argument | false | 3 |
| `public: bool __cdecl Localization::String::PushArgument(class Localization::String const &)` | L.h:341 | appends nested localized string (gender/plural source) | false | 3 |
| `public: static class Localization::String Localization::String::Empty` | L.h:429 | static empty string | static empty string (its constructor/destructor not logged) | 1 |

The DLL never checks `PushArgument`'s result (grep, 2026-11-10). Replacement `[I]`: the 16 entry points are ~100
lines; the work is behind them: the `{n_Tag}` parser and composer, plural rules per language, gender/noun-trait
forms, number formatting, the dictionary over the database (`InitializeDictionary`) and its language fallback.
Without ICU 1k-1.5k lines; the census's 1k-2.5k stands. Text only reaches the UI and logs, never game state
(`[I]`, so it does not touch D10 save identity), which makes this the safest group to leave stubbed longest.

## lua51 (46 symbols) `lua51/` + real Lua 5.1.4

### The 43 Lua API functions: real, stock 5.1.4

`lua_checkstack` lua.h:137, `lua_cpcall` :214, `lua_createtable` :192, `lua_gc` :241, `lua_getfield` :189,
`lua_getstack` :344 (debug API), `lua_gettable` :188, `lua_gettop` :131, `lua_isnumber` :146, `lua_newuserdata`
:193, `lua_next` :250, `lua_objlen` :161, `lua_pcall` :213, `lua_pushboolean` :180, `lua_pushcclosure` :179,
`lua_pushinteger` :173, `lua_pushlightuserdata` :181, `lua_pushnil` :171, `lua_pushnumber` :172, `lua_pushstring`
:175, `lua_pushvalue` :133, `lua_rawget` :190, `lua_rawgeti` :191, `lua_rawset` :203, `lua_rawseti` :204,
`lua_replace` :136, `lua_setfield` :202, `lua_setmetatable` :205, `lua_settable` :201, `lua_settop` :132,
`lua_toboolean` :159, `lua_tointeger` :158, `lua_tolstring` :160, `lua_tonumber` :157, `lua_touserdata` :163,
`lua_type` :150, `lua_typename` :151; `luaL_checkinteger` lauxlib.h:57, `luaL_checklstring` :50, `luaL_checktype`
:62, `luaL_error` :69, `luaL_loadstring` :80, `luaL_optinteger` :58 (all in `ThirdPartyLibs/Lua51/include`).
Replacement size: 0 lines (stock source). Since issue 1009 (2026-11-10) the core and standard libraries (all of
`src/` except `lua.c`, `luac.c`, `print.c`) are built as the shared `lua51_x64.dll` (`LUA_BUILD_AS_DLL`, 123 exports
including all `luaopen_*` and `luaL_openlibs`) plus the import library `lua51_x64.lib`, which the DLL links; `lua51.lib`
keeps only the FLua members. `build_stubs.py --lua static` restores the issue-1005 form (Lua inside `lua51.lib`).

Source: https://www.lua.org/ftp/lua-5.1.4.tar.gz, 216 679 bytes, SHA-256
`b038e225eaf2a5b57c9bcc35cd13aa8c6c8288ef493d52970c9545074098af3a` as published on https://www.lua.org/ftp/
(checked 2026-11-10). `tools/x64-dll/lua51/fetch.py` downloads it into `tools/x64-dll/lua51/_download/`
(git-ignored), verifies the hash and unpacks with tarfile's `data` filter. Python's own CA bundle (Anaconda)
failed with "certificate has expired" and lua.org connections time out intermittently from this machine, so
fetch.py retries and alternates urllib with Windows' `curl.exe` (both verify certificates; the SHA-256 is what
makes the file trusted).

### The game's Lua is stock 5.1.4, compiled as C `[C]`

`lua51_Win32.dll` in the Steam install (SHA-256 `f58ceffe...a58c0e35`, identical to `lua51_Win32.dll.stock`, read
2026-11-10): contains `$Lua: Lua 5.1.4 Copyright (C) 1994-2008 Lua.org, PUC-Rio $`; imports `longjmp` from
MSVCR90 (C error handling, not C++ exceptions); `LUA_CPATH_DEFAULT` string `.\?.dll;!\?.dll;!\loadall.dll` (stock).
Its `luaB_ipairs` (0x10004f30) is stock: check table, push `ipairsaux`, the table and 0, return 3. So we compile
Lua as C (`/TC`) from stock sources with the stock `luaconf.h`.

### Differences from the game's Lua, and what this build does about them

| # | Difference | x64 build | Status |
|---|---|---|---|
| 1 | **Number to integer.** The game's `lua_tointeger` (0x10001610) is `fldl; fistpl`: x87 conversion under the current control word, i.e. round half to even (1.5 -> 2, 2.5 -> 2, 1.7 -> 2, -1.5 -> -2), out of range or NaN -> 0x80000000. Stock `luaconf.h` on x64 takes the generic branch, `(int)(d)` / `(lua_Integer)(d)`: truncation (1.7 -> 1). Affects `lua_tointeger`, `luaL_checkinteger`, `luaL_optinteger`, `luaL_checkint` (so every DLL Lua binding taking an int, and `string.sub`, `string.rep`, `table.insert` positions) for non-integral numbers. The fork's `luaconf.h:557-563` shows the x86 branch; issue 1002 raised the same point. | `build_stubs.py` patches a copy of `luaconf.h`: on `_M_X64`, `lua_number2int(i,d)` = `_mm_cvtsd_si32(_mm_set_sd(d))` and `lua_number2integer` the same widened to `lua_Integer`. `cvtsd2si` under the default MXCSR rounds half to even and returns 0x80000000 on overflow/NaN: the game's results bit for bit. It also keeps `lua_Integer` values in int32 range, as the DLL's `int x = lua_tointeger(...)` sites (part of the 794 truncation sites) assume. | `[C]` disassembly; `[L]` self-test: 1.5, 2.5, -1.5, 1.7, -1.7, 0.49999999999999994, 3e9, -3e9 give 2, 2, -2, 2, -2, 0, INT_MIN, INT_MIN; `string.sub('abcdef', 1.7, 3.7)` = `bcd` (truncation would give `abc`). Assumes the EXE leaves the x87 rounding mode at nearest `[I]`. |
| 2 | `lua_Integer` is `ptrdiff_t`: 32-bit in the game, 64-bit here. | ABI follows the header; values stay in int32 range by #1. `lua_pushinteger` of a value above 2^31 now pushes it exactly (the game would have received a truncated int from the DLL first). | `[C]` header |
| 3 | `ipairs` visits index 0 and `debug` is hidden (engine-contract.md 10.5). | Not in the Lua library: the game's `luaB_ipairs` is stock, so the EXE installs the index-0 `ipairs` and hides `debug` itself. The server must do both in every state it creates, as the headless host does (`InstallEngineIpairs`, `HideDebugLibrary`). Nothing for this library to change. | `[C]` disassembly of `lua51_Win32.dll` |
| 4 | Arithmetic: the game's Lua runs on x87, x64 on SSE2. | Lua numbers are doubles; with the x87 precision control at 53 bits, `+ - * /` round identically except for double rounding in the subnormal range. If the EXE sets 24-bit precision (D3D9-style), results differ more. | `[I]`; the EXE's control word is not measured |
| 5 | C runtime: MSVCR90 vs UCRT. | `tostring`/`..` use `sprintf("%.14g")`, number literals and `tonumber` use `strtod`, `math.*` uses the CRT's `pow`, `fmod`, `sin`, `exp`, ... UCRT's `strtod`/`printf` are correctly rounded, MSVCR90's not always; transcendental functions may differ in the last ulp. `math.random` uses `rand()`, the same LCG in both CRTs. | `[I]`, not measured |
| 6 | Memory sizes: pointers double the size of most Lua objects. | `collectgarbage("count")` reads higher and the incremental GC runs at different moments, so `__gc` finalisers and weak-table clearing happen at different times. Game logic should not depend on either. | `[I]` |
| 7 | `LUA_CPATH_DEFAULT`: the fork's `luaconf.h` (LuaForWindows-style, adds `?51.dll` and `clibs\`) differs from stock and from the game's binary. | The build uses stock `luaconf.h` (matches the game). Only `require` of C modules is affected; the DLL does not use it. The fork's other change, `LUA_NORETURN` on `lua_error`/`luaL_error`, is declaration-only. | `[C]` diff 2026-11-10 |

### One Lua per process `[C]`

Lua 5.1 keeps two address-identity statics: `luaO_nilobject_` (lobject.c:27) and `dummynode_` (ltable.c:75), the
shared empty hash part of every table without one. `luaH_resize`/`luaH_free` free a table's hash part unless it
`== dummynode`, so a table created by one copy of the Lua code and resized or freed by another copy frees the
other copy's static: heap corruption. Today the EXE and the DLL share one `lua51_Win32.dll`. **Resolved 2026-11-10
(issue 1009, coordinator decision):** the x64 DLL imports `lua51_x64.dll` and the server (and the smoke host) load the
same DLL, so there is one Lua copy per process; the smoke host checks that `GetModuleHandle("lua51_x64.dll")` after
the DLL load is the module it loaded itself [L]. (Before: "With `lua51.lib` linked statically into the x64 DLL, the
server must not run its own Lua on states the DLL touches"; the static DLL also linked no `luaopen_*`, so nobody
could open the standard libraries through its copy [C map].)

### The three FLua members the probe filed under lua51 `lua51/FLua_luastate.cpp`

The probe's grouping tests `\blua_` before `FLua`, and these signatures contain `struct lua_State`:

| Symbol | Header | Implementation | Repl. |
|---|---|---|---|
| `bool __cdecl FLua::Details::CCallWithErrorHandling(struct lua_State *, int (__cdecl *)(struct lua_State *), void *)` | `FirePlace/include/FireWorks/FLua/include/FLuaCommon.h:790` | real: `lua_cpcall`; on error report through `FLua::Details::Error("%s", msg)` (1006's) and pop, per the header's comment at :243 | 12 |
| `public: __cdecl FLua::Value::Value(struct lua_State *, int)` | `.../FLuaTypes.h:59` | real `[I]`: `luaL_ref` of a copy in `LUA_REGISTRYINDEX`; nil -> `LUA_NOREF` (so `Valid()` is false) | 8 |
| `public: void __cdecl FLua::Value::Set(struct lua_State *, int)` | `.../FLuaTypes.h:66` | real `[I]`: new ref first, then `luaL_unref` the old one | 8 |

They touch only `Value`'s two data members and never call the private `Value::Unref()`, so **issue 1006 must not
define these three, and may define `Unref`, `Value()`, `~Value()`, `Push()` freely**. The real reference scheme is
inferred from `Valid()` and `GetRef()`; whether nil is stored as `LUA_REFNIL` is unknown.

## Files

| Path (under `tools/x64-dll/`) | What |
|---|---|
| `closedlib-stubs/build_stubs.py` | builds every group folder into `<out>/<group>.lib` |
| `closedlib-stubs/common/stublog.h`, `stublog.cpp` | the logger (contract above) |
| `cvgamedatabase/` | the real CvGameDatabase (2026-11-10, issues 1402/1403; replaced `closedlib-stubs/CvGameDatabase/Database_stubs.cpp`, 46 stubs + `GetColumns`, deleted) |
| `cvlocalization/` | the real CvLocalization (2026-11-10, issues 1701/1703; replaced `closedlib-stubs/CvLocalization/Localization_stubs.cpp`, 16 stubs + `SharedString`, `~SingleFormString`, deleted) |
| `closedlib-stubs/lua51/FLua_luastate.cpp` | the 3 FLua members |
| `closedlib-stubs/_test/stub_selftest.cpp`, `run_selftest.py` | self-test (not a group: `_` prefix) |
| `lua51/fetch.py`, `lua51/.gitignore` | fetch and verify the Lua tarball; keep it out of git |
| `evidence/2026-11-10-1005/` | probe-link summary and remaining 228, self-test log, the adapted link script (paths point at the 2026-11-10 session scratchpad) |
