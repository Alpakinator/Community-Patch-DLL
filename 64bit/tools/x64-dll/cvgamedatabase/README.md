# CvGameDatabase replacement (issues 1401-1403)

A real implementation of the closed `CvGameDatabase` library (`Database::Connection`, `Results`, `SingleResult`)
over SQLite 3.7.17, built as a shared DLL plus import library for x64 (the game DLL and the server) and x86 (the A/B
against the closed library). It replaces the 46 logging stubs of issue 1005 (`closedlib-stubs/CvGameDatabase/`,
deleted 2026-11-10). Written 2026-11-10 by worker-db, team 1400. Confidence: `[C]` confirmed by decompile, dump or
build; `[L]` verified by running (here: the A/B); `[I]` inferred.

## Result

- **x86 A/B against the closed library: PASS** `[L]` (2026-11-10, release build, `ab/run_ab.py`). Same database,
  same query program, linked once against the closed `CvGameDatabaseWin32.lib` + `CvGameDatabaseWin32Final Release.dll`
  and once against ours: 59,599 output lines each, 832 tables dumped completely (45,602 rows, 539,368 cells, each with
  column type, text, int, int64, float bits, double bits and bool, plus the by-name getters and every `GetValue`
  overload), plus every error path below. **2 differing lines, both explained** (the `SelectAt(double)` bug of the
  closed library, below); 0 unexplained. The memory-statistics text differs in its numbers only (same layout).
- **x86 vs x64 of our library: identical** `[L]` (0 differing lines, memory statistics excepted).
- **Export names** `[C]`: our x86 DLL exports 104 names, every one present in the closed import library with the same
  decoration (incl. the default-constructor closures `??_FResults`, `??_FSingleResult`); the closed library's other 31
  are `BinaryIO`, `ResultsCache`, `XMLSerializer` and `Database::Scripting::Lua` (out of scope: the game DLL imports
  none of them). The closed import library and the closed DLL export the same 135 names. Ours exports no `sqlite3_*`.
- Game DLL link: 0 unresolved, 46 imports from `CvGameDatabase_x64.dll` (below).

## Build and use

```
python tools/x64-dll/cvgamedatabase/build_db.py --out DIR [--arch x64|x86] [--config debug|release]
                                                [--src FORK] [--unlocked] [--sqlite-zip PATH]
python tools/x64-dll/cvgamedatabase/ab/run_ab.py --out DIR --src FORK [--config release|debug] [--skip-build]
```

`--src` is checked against `deps/dll.lock` like every `--src` here (`tools/x64-dll/build.py` `resolve_src`, issue
1508): a git checkout at the locked commit without tracked changes or a plain copy whose fingerprint matches;
the default (the DLL source setting of `tools/x64-dll/dll_source.py`: `VP_DLL_SRC`, else `src =` in
`deps/dll-source.ini`, else the upstream layout) only while it is at that commit.
`run_ab.py` passes the checked `--src` (and `--unlocked`) to both `build_db.py` runs.

`build_db.py` writes `DIR/CvGameDatabase_<arch>.dll`, `.lib` (import library), `.pdb`, `.map`, `.exports.txt`,
`build_db_<arch>.log`; objects in `DIR/obj-db-<arch>-<config>/`, the amalgamation unpacked to `DIR/sqlite-3.7.17/`
(never into the repository). The zip comes from `tools/x64-dll/thirdparty.py` (`deps/third-party.lock`): `--sqlite-zip`,
else the cache root, else a download; wrong hash or missing offline -> exit 2 with the URL, hash and path (issue
1401, `evidence/2026-11-10-1403/deps-checks-1401.txt`). About 15-35 s.

For the game DLL: `build_db.py --out <stubs dir> --arch x64 --config <same as the DLL>`; `tools/x64-dll/build.py
--stubs <stubs dir>` links every `*.lib` there, so it picks up `CvGameDatabase_x64.lib`. At run time
`CvGameDatabase_x64.dll` must be beside the game DLL (or on the loader path), like `lua51_x64.dll`. The config must
match the DLL's (VPDEBUG changes layouts; the defines are copied from `closedlib-stubs/build_stubs.py`).

Why a DLL: the server opens the `Connection` and hands it to the game DLL through `SetGameDatabase`; both must use one
copy of the wrapper and one SQLite (one heap, one mutex set) per process. The classes' members are
`__declspec(dllexport)` (`CvGameDatabaseAPI` defined on the command line), so the export names are the ones the game
DLL's `__imp_` references and the closed import library carry.

Toolchain `[C]`: clang-cl 19.1.7, MSVC 14.43 STL/CRT (vcvars64/vcvars32), SDK 10.0.26100.0, `/std:c++14 /MD`,
`/W4 /WX` for our sources (the fork's headers and `sqlite3.h` are included as system headers, `/imsvc`, because
`Database.h` declares an unused `static void Error(const char*)`), lld-link. SQLite compiled as C inside the DLL.

## SQLite version and compile options

Version 3.7.17 (2013-05-20, source id `118a3b35693b134d56ebd780123b7fd6f1497668`), as the closed library `[C]`
(strings of `CvGameDatabaseWin32Final Release.dll`, SHA-256 `f9563739...6949563`). Its compile-option list
(`sqlite_compileoption_get` table) is `THREADSAFE=1`, `TEMP_STORE=2`, `OMIT_TCL_VARIABLE`, `OMIT_PROGRESS_CALLBACK`,
`OMIT_DEPRECATED`, `OMIT_AUTOINIT`, `OMIT_AUTHORIZATION` `[C]`; `build_db.py` uses exactly these. Effects: temp tables
in memory; `sqlite3_initialize()` must be called explicitly (done in the `Connection` constructors, as the closed
library); the omitted APIs are not used.

Not reproduced (decision, nothing observable but `CalculateMemoryStats`): the closed library's
`InitMemoryManagement` installs its own allocator (`SQLITE_CONFIG_MALLOC`), a static page cache of 5000 x 1168 B
(`ms_pPageCacheBuffer`), a 7008-byte scratch buffer and turns lookaside off (`sqlite3_config` 4, 7, 6, 13) `[C]`.
Ours uses SQLite's defaults; the two static buffers are not defined.

## What each member does (as implemented = as the closed library, `[C]` decompile + `[L]` A/B)

Connection
- Constructors: `m_db = NULL`; `sqlite3_initialize()` once; `(file, flags)` then `Open`.
- `Open(file, flags)`: `NULL` file -> `":memory:"`; `sqlite3_open_v2(file, &m_db, flags)`; on failure logs the error
  and returns false, leaving `m_db` as SQLite set it (a handle carrying the message, or NULL for invalid flags such as
  0 or `OPEN_CREATE` alone); does not close a previous handle. Installs a busy handler: logs "Database is busy.
  Sleeping for 2 ms... (%i) attempt(s).", `Sleep(2)`, gives up after 30 attempts.
- `Close()`: clears both count maps, finalizes every statement of the connection (`sqlite3_next_stmt` loop, also
  those held by live `Results`), `sqlite3_close`; `m_db = NULL` on success. Destructor: `Close()` if open, deletes
  the logger.
- `BeginTransaction`/`EndTransaction` = `SAVEPOINT _;` / `RELEASE _;` (nestable); `RollbackTransaction` = `ROLLBACK;`;
  `BeginDeferred/Immediate/ExclusiveTransaction` = `BEGIN ... TRANSACTION`; `CommitTransaction` = `COMMIT`;
  `SetSavePoint(n)` / `RollbackToSavePoint(n)` = `SAVEPOINT n` / `ROLLBACK TO n`. All through `Execute(sql)`.
- `Count(table, bCache)`: if a value is remembered for `table` and `sqlite3_changes()` is 0, returns it (whatever
  `bCache` says); else prepares `select count(*) from <table>` (unquoted; kept in a map if `bCache`) or resets the
  kept one, steps, remembers and returns the value; -1 on any error. An uncached statement whose step fails is left
  for `Close()` (as the closed library).
- `ClearCountCache()`: finalizes the kept statements, clears both maps.
- `SelectAt(R, table, rowid)`: `select <cols> from <table> where ROWID = <id> LIMIT 1`; `SelectAt(R, t, col, text)`
  `... where <col> = '<quoted>' [LIMIT 1]`; `(R, t, col, int)` `... = <int> [LIMIT 1]`; `SelectAll` `select <cols>
  from <t> [LIMIT 1]`; `SelectWhere` `... where <cond> [LIMIT 1]`. `<cols>` = `R.GetColumns()` (default `*`); the
  table goes through `%q`, column and condition verbatim; `LIMIT 1` only for a `SingleQuery` result; formatted with
  `sqlite3_snprintf` into 1024 bytes. Then as `Execute(R, sql)`.
- `Execute(R, sql, len)`: prepares into a fresh command (`AcquireCommand`: on failure logs the SQLite error and
  "In Query - <sql>"; an empty statement fails with only the second message) and attaches it to `R` (`SetCommand`:
  finalizes `R`'s previous statement, clears its column-name hash; for a `SingleQuery` result steps once and returns
  whether a row came). There is no statement cache.
- `Execute(sql, len)`: prepare, step until not ROW, finalize; true only for DONE; empty statement -> false, no log.
- `ExecuteMultiple(sql, len)`: each statement in turn as above; stops at the first failure; true at the end of text
  (a lone `;` also ends it).
- `TotalChanges()`: **`sqlite3_changes()`**, not the total (the closed library reads `sqlite3::nChange`, offset 0x50
  of the 3.7.17 x86 struct `[C]`; the first A/B run showed 0 vs 4 `[L]`); 0 when closed.
- `Analyze()` / `Vacuum()`: `sqlite3_exec("ANALYZE"/"VACUUM")` when open, error logged.
- `StatementCount()` / `StatementSQL(i)`: walk `sqlite3_next_stmt`.
- `SetBusyTimeout(ms)`: `sqlite3_busy_timeout` (replaces the busy handler).
- `SetLogger` (owns the logger, `auto_ptr`), `LogMessage/Warning/Error` -> `Log(msg, 0/1/2)` when a logger is set.
- `CalculateMemoryStats()`: the same text layout as the closed library from `sqlite3_status` / `sqlite3_db_status`
  (memory used, page cache, lookaside, scratch, overflows, largest allocations, prepared statements), stored in
  `m_strMemoryStats`, logged as a message, returned. Numbers differ (allocator and buffers not reproduced).
- `ValidateFKConstraints(bAllowNULL)`: for every table, `pragma foreign_key_list`, then
  `select "<from>" from <t> where <from> not null and <from> not in (select "<to>" from <parent>);`; logs each
  violation as an error; true if none. `bAllowNULL` is ignored (also by the closed library). On the VP 5.4.8 database:
  true.
- `ErrorCode()` / `ErrorMessage()`: `sqlite3_errcode` / `sqlite3_errmsg`; -1 / NULL when `m_db` is NULL.

Results / SingleResult
- `Results(cols)`: copies `cols` (`sqlite3_mprintf`); `SingleResult(cols)` the same with `SingleQuery(true)`.
  Copy constructor: same columns, hash, connection, and a new statement prepared from the source's SQL.
- `GetColumns()` (virtual): the copy or `"*"`. `SetColumns`. `Release()`: finalizes the statement **and forgets the
  column list** (a released result selects `*` next time). Destructor = `Release()`.
- `Step()`: `sqlite3_step` (also on a NULL statement: a harmless MISUSE), error logged with "While executing - '<sql>'";
  true on ROW. `Execute()`: step, true on DONE; a returned row is a logged warning. `TryExecute()`: also true on
  ABORT and CONSTRAINT. `Reset()`: `sqlite3_reset` then `sqlite3_clear_bindings`; false if the reset reports the last
  step's error. `TryReset()`: ignores the reset's result.
- `Bind(i, text, len, copy)`: `bind_text`, TRANSIENT if `copy` else STATIC; wide overloads `bind_text16` (length x 2);
  `double`, `float` (widened), `int`, `__int64`, `BindNULL`. True on SQLITE_OK.
- `ColumnCount`, `ColumnName`; `ColumnType(i)`: SQLite's type, or 6 (`COLTYPE_BOOL`) for an integer in a column
  declared exactly `boolean` (case-sensitive), 5 when there is no statement; `ColumnTypeName`: "integer", "float",
  "text", "null", "boolean", NULL for blob.
- `ColumnPosition(name)`: hashes all column names on first use after each new statement (a repeated name keeps its
  last position; case-sensitive); -1 if absent. `HasColumn` = position != -1. By-name getters use position -1 for an
  unknown name, i.e. SQLite's out-of-range result (NULL text, 0).
- `GetText` (NULL for a NULL value), `GetText16`, `GetInt` (`column_int`), `GetInt64`, `GetFloat` (`(float)
  column_double`), `GetDouble`; `GetBool`: integer or bool type -> value != 0; text -> first character `t` or `T`;
  float, NULL, blob -> false. `GetValue(i, bool&)` is `column_int != 0` (not `GetBool`). All return 0/NULL without a
  statement.
- `operator new/delete`: `malloc`/`free` of the CRT this DLL uses (`/MD`, the process's shared UCRT).

## Differences from the closed library

| # | Where | Closed library | Ours | Confidence |
|---|---|---|---|---|
| 1 | `SelectAt(R, t, col, double)` | formats the double with `%d`: the low 32 bits become the number and the high 32 bits are consumed by the next `%s` (the LIMIT clause) as a pointer: a crash for most values, e.g. 2.0 (0x40000000); for 0.0 it gives `= 0 ` without LIMIT | formats with `%!.15g` (`= 0.0 [LIMIT 1]`) | `[C]` decompile, `[L]` A/B with 0.0 (same rows; the logged SQL differs: the 2 explained A/B lines). No caller in the game DLL. |
| 2 | `CalculateMemoryStats` | numbers from its own allocator, page cache and scratch buffers | SQLite defaults | `[L]` same layout, different numbers |
| 3 | message buffers | `vsprintf_s` into 256/512/1024 bytes: an over-long message calls the invalid-parameter handler (crash) | truncates | `[C]`, not exercised |
| 4 | `InitMemoryManagement` | `sqlite3_config` before each initialize (flag never set) | initialize once | `[C]`, nothing observable |
| 5 | `Count`, `ClearCountCache`, `Close` (issue 0165, 2026-11-14) | no lock found in the 1403 decompile notes [I] | one process-wide recursive lock (`CountLock`) around each: the kept count statements and the two maps are shared by the server's channel threads; without it two threads that `Count` one table share one `sqlite3_stmt` and the server crashed in `sqlite3_column_int` (research/03-dll-engine-boundary/proxy-design.md 20a) | `[L]` `tests/run_count_threads.py`: before the fix 5 of 5 rounds 0xC0000005 at the game dump's site, after 5 of 5 pass (2026-11-14) |

Behaviours that look like bugs but are reproduced on purpose (the DLL may depend on them): `TotalChanges` =
`sqlite3_changes`; `Count` reusing a remembered value whenever the last change count is 0, regardless of `bCache`;
`Release` forgetting the columns; `Close` finalizing statements of live `Results`; `ValidateFKConstraints` ignoring
its argument; `Open` not closing an earlier handle.

## The A/B (`ab/`)

- `ab/dbquery.cpp`: one program, compiled once per architecture (`/W4 /WX`), linked against either library.
  Layout-agnostic: objects are placement-new'd into 4 KB aligned buffers by the library's exported constructors,
  destroyed by the explicit (virtual) destructor, only exported members are called, `sizeof` of the library classes
  is never used (the closed DLL is built with the VC9 STL). A test logger (`IDatabaseLogger`) puts every logged
  message into the output. Sections: `Open` on missing files with flags 0, READONLY, READWRITE, RW|CREATE,
  RW|CREATE|FULLMUTEX, CREATE; the opening constructor; an unopened connection; every table of `sqlite_master`
  (`Count` cached twice and uncached, `SelectAll`, every row and column with `ColumnName`, `ColumnTypeName`,
  `ColumnType`, `GetText`, `GetInt`, `GetInt64`, `GetFloat`/`GetDouble` bits, `GetBool`; the by-name getters compared
  with the by-index ones; all 14 `GetValue` overloads and `GetText16` on the first two rows, with sentinels);
  `SelectAt` (rowid -1..2, `ID` int, `ID` double 0.0, `Type` text with a column list, a bad column) and
  `SelectWhere` (good and bad) on five tables and a missing one, each as `Results` and `SingleResult`;
  `SetColumns`/`GetColumns`/`SingleQuery`; every `Bind` overload incl. wide strings and out-of-range indexes, `Reset`
  (bindings cleared), `TryReset`, use after `Release`; the game's Bind/Step/Reset loop; type conversions (NULL,
  numeric text, `0x10`, `true`, large and negative integers, 2.5/3.5/-2.5, 1e300, -0.0, blobs, duplicate and
  mixed-case column names, missing names, out-of-range indexes, declared types INTEGER/TEXT/REAL/BOOLEAN/VARIANT/
  NUMERIC); error paths (missing table, bad SQL, missing column, empty and blank SQL, two statements, a failing
  INSERT; `Results::Execute/TryExecute`, `ExecuteMultiple`, explicit length); writes in transactions (savepoint
  transaction with a prepared INSERT, UNIQUE violation through `Execute` and `TryExecute`, `ROLLBACK`, deferred,
  immediate with savepoints, exclusive, `RELEASE` and `COMMIT` without a transaction, `ExecuteMultiple`, `Count` cache
  before and after a change and after `ClearCountCache`, `sqlite_sequence`); `ValidateFKConstraints(true/false)`;
  `ANALYZE` then all of `sqlite_stat1`; `StatementCount`/`StatementSQL`; `CalculateMemoryStats`; `Vacuum`; `Close`.
- `ab/run_ab.py --out DIR`: builds both architectures, copies the closed DLL (SHA-256 checked) and its
  `lua51_Win32.dll` from the headless toolkit's game copy into `DIR/run-closed` (the source folder is only read; its
  MSVCR90/MSVCP90 come from WinSxS), compares export names, links the three programs, runs each on a fresh writable
  copy of the database, compares closed-x86 with ours-x86 and ours-x86 with ours-x64. Lines starting with `~` (memory
  statistics) are compared with numbers masked. Every other difference must match the `EXPLAINED` table in
  `run_ab.py`; exit 0 = PASS, 1 = an unexplained difference, 2 = setup error. About 1 min including both builds.
- Database: `%LOCALAPPDATA%\Temp\claude\cv64-deps\testdb\vp548-cdrift-Civ5DebugDatabase.db` (VP 5.4.8,
  SHA-256 `bd9b8aa1...addfadd6f`, read-only master; a copy is used per run).
- Evidence: `tools/x64-dll/evidence/2026-11-10-1403/` (`summary.txt`, logs, hashes, heads of the outputs).

Pitfall found on the way (2026-11-10): copying the read-only master with `shutil.copy2` keeps the read-only
attribute, and every write then fails with "attempt to write a readonly database" in both libraries alike, so the
A/B still "passes" while testing nothing. `run_ab.py` uses `copyfile`.

## Game DLL link

`[C]` 2026-11-10 04:29, debug, fork x64 @ e9d0c4837: `build_stubs.py --out S` (no CvGameDatabase group any more),
`build_db.py --out S --arch x64`, `build.py --config debug --stubs S --no-commit-id`: 173/173 compiled, lld-link rc=0,
**0 undefined-symbol reports**; the game DLL imports exactly 46 symbols from `CvGameDatabase_x64.dll`
(`evidence/2026-11-10-1403/gamedll-link-debug.txt`). Not yet run inside the game DLL (issue 1404, smoke host with the
real database). Layout parity of `Connection`/`Results` between the game DLL and this DLL rests on identical headers,
compiler, STL and defines `[I]`; the smoke host is the live check.

## Files

| Path | What |
|---|---|
| `build_db.py` | builds the DLL + import library for x64/x86 |
| `src/Database.cpp` | the implementation (Connection, Results, SingleResult) |
| `ab/dbquery.cpp` | the A/B and self-test query program |
| `ab/run_ab.py` | the one-command A/B and x64 self-test |
| `tests/count_threads.cpp` | issue 0165: N threads `Count("Months")` on one `Connection` (crashes without the lock) |
| `tests/run_count_threads.py` | builds and runs it against a built library: `--lib-dir DIR --src FORK --out DIR [--threads 4 --ms 2000 --rounds 5] [--expect pass\|fail]`; exit 0 when the expectation holds |

Licence note: written from the fork's headers and a decompile of the closed DLL; no code from civ5-headless
(GPL-3.0, tracker question 0076).
