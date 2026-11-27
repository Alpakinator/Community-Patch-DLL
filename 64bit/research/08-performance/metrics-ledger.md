# The metrics ledger: run records, the registry, the DuckDB index, the profile kit, the owner breakdown, the page (epic 0330, D145)

Kept by epic 0330 (lead-0330) until the epic merges, then by the performance standards team (7000). The design is the
Architect's proposal `research/07-architecture-decisions/architect-2026-11-15-metrics-ledger-epic.md` (sections 4, 5
and 9); the brief is `docs/briefs/epic-0330-metrics-ledger.md`. Code: `perf/ledger/`, `perf/profilekit/`. Store:
`F:/c5-64bit-work/ledger/` (D141). Marks: `[C]` read in code or records, `[L]` verified live, `[I]` inferred.

## 1. Premises M1-M6 (proposal section 5)

| premise | state | evidence |
|---|---|---|
| M1 `perf/memsampler` works on a live game | **yes, also on an AI-autoplay game** with `--autosave-kind post` (below) | 2026-11-15 05:21, [L] |
| M2 load phases of the product path readable from existing logs | **yes for stock and for inc1's proxy route, except the save write and anything of the server renderer** (below) | 2026-11-15 05:25, [L] stock, [C] proxy route |
| M3 an address-space walk reproduces the hand-made owner totals on stock | **partly** (4204): every owner a walk can see (images, DLL image and minidump reserve, the 128 MB reservation, heap 10, sections, the large-block mass) within 5 MB or 5 %; the DLL's heap share (236 MB at t344) not without the DLL's hook, which the pin lacks; the t344 totals unchecked, no GameId 62 save exists (section 5.5) | 2026-11-15 05:40-05:52, section 5 |
| M4 UI latency through the Lua channel without the channel dominating | **no end to end (the channel's round trip is ~120 ms); yes with an in-game frame probe; no for tooltips** (below) | 2026-11-15 05:24, [L] |
| M5 DuckDB installs from PyPI into a venv on the workstation and reads the DLL's `stats.db` in place | **install yes; read in place no** (below) | 2026-11-15 05:00 |
| M6 run folders come back within `tb.py fetch`'s limits | **yes**: a 3-hour memsampler-shaped run (5,400 rows x 11 columns + 270 events) is 495 kB, its largest file 479 kB against the 5 MB per-file limit; fetched from the laptop with its seal intact, validated, ingested (section 3.7) | 2026-11-15 06:52-06:55, [L], laptop job 20261115-065240-917b |

**M5 [L, 2026-11-15 04:58-05:02, workstation].** The ledger's virtual environment is
`%USERPROFILE%/c5-ops/venvs/ledger`, made with `python -m venv` from anaconda's Python 3.11.5 (base untouched, MC's yes
on thread 0330, entry 1512), with `duckdb==1.4.5` (the 1.4 LTS line) and `plotly==6.5.2` from PyPI (pip also pulled
`narwhals 2.28.0`, `packaging 26.3`). The wheel bundles the `json` and `parquet` extensions (installed and loaded) but
**not `sqlite_scanner`**: `LOAD sqlite` fails with "Extension ... sqlite_scanner.duckdb_extension not found ... Install
it first using INSTALL sqlite", and `INSTALL` downloads from DuckDB's extension server, which is outside "PyPI only"
(the brief's decision). So the index does **not** `ATTACH` a `stats.db`; `ledger ingest` reads SQLite files with
Python's own `sqlite3` (read-only URI, `mode=ro`) and copies the chosen tables into DuckDB. Checked on a copy of an
instance's `Civ5UserHistoryDatabase.db`: six tables read through `sqlite3` (script: the lead's scratch `m5.py`, not kept).

**M1 [L, 2026-11-15 05:12-05:21, 4203, workstation].** The proposal's premise ("its README says it has not run on one")
was already answered for driven games by one-off 9043 (`memory-sampler.md` 5b, 2026-11-13). What was open is an AI
**autoplay** game, which writes only `AutoSave_Post_N` (no `AutoSave_N`): memsampler's default marker would read no
turn. Run: P7 stock seed 1 (section 4.3), c5h-b, the stock EXE with the pin `i0ship-26234371-ia` (3c2f47da...), the
Hormigas pack game, `memsampler.py run --out <run>/mem --instance c5h-b --autosave-kind post --every 2`, started by the
P7 body in a retry loop while it exits 11 (no live game yet); it attached at the launch. Against the driver's own reads
(`tools/stressmap/calibrate.py` `per-turn.csv`, one `tools/harness/memory.py` sample at the first poll after
`Game.GetGameTurn()` changes) over turns 0-39 (`perf/profilekit/m1check.py`): **40 of 40 turns, 0 skipped, 0 missing**;
per-turn differences median -0.05 MB private, 0.00 MB free and largest free block (max |25.4| MB private, |3.6| MB free:
the two read at different moments of the turn); slopes free -0.50 vs -0.49, largest free block -0.59 vs -0.59 MB/turn;
private +0.54 vs +0.41 (wide intervals at n = 40, r^2 0.14). Mean 10.1 s per turn. Verdict: memsampler reads the turns
and the memory of an autoplay game correctly; **`--autosave-kind post` is required for autoplay** (the row is then the
end of the observer's turn N, before the AI part, one turn earlier than `AutoSave_N` would mark). No fix to memsampler
is needed; a note for its README (7000): say that autoplay games write only `_Post_` autosaves. Evidence:
`F:/c5-64bit-work/ledger/pending/p7-stock-seed1/mem/` and `.../game/per-turn.csv`.

**M2 [2026-11-15, 4203].** Family C from the logs that exist, no new hook. Parser: `perf/profilekit/loadphases.py`.
- **Clock.** Every EXE log line is stamped with the machine's uptime in seconds (`[51109.718]`); wall time = that +
  `psutil.boot_time()` [L: within 1.4 s of `Lua.log`'s modification time, 2026-11-15]. The harness state gives the
  process creation time and the driver the moment the load screen was closed, so the whole chain is on one clock.
- **Stock [L, 2026-11-15 05:21-05:24, c5h-a, pin `i0ship-26234371-ia`, huge save `C5H-VP8P-HUGE_0350`, cp+vp+eui,
  workstation under other teams' load: not for timing].** Process start to APP.log 21.3 s; APP.log to menu
  (`stopwatch.log` "Discovering Base Game Maps") 43.1 s; **process to menu 64.4 s**; mod activation (`ActivateEnabledMods`)
  37.7 s; load issued (the harness front end's `vp-dll-dev: loading` in `Lua.log`) to `NetLoadGame` 11.0 s; the DLL's
  read (`NetLoadGame` RECV to `NetPlayerRegisterComplete`) 1.8 s; terrain bake 9.0 s; `LoadGameState_TOTAL` 44.4 s;
  **load issued to load done (`WaitForNetPlayers`) 60.5 s**; load done to control (the harness closes the load screen)
  2.1 s; **process to control 165.0 s**. P1's three cold loads: section 4.2. Every phase of
  `research/02-exe-internals/load-sequence.md` section 1 is readable the same way.
- **Proxy route (inc1) [C, from one-off 9043's `D:/c5-64bit-work/9043/runs/proxy-350/vpproxy.log` and `server.log`,
  2026-11-13].** The EXE's own logs are written as on stock (same phases). `vpproxy.log` counts seconds from the proxy
  DLL's load, `server.log` from the server's start; the zero is the run folder's name `VP64/run/<YYYYmmdd-HHMMSS>-...`
  (1 s) or the files' times. Readable: proxy boot to server attached 0.93 s, `DllGetGameContext` done 0.97 s, database
  ready (`SetGameDatabase ... server ok`) 1.27 s; the database checks "after CacheGameDatabaseData" at server 20.2 s
  (boot) and 46.4 s (mod activation done); **the save's read = the load's stream sessions 46.36-53.54 s (7.2 s)**; the
  game running = the core thread's first `GetGame()` at 106.2 s (59.8 s after the first stream); the server's end
  (`result:` line with its total seconds and exit code). Not readable without a hook: **the save write** (a turn's
  write shows only as a 16-byte stream session; the EXE writes the file itself, so the save-write time is measured from
  outside as on stock: file appears and settles, section 4); **first server picture and whole map baked** belong to
  inc2 (the server renderer's `render.log` writes a per-second `terrain 1s: state=... drawn=... bake=ACTIVE` line,
  `research/05-graphics-composition/proxy-route-compositor.md`, from which both can be read [I: not parsed here]).
  A live product-path run was not made: ticket 0324's thin-client build does not exist yet (state backlog).

**M4 [L, 2026-11-15 05:23-05:25, c5h-a, the same game as M2's stock run, turn 350, not for timing].**
- **The channel's own round trip is 99-217 ms** (`return 1`, 30 times, median 122.7 ms, p95 179.8 ms) with the
  caller polling every 5 ms, and the same (median 127.9 ms) with the harness's default 200 ms poll: the time is spent
  before the game picks the request up, not in the caller's polling; the game-side run time is 0-16 ms. So timing a
  screen through the channel (request, then poll until the state reads "shown") measures mostly the channel: city screen
  239-1,125 ms (median 307), tech tree 207-490 ms (median 234), 10 each. **End to end through the channel: no.**
- **The frame probe: yes.** One chunk installs a per-frame handler (`Events.LocalMachineAppUpdate`, main thread) and
  issues the action; the handler reads the screen's state each frame and keeps the time when it first reads "shown"; a
  later chunk reads the result, so the channel is outside the span. City screen (`UI.DoSelectCityAtPlot`, state
  `UI.IsCityScreenUp()`) and tech tree (`CONTROL_TECH_CHOOSER`, `/InGame/TechTree` not hidden): **shown at the first
  frame after the action in 20 of 20**, os.clock() from the action to that frame 24-77 ms (city, median 30) and 37-48 ms
  (tech, median 41). What it does not see: the frames until the screen's pixels are complete (the state flips first).
- **`Events.LocalMachineAppUpdate` passes two numbers** [L 2026-11-15, 0350]: the uptime in ms (= `GetTickCount64`)
  and the frame delta in seconds (float32, the uptime's difference / 1000); both move on GetTickCount's 15.6 ms tick.
  `proxy/tools/live/frame_counter.lua` and `idle_window.py` are fixed on branch `task-0350-frame-counter-arg`:
  `max_frame_s` comes from os.clock steps, `max_frame_engine_s` from the argument. The ledger's adapters do not read
  `max_frame_s`. Details: `research/08-performance/idle-frame-rate.md` section 0 (on that branch). (Corrected
  2026-11-15 by 9071 from 0350's measurement; replaced: "[I] the argument is the uptime in ms, not a frame delta; the
  counter's largest frame time is the uptime".)
- **os.clock() in the game is wall time with 1 ms steps** [L 2026-11-15, 0350: 23.899 s over 23.890 s of uptime at
  10 fps], so the os.clock spans of this section (the frame probe's 24-77 ms, the save write's 2.0 s) are wall time.
- **Tooltips: no.** A tooltip appears on mouse hover; the Lua channel has no call that shows one and no read of
  whether one is up. It needs posted mouse input (the harness's `input.move`) plus a read of the tooltip control, not
  tried.
- Save write (the same session): `UI.SaveGame` from a one-shot main-thread handler (checklist G9's way) blocks the main
  thread **2.0 s** (os.clock); the file appeared 2.45 s after the request, already at its final size (2,227,788 B).
- End turn to control (`turns.py end` + `wait`): turn 350 48.2 s, turn 351 37.5 s (AI turns, diplomacy screens
  answered by the harness's handler). Exit (`cli.py stop`, 10 s save settle included): 25.2 s.
Evidence: `F:/c5-64bit-work/ledger/pending/4203-probes/p4-v1-20261115-052114-ec0b-m2m4/run.json`, `Logs/`.

## 2. The run record and the index

Written by worker-4201 (W1, 2026-11-15). The contract in full, for review by the MC and 7000: `perf/ledger/CONTRACT.md`
(contract `c5-run/1`, registry `c5-registry/1`); the code is its executable form (`perf/ledger/record.py`,
`ledger.py`, `registry.json`, `schema.sql`); commands and the venv: `perf/ledger/README.md`. Marks: the design is
`[C]` (read in the code and its 67 tests); the timings are `[L]`. Status: **contract written, under review**; W2-W5
build on it.

### 2.1 The record

- **A run folder is the truth**: `<root>/runs/<YYYY-MM>/<run_id>/` with `run.json`, `series-<source>.csv`,
  `events.csv`, `stats.db` and any other file. Root `F:/c5-64bit-work/ledger/` (D141) or env `C5_LEDGER_ROOT`.
  `run_id` = `<UTC yyyymmddThhmmssZ>-<alias>-<6 hex>`; aliases `ws`, `oldpc`, `laptop`, `unknown` (never a host name:
  `ws` is recognised by a SHA-256 prefix of the computer name, testbeds through `TESTBED_HOST`, others by `--machine`).
- **`run.json`** groups: identity (run_id, issue, label, series_id, pair_id, arm, repetition, tool, started_utc),
  configuration (registry list: `stock-32`, `inc1-thin`, `inc2`, `headless-x64`, `headless-x86`), build (pins read
  from `deps/dll.lock` and `deps/i0.lock`, repository commit and dirty flag, SHA-256 prefixes (16 hex) of named
  binaries; for acceptance runs since 0564 the pins are the arm's, checked against its dll, with `build.pins_source`:
  section 8.6), scenario (name, version, save and its SHA-256, map size, turns, mods, asset set (D94), patch options,
  graphics settings hash, Quick Combat/Movement, AI and statistics logging), machine (alias, CPU, logical
  processors, memory, GPU and driver, Windows build, drive; read by the library without a host name), trust (grade
  A-D, quiet lock held, load summary, `not_for_timing`, forced true on `ws`, D106; since 0362 also on `unknown`, and lifted
  on `ws` only inside an MC window, section 7), outcome (proposal 9.3: `ended_by`
  in `completed | crash | game_over | stopped | turn_limit | error`, `turn`, `last_sample` {t_s, time_utc,
  committed_mb, free_mb, largest_free_mb}, `crash_left` in `minidump | dialog | nothing | unknown`, loud lines),
  provenance (backfilled + source), `metrics` (scalars `{"<id>@<version>": number}`), `files` (path, bytes, full
  SHA-256, role) and `seal.sealed_utc`.
- **The seal.** `RunRecord.start()` writes `run.partial.json` and rewrites it after every call; series and event rows
  are flushed one by one. `seal()` hashes every file, validates the whole record and writes `run.json` atomically; a
  record with faults is refused (`SealError`) and stays unsealed. After the seal the library refuses every call and
  the hashes catch any change. A crashed writer leaves an unsealed folder: `validate` reports it, `ingest` skips it.
- **Line endings are part of the hash.** The library writes LF; a folder must be copied byte for byte. The worked
  example carries `.gitattributes` `-text` because the repository's `core.autocrlf` is true on the workstation.
- **Every GPU and the game's GPU (issue 0476, 2026-11-17) `[C]` + `[L]` on the workstation.** `machine.gpu`/`gpu_driver`
  name only the first non-basic adapter of the display class (on the laptop the Intel HD 630, not the Quadro P3000), so
  records since 0476 also carry `machine.gpus` (every adapter of this boot with name, driver, LUID, vendor, `basic`;
  read from `HKLM\SOFTWARE\Microsoft\DirectX\{GUID}`, whose `LastSeen` Windows writes at boot, workstation 69 s after
  it, so cached adapters of earlier boots are left out) and `machine.render_gpus` (per process: `game_log` from the
  server's `using adapter` and the compositor's `host adapter` lines, read at `seal()`; `gpu_counter` from
  `RunRecord.read_render_gpus({label: pid})`, the LUID with the most `\GPU Engine(pid_..)\Running Time`; or
  `declared`). D175's "every timing series names its GPU driver" is met by `render_gpus[].driver` where the game's
  adapter is known, else `gpus`. Costs on the workstation (2026-11-17): the facts 19 ms; the first PDH collection in a
  process 425 ms, later ones 5 ms (36 processes with GPU instances), so the counter read stays outside the timed window.
  The counter's unit is taken as 100 ns `[I]` (only the ordering between a process's LUIDs is used). The ingest
  (`ledger.py`, `schema.sql`) does not index the two keys yet. Contract rows: `perf/ledger/CONTRACT.md`; tests
  `perf/ledger/tests/test_gpus.py` (one, two and a basic adapter; logs; counter; old records valid).

### 2.2 The registry and the conventions

- **Metrics** are `<id>@<version>` everywhere (run.json keys, series column headers, event kinds). Fields: id,
  version, unit (`MB` = 2^20 bytes, `s`, `ms`, `MB/turn`, `count`, `turn`, `percent`), kind (`scalar | series |
  event`), family (A-H, context), better, timing, definition, source. A changed definition is a new version; entries
  are never edited in place. Seeded with 54 metrics, version 1: what memsampler, `tools/harness/memory.py`, turnbench,
  cputime, `perf/tbjobs/series.py`, the compositor's `stats` line, the renderer's `terrain 1s:` line, `run_d10.py`
  and the DLL's `stats.db` measure today, P7's forecast `exe.forecast_out_turn@1`, and the owner breakdown at owner
  level for 4204: `exe.owner.{space, dll, lua, driver, exe_image, other_modules, heaps, mapped, reserved, free,
  fragmentation}_mb@1`, mapped to the toolkit page's owner keys (dll, lua, drv, exe, rsv, frag, free) in
  `CONTRACT.md` section 6. Not seeded (no tool yet): product-path load phases, end turn to control, UI latency, the
  camera path, footprint; W3 adds them.
- **Entries are pinned `[C]` (2026-11-15, worker-4207, 7000's review B2).** `perf/ledger/tests/test_registry_pins.py`
  holds the SHA-256 of each metric entry's canonical JSON (`json.dumps(entry, sort_keys=True, ensure_ascii=False,
  separators=(",", ":"))`, UTF-8): 139 entries. It fails on an entry changed in place, removed, or added without its
  pin line (the message prints the line to add) and tells the reader to add a new version instead. Three entries were
  fixed in place first, as the review allowed, since no run folder under `F:/c5-64bit-work/ledger/runs/` (154 sealed)
  or `pending/` used them (grep, 2026-11-15 09:30): `frame.missed_ins@1` and `frame.timeouts@1` `timing` false ->
  **true** (the compositor's misses and timeouts move with the machine's load); `ctx.revealed_plots@1` ("revealed to the
  observed player (or the union over all players, as the adapter states)": two quantities under one id) **removed and
  split** into `ctx.revealed_plots_observed@1` (one player, the observed one; the writing tool names which in a `note`
  event) and `ctx.revealed_plots_union@1` (plots revealed to at least one player, each counted once). (Replaces: the
  registry rule "never edit in place" was a convention only, unchecked.)
- **Series**: wide CSV per source, `t_s` (seconds since run start), `turn`, optional `label` (a checkpoint name: the
  owner breakdown's rows are labelled `menu`, `after-load`, ...), then registry ids as columns; empty cell = not
  measured. **Events**: `events.csv` `t_s,turn,kind,detail,value`; kind is a registry event kind (`run_start`,
  `load_start`, `load_end`, `turn_start`, `turn_end`, `autosave`, `leader_screen`, `checkpoint`, `crash`,
  `loud_line`, `exit`, `note`) or an event metric with its value (UI latency samples). **Scalars** only in `run.json`.

### 2.3 Commands and the index

- `python -I perf/ledger/ledger.py validate DIR|ROOT` (stdlib, Python 3.8; exit 0/1, one `FAULT` line per fault),
  `new` / `seal` for shell tools (stdlib), `ingest [--root R ...] [--db F]` and `query runs|metric|series|outcomes|
  ingested|SQL` (DuckDB, the ledger venv). Exit codes 0 / 1 faults or errors / 2 usage / 3 duckdb missing.
- **Index** (`perf/ledger/schema.sql`): `runs` (flattened record + `run_json` JSON + its SHA-256), `metrics`,
  `series` (long: run_id, source, metric, version, t_s, turn, label, value), `events`, `files`, `registry_metrics`,
  `ingest_log` (rows only for runs ingested), `stats_<table>` per table of a run's `stats.db`.
- **Ingest rules**: one transaction per run; same run_id and same `run.json` hash = skipped; another hash = error,
  not an update; a sealed run with faults is not ingested (exit 1); unsealed skipped with a line (exit 0); a second
  ingest writes nothing. Series and events are read with DuckDB's CSV reader (explicit column types) and unpivoted in
  SQL; `stats.db` tables through Python's `sqlite3` (`mode=ro`, M5) and a temporary CSV; no extension is ever
  installed or auto-loaded.
- **A changed registry entry refuses the ingest `[C, L]` (2026-11-15, worker-4207, 7000's review B2).** Before any
  write, `ingest` opens an existing index read-only and compares each registry entry with the `registry_metrics` row of
  the same id@version (unit, kind, family, better, timing, definition, source). Any difference: one line per entry,
  `ERROR registry entry <id>@<v> differs from the index's registry_metrics row: <field> (index X, registry Y); ...;
  entries are pinned and never edited in place: restore the entry and add the change as a new version`, then
  `ERROR ingest refused: N registry entr(y|ies) changed in place; the index <db> was not changed`, exit 1, the file
  byte for byte the same (test `test_changed_registry_entry_is_refused_and_the_index_unchanged`). A new id@version is
  still added. (Replaces: the old row was kept silently, `ledger.py` ~390.)
- **What the check found on the real index `[L]` (2026-11-15 09:39:08, workstation).** The first ingest with the new
  code into `F:/c5-64bit-work/ledger/index.duckdb` refused with **13** changed entries, not only the 2 fixed by 4207:
  the 11 `exe.owner.{dll, driver, exe_image, fragmentation, free, heaps, lua, mapped, other_modules, reserved,
  space}_mb@1` definitions had been extended in place on the epic branch (4203b's "Measured: ..." and threshold
  sentences) after the index took their first wording (3.6: ingested with a union registry). The page had been
  showing the old wording. File SHA-256 before and after the refused ingest: `1d94cbb8...` both. (`ctx.revealed_plots@1`
  is no longer in the registry, so it was not compared.)
- **The real index rebuilt `[L]` (2026-11-15 09:39:08-09:39:24, workstation, script
  `rebuild.sh` in worker-4207's scratch: refused ingest, move aside to `index.duckdb.pre-4207`, fresh ingest, second
  ingest, counts, delete the aside file only after both ingests exit 0).** First ingest: 154 run folders, 154 ingested, 0
  errors, 10.57 s. Second: 0 ingested, 154 already in, 0 errors, 0.09 s, file SHA-256 unchanged (`c1856916...`). Tables
  old -> new: runs 154, metrics 671, series 83,232, events 728, files 1,393, ingest_log 154 (all equal);
  registry_metrics 138 -> 139 (the split); `frame.missed_ins@1` and `frame.timeouts@1` now timing true; configurations
  headless-x64 42, headless-x86 78, inc1-thin 2, stock-32 32.
- **Two costs found and removed [L, 2026-11-15 05:25-05:30, workstation, cProfile of a 100-run ingest]:** DuckDB's
  Python binding tries `import pandas` for every bound parameter; with pandas absent each try searched `sys.path`
  again (13 of 38 s). `ledger.py` now puts a `None` entry for pandas in `sys.modules` when pandas is not installed.
  Counting inserted rows with `count(*)` before and after each insert scanned the growing `series` table; the INSERT's
  own row count is used instead.

### 2.4 Ingest timing at scale

`[L]`, workstation (`ws`), **not for timing** (D106: an indication of where the cost is, not a performance claim; the
machine was shared). Command: `python -I perf/quietlock/quietlock.py run --who worker-4201 --what "ledger ingest scale
check" -- <ledger venv python> -I perf/ledger/tests/bench_ingest.py C:/c5-64bit-work/0330/4201-bench3 --runs 100 --rows
5400`: 100 synthetic runs written through `record.py`, each a memsampler-shaped series of 5,400 rows x 11 metric
columns (3 h at 2 s) and 270 events; DuckDB 1.4.5, Python 3.11.5, run folders and index on C:.

| Phase (2026-11-15 05:30:26-05:31:34 EST) | Result |
|---|---|
| write 100 runs through the library (row flushed one by one) | 29.4 s (~0.29 s per 3-hour run) |
| `validate` of the root (every cell parsed) | 8.2 s, 100 valid |
| `ingest` into a new index | 28.4 s wall, 100 ingested, 5,940,000 series rows (~0.28 s per run, inside it validation as above) |
| `ingest` again | 0.5 s, 0 ingested, 100 already in, no table changed |
| index file | 27.0 MB |

Machine during the check: not quiet; `quietlock.py check --quiet` at 05:30:26 listed 9 heavy jobs of other teams
(five game instances, a server build, a scene build, a map A/B with ASan). Earlier runs of the same check on the same
day, before the two fixes of 2.3 and during other load: 48.7 s and 33.9 s for the first ingest. Reading: an ingest
of hundreds of such runs is minutes once and under a second afterwards; the cost is in validation (pure Python,
about 0.06-0.08 s per 59,400 cells) and in DuckDB's CSV read, unpivot and per-run commit. `[I]` faster options if
needed later: skip re-validation of runs whose `run.json` hash was validated before, or ingest several runs per
transaction.

### 2.5 When a new tool may reuse a registry id (7000's ruling in review 7016, 2026-11-16)

From the review of epic 0437's R19 diff (`perf/evidence/7016/review.md`):

- **A new tool may write an existing `id@version`** only when two things hold:
  - It reads the same quantity through the same API.
  - Where the tool itself picks the instant, it reads at the same event as the entry's tool.
- **A different poll rate for the same event is still the same method** for a per-turn delta or a mean. The difference is
  recorded as a note.
  - Example: turn lines polled every 0.1 s by the turn-study runner, against `cputime watch`'s 25 ms. A window mean over
    25 turns moves by at most about lag / 25.
- **A new version is needed in these cases:**
  - a statistic that depends on the sampling rate: a sampled maximum such as `host.peak_private_mb`, where 2 s ticks read
    lower than 0.5 s samples;
  - a different end event: `load.boot_load_s` as turnbench's wall minus the turn sum, against "process start to the save
    turn's first turn line";
  - a different set of points: a slope over the window's boundaries, against memsampler's slope over all turns.
- **Entries not yet on `main`** may be edited in place on their branch, with their pins updated, until this team's review
  is met. A record carries only `id@version`, and branch records are not in the index (D151). From the merge on, an entry
  is immutable (CONTRACT 6).
- **psutil reads the same counters as `tools/harness/memory.py` `[L]`.** psutil `memory_info().private` and `.rss` equal
  `memory.py`'s `private_mb` (PrivateUsage) and `ws_mb` (WorkingSetSize) to 0.1 MB: 3 of 3 back-to-back reads of one
  process, 18.6-34.6 MB. Measured 2026-11-16 on the workstation, Python 3.11.5, psutil 5.9.0, a 64-bit process. A tool
  that reads psutil may therefore reuse `exe.private_mb@1`, `exe.ws_mb@1`, `server.private_mb@1` and `server.ws_mb@1`.
  For a 32-bit process the result is expected to hold `[I]`: both read GetProcessMemoryInfo.
- **No GHz unit.** An effective clock in GHz is `machine.processor_performance_pct@1` times a constant per machine (the
  rated MHz), so it gets no registry id and no unit. A report shows it as a derived column and names the rated MHz. A
  future frequency id would use `MHz`.

## 3. Adapters and backfill (W2, issue 4202)

Written by worker-4202, 2026-11-15. Code `perf/ledger/adapters/` (`adapterlib.py` + one module per source,
`backfill.py` + `backfill.json`, `m6_job.py`); tests `perf/ledger/adapters/tests/` (synthetic inputs written by
`make_data.py`, numbers only). Marks: the mappings are `[C]` (the code and its tests); the conversions, the ingest and
M6 are `[L]` (2026-11-15, workstation and laptop).

### 3.1 How an adapter writes

- **Command**: `python -I perf/ledger/adapters/<kind>.py SRC --root R [--issue I] [--config C] [--grade G] [--machine
  M] [--label L] [--source DOC] [--backfilled] [--ended-by X] [--crash-left Y] [--turn N] [--dry-run]`. Standard
  library, Python 3.8 (the quietlock scanner passes on every file). The source is only read.
- **One run per measured run, idempotent.** `run_id` = the measurement's UTC start, the machine alias, and the first 6
  hex of SHA-256(`<kind>|<key>`), where the key names the run inside its source (start time and pid, arm and round, a
  name). Converting the same output again writes nothing (`ALREADY`, exit 0). Each run is built and sealed under
  `<root>/.staging/` and moved into `runs/<YYYY-MM>/` after the seal, so a failure leaves no folder where ingest looks.
- **What adapters do not invent**: machine facts beyond what the source states (CPU, memory, GPU, Windows build stay
  null; `drive` = the source path's drive), build facts beyond the source's own hashes and commits (a later
  checkout's pins are not the run's), an outcome the source does not give (refused with exit 5 and "pass
  --ended-by"). Times without a zone are read as the converting machine's local time (all three machines are on
  Eastern) `[I]`.
- **Privacy**: text files copied into a folder (a summary, a report, the tool's own run.json) have `C:\Users\<name>`
  replaced by `~`; `provenance.source` names the source document and the raw folder the same way.
- **Exit codes**: 0 written or already in; 1 a seal refused (the record's faults are printed); 2 usage; 4 a source file
  missing; 5 a source malformed (file, line and column named); 6 an unknown configuration, machine, grade or arm kind.
- **Tool calls**: `adapterlib.tool_hook(kind, out, root, **opts)` never raises and prints `ledger:` lines; the tools
  reach it by appending (never inserting) `perf/ledger/adapters` to `sys.path`, because the adapters' file names repeat
  the tools' own (`turnbench.py`, `memsampler.py`); adapters load each other by path under `c5_ledger_adapter_<kind>`.

### 3.2 The adapters

| adapter | source | run folders | registry ids (and other fields) | what the source lacks / not mapped | tried on (real) |
|---|---|---|---|---|---|
| `turnbench` | `turnbench.json` of `perf/turnbench` | one per run; series_id = the out folder (+ job) | turn.ai_mean_s, load.boot_load_s, host.peak_ws_mb, host.peak_private_mb, turn.cpu_mean_s (scalars); series-turns: turn.ai_s, turn.cpu_s; series-machine: machine.free_commit_mb (start, end); trust.load; pins of meta.pins; x64 -> headless-x64, x86 -> headless-x86 | no clock per turn (t_s = boot+load + earlier turns, reconstructed); engine calls are whole-run totals (kept in source-run.json); a file written by series.py is refused (exit 6) | `D:/c5-64bit-work/7000/tb-huge-2026-11-11`, `tb-std-ailog-2026-11-11` (ws, the old-pin history of PERFORMANCE.md) and `9033/setL1/out` (headless_series.py's file, laptop): 48 runs, scratch root only (not in the backfill list; one `convert` line each adds them) |
| `tbseries` | `series.json` of `perf/tbjobs/series.py` | one per run (repetition = round), series_id = label/job | as turnbench + server.exe and DLL SHA-256, save and its SHA-256, AI logging per arm, machine = meta.host; issue = the digits of the label | `counts` and `compares` not mapped | perf/evidence/7010 A-D, 7013-B: 120 runs |
| `memsampler` | memsampler's `--out` (samples.csv, per-turn.csv, summary.json) | one | series-memsampler (every tick) and series-memsampler-turns (+ turn.wall_s): exe.* and server.private_mb/ws_mb; scalars exe.private_slope(+_ci95: half the interval), exe.largest_free_slope(+_ci95), exe.min_free_mb, exe.min_largest_free_mb, exe.forecast_out_turn@1, exe.committed_slope (fitted here), server.private_slope; autosave events | the configuration (`--config` required, exit 6 without); how the game ended ("process exited" -> stopped unless `--ended-by`) | D78's two runs and 0322's: slopes equal the knowledge base's sampler figures (+0.77/-1.55, +1.82/-2.70, +0.91/-2.01) |
| `harnessmem` | `tools/harness/memory.py`'s memory.csv | one | series-memory: exe.* incl. exe.free_blocks_1mb/16mb (32-bit) or server.private_mb/ws_mb (64-bit); exe.min_free_mb, exe.min_largest_free_mb; last_sample | no turn, no configuration (`--config`), no end reason (stopped by default) | 0140 live-a-new (scratch root) |
| `stressmap` | `tools/stressmap/calibrate.py` folder (run.json, per-turn.csv, memory.csv, forecast.json) | one | series-calibrate-turns and series-memory; exe.committed_slope, exe.largest_free_slope(+_ci95), exe.min_free_mb, exe.min_largest_free_mb, **exe.forecast_out_turn@2** (forecast.py); the outcome of proposal 9.3 (autoplay handed back -> turn_limit, EXE gone -> crash, game state -> game_over, stall -> error); map size, mods, the T000 save's SHA-256 | private-bytes slope not mapped (its definition names memsampler's autosave rows); the run's turns.to is what run.json says (pack-cal1 says 30, ran 100) | 9069: six folders; slopes equal section 4 of giant-stress-game.md |
| `serverrun` | the server's run folder (server-report.txt, turn-times.tsv) | one | server.peak_ws_mb, turn.ai_mean_s; series-turns: turn.ai_s; phases as `note` events with their seconds | configuration inferred from the text (remote + render thread -> inc2, remote -> inc1-thin, direct -> headless-x64) `[I]`; direct mode's start = file time - total; the engine-call table (whole run) | 0140 live-a-new (remote), D:/c5-64bit-work/0132/acc/control (direct) |
| `loglines` | the compositor's `stats` lines, the renderer's `terrain 1s:` lines | one for all files given | series-compositor: frame.ft_mean_ms, ft_p99_ms, ft_max_ms, missed_ins, timeouts; series-renderer: frame.terrain_gpu_ms (gpu_ms -1 = empty) | the date (`--date` or the file's), the turn; inc2 by default | 0140 live-a-new (796 stats, 892 terrain lines) |
| `capacity` | `perf/capacity` level (level.json, samples.jsonl or `<prefix>`-named copies) | one per level | capacity.places, capacity.places_ok; series-capacity: machine.cpu_total_pct, machine.free_commit_mb, machine.free_phys_mb, machine.processors_busy | per-server turn times, per-game memory (in the copied level.json) | perf/capacity/evidence 0158 headless L04, games L4-64GB |
| `release` | the release tier's `validated-report.json` (tools/release/stages/report.py) | one | release.validated, release.stages_ok, release.stages_not_ok, stability.proxy_mismatches, stability.proxy_lock_errors; pins and binaries of `builds`; release_descriptor = the tag | no hash of the descriptor; inc1-thin by default | D:/c5-64bit-work/3500/evidence/3506 |
| `profilekit` | a kit folder of schema `profilekit-run/1` that was never sealed | one (P1: one per repetition) | load phases as `note` events (the kit's own load ids are on 4203b's branch), `mem_*` checkpoints as series-profilekit rows with labels | P7 refused (exit 6: convert game/ with stressmap, mem/ with memsampler) | the 4203 probes (scratch root only: 4203b had sealed them) |

Error paths (missing file, malformed row, unknown configuration) of every adapter have a test that checks the exit code,
the message and that nothing is left under `runs/` or `.staging/` (31 cases).

### 3.3 Registry entries appended (12, at the end of `metrics`)

`exe.heap_slope@1` (the memory investigation's heap growth, backfill only), `exe.forecast_out_turn@2`
(tools/stressmap/forecast.py's forecast: a different method from @1, so a new version), `server.peak_ws_mb@1`,
`machine.free_phys_mb@1`, `machine.processors_busy@1`, `capacity.places@1`, `capacity.places_ok@1`,
`release.validated@1`, `release.stages_ok@1`, `release.stages_not_ok@1`, `stability.proxy_mismatches@1`,
`stability.proxy_lock_errors@1`. No id overlaps the 72 that worker-4203b appended on `task-4203-profile-kit-2`
(load, ui, camera, footprint, exe.item, server.owner); both lists end the same array, so the merge conflicts by
position only.

### 3.4 The tools' opt-in calls

`--ledger-root R` (default env `C5_LEDGER_ROOT`) at the end of `perf/turnbench/turnbench.py` (+ `--ledger-issue`; exit
1 when the record fails), `perf/memsampler/memsampler.py run|join` (+ `--ledger-config`, required with an explicit
root; the env root applies only when `--ledger-config` is given; exit 15 when the sampler ended ok and the record
failed), `perf/tbjobs/series.py` (+ `--ledger-issue`; on a testbed `%TESTBED_OUT%/ledger`), `perf/capacity/capacity.py
headless` and `perf/capacity/games.py level` (+ `--ledger-issue`). Nothing they measure changed. Tests:
`perf/memsampler` 25, `perf/capacity` 11, `perf/turnbench` 15 passed (anaconda Python 3.11; one turnbench cputime test
failed once in an earlier combined run on the loaded workstation and passed alone and in the rerun: timing-sensitive,
not touched by this change); `perf/quietlock` 64 passed.

### 3.5 The backfill

`python -I perf/ledger/adapters/backfill.py --root R` (inputs `perf/ledger/adapters/backfill.json`; re-runnable into an
empty root; a second run writes nothing). 2026-11-15 06:47 into `F:/c5-64bit-work/ledger`: 135 run folders, 0 failures.

| entry | runs | backfilled | source | trust as recorded |
|---|---|---|---|---|
| 7010 series A, B, C (oldpc), D (laptop), 7013-B-loggedon (oldpc) | 30 + 24 + 24 + 18 + 24 | false (raw `perf/evidence/7010/*/series.json`) | turn-time-baselines.md section 11; PERFORMANCE.md baselines (B, C) | A (>= 6 ok runs per arm on a quiet testbed job) |
| D78 proxy route `proxy-350`, stock route `stock-350` | 1 + 1 | false (raw `D:/c5-64bit-work/9043/mem-*`) | increment-1-acceptance.md sections 1-3 | C (ws, one run each, not for timing); ended by game_over at 436 / 425 |
| 0322 `huge80b` (human play through the proxy) | 1 | false (raw `F:/c5-64bit-work/0322/ev/mem-huge80b`) | increment-1-acceptance.md section 4 | C; turn_limit at 429 |
| 9069 `stock-cal1..4`, `stock-tsl150`, `pack-cal1` | 6 | false (raw `F:/c5-64bit-work/0330/9069/*`) | giant-stress-game.md section 4 (cal4, tsl150, pack-cal1 in its table; cal1-cal3 earlier attempts; cal2 had no end record: `--ended-by error`) | C |
| memory investigation run 15, GameId 62 (crash at 317, committed +2.476 MB/turn, t0/t20/t314 points, t314: 70.5 MB free, largest 4.0) | 1 | **true** | leaks-and-growth.md 2; ownership-and-ceiling.md 1; timeline item 19 | C |
| the CitySites leak fix, GameId 47 class, before / after a26506ae8 (heap 4.05 -> 0.63, committed 6.54 -> 1.48 MB/turn), pair `mi-leak-fix-a26506ae8` | 2 | **true** | leaks-and-growth.md 1 (the fix table); timeline item 15 | C |
| run 9, GameId 40, 200 turns, committed +1.006 MB/turn | 1 | **true** | leaks-and-growth.md 1 (no second leak); timeline item 13 | C |
| the toolkit's hand-made owner points t7 (GameId 77) and t344 (GameId 62) | 2 | **true**; numbers read from the owner's `process_memory.json` in place, only into F: | ownership-and-ceiling.md 3-4; section 5.4 here | C |

- The owner points map to the contract's owner ids (section 6 of `CONTRACT.md`): t344 dll 312.6, lua 95.9, driver
  329.1, EXE image 101.1, other modules 119.5, heaps 2,149.9 (with `exe.misc`: the page does not split it from mapped
  files, so `exe.owner.mapped_mb` is empty), reserved 516.0, free 127.6, fragmentation 344.2; t7 169.8 / 81.5 / 329.1 /
  101.1 / 123.2 / 1,609.2 / - / 428.3 / 1,134.2 / 119.5; each sums to 4,095.9 = `space`.
- Dates of the September points are the timeline's days; the hours are unknown (12:00 local), marked `[I]` in a
  `note` event of each folder. Backfilled series rows carry `t_s` 0 (the sources give turns, not times).
- **Not backfilled**: the profile kit's probe runs of 2026-11-15 (worker-4203b sealed them into the root through
  `record.py` at 06:28-06:29); bug 0102's growth through the proxy ("about 30 MB per AI turn", 9036/9041: an
  approximate figure, and the raw 9036 driver runs end at turn 361 and 380; no adapter for `proxy_long.py`'s
  run.json); P7 seeds 1 and 2 (running; the kit seals them, or `stressmap.py` + `memsampler.py` on `game/` and `mem/`).
- One measured run with several sources (0140's live runs: memsampler, server-report, compositor and renderer logs,
  memory.csv) becomes one folder per adapter: the adapters were tried on them in a scratch root
  (`C:/c5-64bit-work/0330/4202/t1`, removed after the session), not ingested. Joining sources into one folder is open.

### 3.6 The real index after the ingest, and the two-ingest check `[L]`

Commands (2026-11-15, workstation, the ledger venv; the root also holds 13 runs sealed by 4203b and 4204, whose ids are
on `task-4203-profile-kit-2`, so the ingest used the union of the two registries,
`F:/c5-64bit-work/0330/4202/registry-union.json`, 138 metrics):

```
<venv> -I perf/ledger/ledger.py --registry <union> ingest --root F:/c5-64bit-work/ledger     # 06:49:41
<venv> -I perf/ledger/ledger.py --registry <union> ingest --root F:/c5-64bit-work/ledger     # 06:50:28
```

| | first ingest | second ingest |
|---|---|---|
| result line | 148 run folders, 148 ingested, 0 errors, 13.20 s | 148 run folders, 0 ingested, 148 already in, 0 errors, 0.11 s |
| tables | runs 148, metrics 635, series 70,686, events 643, files 747, ingest_log 148, registry_metrics 138 | identical |
| content hashes (runs, metrics, series ordered) | 72bdb6bd..., 48f44a63..., 1789239a... | identical |

Runs by configuration: headless-x64 42, headless-x86 78, stock-32 26, inc1-thin 2; by machine: oldpc 102, laptop 18,
ws 28; backfilled 6. Logs and counts: `F:/c5-64bit-work/0330/4202/` (`ingest-1.log`, `ingest-2.log`, `counts-1.json`,
`counts-2.json`, `backfill-real-1.log`, `registry-union.json`). The index is `F:/c5-64bit-work/ledger/index.duckdb`.
Until 4203b's registry entries are on the branch that ingests, an ingest with this branch's registry alone reports
those 12 runs as faults (unknown metric) and leaves them out. **Superseded 2026-11-15 09:39 (4207):** this index was
rebuilt from the run folders with the epic branch's own registry (154 runs, no union file); see 2.3.

### 3.7 M6: run folders back through `tb.py fetch` `[L]`

`tb.py fetch` brings back `out/` files up to the host's `fetch_max_mb` (default 5 MB) each, larger ones only by
`--include`/`--all`, checked by SHA-256 (`tools/testbed/tbclient.py fetch`). Job `20261115-065240-917b` on the laptop
(`perf/ledger/adapters/m6_job.py`, functional, not quiet; the laptop's `TESTBED_PYTHON` is 3.12.10, not 3.8): a
harness memory.csv of a child process converted by `harnessmem.py`, and a synthetic memsampler-shaped run of 3 h at 2 s
(5,400 rows x 11 columns + 270 events) written through `record.py`, both validated on the laptop. The largest file is
**478,663 B** (series-memsampler.csv), the whole folder 495 kB: a tenth of the fetch limit. Fetched (10 files, 0.5 MB, 2
s) to `F:/c5-64bit-work/0330/4202/m6-20261115-065240-917b/`; `ledger.py validate` 2 of 2 valid (the seal's SHA-256 held
through the tar), ingested into a scratch index (`F:/c5-64bit-work/0330/4202/m6-index.duckdb`; checks, not
measurements, never in the real index): 2 ingested, then 0 and 2 already in. A run would cross 5 MB in one file at
about 10x that series (30 h at 2 s, or 3 h at 0.2 s): `tb.py fetch --include "ledger/**"` then. The same job on oldpc's
Python 3.8 was queued as `20261115-065437-69bb` behind 0349's quiet job.


## 4. The profile kit (`perf/profilekit/`, issue 4203)

Written 2026-11-15 by worker-4203 (wave 1) and worker-4203b (wave 2: the ledger wiring, P6 v1, P2 and P6 live, P7 seed
2, owner snapshots on P7). One command per scenario on a named configuration; one kit run folder per run with a
`run.json` (schema `profilekit-run/1`: scenario name and version, configuration, build (repository commit, DLL path and
SHA-256), machine alias, trust (`not_for_timing` on the workstation, D106), instance, state, result) beside the tools'
own files. The kit's own code reads that folder while a run goes on (P7's `status`, `stop`, `finish`, the keepers), so it
stays. **When the run ends** (state `done...` or `failed`), `perf/profilekit/common.py` `write_record` writes the sealed
ledger run folder(s) through `perf/ledger/record.py` (section 4.4). Tests: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m
pytest perf/profilekit/tests -q` (hermetic: the ledger root is the test's tmp folder, `tests/conftest.py`). README of the
commands: `perf/profilekit/kit.py`'s docstring (`python -I perf/profilekit/kit.py list`).

### 4.1 Scenarios

| # | version | command (configuration `stock-32`; `inc1-thin` refuses with exit 4 until ticket 0324's build exists) | gives | where |
|---|---|---|---|---|
| P1 | 1 | `kit.py run P1 --instance I --owner O --out-root DIR [--reps 3]`: cold start, menu, load the huge save `C5H-VP8P-HUGE_0350` (turn 350), control, safe stop; the EXE's `Logs/` copied per launch and parsed (`loadphases.py`) | family C: `load.*`, `exit.request_to_exit_s` (M2); one ledger run per repetition | timing: oldpc only (`tbjob.py`); workstation runs are "not for timing" |
| P2 | 1 | `kit.py run P2 ... [--turns 25]`: the huge save, memsampler beside it, 25 x end turn to control, memory after load and after the turns. The Standard map 0-50 part is the headless series `perf/turnbench/turnbench.py --game standard` (named, not re-implemented) | A (`series-memsampler`, checkpoints), D (`turn.end_to_control_s`), C | memory anywhere; timing oldpc |
| P4 | 1 | `kit.py run P4 ... [--reps 10]`: the channel's round trip, the city screen and the tech tree by the frame probe (`--reps` each), one save written, end turn to control twice | E (`ui.*` events, raw samples kept from wave 2 on), `save.write_*`, `footprint.save_mb`, D | oldpc for timing |
| P5 | 1 | `kit.py run P5 ...`: the frame cap lifted, the camera jumps over 16 plots across the huge map (`UI.LookAt` from the per-frame handler, 1.5 s each), frame times from the per-frame event's uptime argument | F `camera.*` (stock part; the product path's compositor line when 0324/inc2 exist) | oldpc for timing |
| P6 | **1** (wave 2) | `kit.py run P6 --instance I --owner O --out-root DIR [--turns 25]`: `perf/ledger/owners/m3_drive.py` twice: `menu` (owner snapshot at the idle main menu) and `load` (the huge save, a snapshot after the load and after each of 25 AI turns); harness launch, guard, safe stop and front-end restore inside the driver; the pin put in and restored by the kit. v0 (`--pid PID --point P`, one `tools/harness/memory.py` sample) is kept | A: `exe.owner.*`, `exe.item.*` at `menu`, `after-load`, `after-1` ... `after-25` | anywhere (memory only) |
| P7 | 1 | `kit.py run P7 --instance I --owner O --run-dir DIR` = `p7_giant.py start` (4.3); from wave 2 the body also takes owner snapshots (turn 0, every 25 turns, turn 344) | the CTD marker (outcome fields of proposal 9.3), A, G, owner breakdown | anywhere (memory only) |

Live scenarios need a pool instance claimed by `--owner` (exit 3 otherwise); they put the pin
(`<pins_dir>/dlls/i0ship-26234371-ia`, 3c2f47da...) into the instance, keep the DLL that was there by SHA-256 in
the run folder's `backup/` and put it back at the end, and restore the harness front end. A failed step ends the
scenario with exit 1 and leaves the game running for a safe stop (nothing stops a game from an except block; P6's
driver stops its own game safely). Exit codes: 0 ok, 1 a step failed, 2 usage, 3 refused, 4 not ready.

**On oldpc (timing):** `perf/profilekit/tbjob.py` (docstring: the full `tb.py --host oldpc submit --quiet --headed ...`
line; it ships the pin, the harness natives and the front-end assets, copies `%TESTBED_SAVES%/TB-HUGE_0350` into the
job's instance and runs `kit.py ... --testbed --dll <shipped pin>`). Not run yet [I]: it follows `product_path.py`'s
set-up; the first submission is the check. On the testbed the ledger root is the job's folder (`C5_LEDGER_ROOT`), the
machine alias comes from `TESTBED_HOST` (record.py), the grade defaults to B.

### 4.2 First runs (workstation, c5h-a, 2026-11-15, not for timing) [L]

| run | result | kit folder under `F:/c5-64bit-work/ledger/pending/4203-probes/`; sealed ledger run (`F:/c5-64bit-work/ledger/runs/2026-11/`) |
|---|---|---|
| P4 (M2, M4) | section 1, M2 and M4 | `p4-v1-20261115-052114-ec0b-m2m4`; `20261115T102114Z-ws-9855bf` |
| P4 again (frame probe in ms, 5 reps) | channel round trip median 124.5 ms (75-192); city screen and tech tree "shown" at the first frame in 10 of 10 (0 ms between the first frame and the shown frame), action to that frame 31-106 ms (city, median 33) and 36-41 ms (tech, median 39) by os.clock; through the channel 212-990 / 163-271 ms; save write 1.64 s in game, file at 1.79 s; end turn to control 44.8 / 37.2 s; process to control 221.4 s (P7 ran beside it) | `p4-v1-20261115-053147-699e-probe`; `20261115T103147Z-ws-c441ab` |
| P1 (3 cold loads, 05:38-05:49) | process to APP.log 30.2 / 24.8 / 33.8 s; **process to menu 73.0 / 68.2 / 81.8 s**; mod activation 37.7 / 38.1 / 43.3 s; **load issued to load done 65.1 / 68.2 / 66.4 s** (`LoadGameState_TOTAL` 47.4 / 50.9 / 47.4); load done to control 1.8 / 2.5 / 1.6 s; **process to control 178.0 / 177.5 / 193.6 s**; exit (safe stop, 10 s settle) 24.5 / 27.6 / 24.9 s. P7's game ran on the same machine all the time | `p1-v1-20261115-053757-ca97-probe`; `20261115T103802Z-ws-12697a`, `...T104203Z-ws-18be22`, `...T104605Z-ws-48db3d` (repetitions 1-3, one series_id) |
| P5 | 1,744 frames in ~27 s with the cap (30) lifted: mean 15.8 ms, median 16 ms, p99 78 ms, max 110 ms; 16 of 16 camera moves. The uptime argument moves in steps of the system tick (~15.6 ms), so single frame times are quantised to it: means hold, p99 and maximum are coarse [I]. Its stored `over_50ms` = `over_100ms` = 1,327 cannot be right beside a 16 ms median (an earlier code version; the keys were then `*_s` although the values were ms): not converted into the ledger | `p5-v1-20261115-052721-94d2-probe`; `20261115T102722Z-ws-8e3aed` |
| P4, the first try | launch failed (rc 1, no launch line): sealed as `ended_by error` (stability evidence, no measurement) | `p4-v1-20261115-052001-5ae1-m2m4`; `20261115T102001Z-ws-c17dfc` |

The UI latency samples of these P4 runs were kept as summaries only (median, p95, min, max), so their ledger runs carry
no `ui.*` events; from wave 2 on `stocklive.time_ui` and `channel_rtt` keep every sample (`samples_*`) and the ledger run
gets one event row per opening. Their end-turn rows have no start time: the ledger's `t_s` for them is control plus the
waits before (wave 2 records `t0`).

### 4.3 P7, the giant game: stock seeds 1 and 2

- **Seed 1 (running)** [L, started 2026-11-15 05:12:16 EST, worker-4203]: `perf/profilekit/p7_giant.py start --instance
  c5h-b --owner 4203 --config stock-32 --run-dir F:/c5-64bit-work/ledger/pending/p7-stock-seed1`, from a code snapshot
  (`F:/c5-64bit-work/ledger/code/p7-fc1c7e19`, commit fc1c7e19, harness natives built there) so that it never depends
  on a worktree. Body pid **61948** (python; started detached: `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP |
  CREATE_NO_WINDOW`), calibrate.py pid 63076, **game pid 28900** (`CivilizationV_DX11.exe` of c5h-b), memsampler in a
  loop beside it. The game: one-off 9069's recipe (`tools/stressmap/calibrate.py --pack --turns 2000`, unchanged), a new
  game from turn 0: 21 AI majors + the observer, **32 city states** (Cahokia, Melbourne, Colombo, Buenos Aires,
  Jerusalem, ... the full list in `run.json` `identity`), victories off, max turns 2000, Standard speed. Seed: the
  front end leaves the seeds random and `PreGame.GetMapRandomSeed` does not exist in the game's Lua (`n/a`), so the
  game's identity is its turn-0 save `saves/P7-STOCK-S1-T000.Civ5Save` (SHA-256 e6fba102..., 475,810 B) and the city
  states drawn. Every 25th turn's `AutoSave_Post_N` is copied into `saves/` with its SHA-256 in `saves/sha256.txt` (a
  reload from one buys turns: proposal 9.2). For seed 1 the body's copy looked in `ModdedSaves/auto` instead of
  `ModdedSaves/single/auto` (fixed in 6552e81c); a detached keeper (`p7_giant.py keeper`, pid 41740, from the snapshot
  `F:/c5-64bit-work/ledger/code/p7-6552e81c`, ends when the game ends) copies them. **Owner snapshots** (wave 2): a
  detached `p7_giant.py owners-keeper` (pid 50688, snapshot `F:/c5-64bit-work/ledger/code/p7-941ec0af`, started 06:10)
  takes the checkpoints it can still reach: the first at checkpoint 150 seen at turn 161, then 175, 200, ..., 344.
- **Seed 2 (running)** [L, started 2026-11-15 06:07:07 EST, worker-4203b, MC entry 1542]: `p7_giant.py start --instance
  c5h-a --owner 4203 --config stock-32 --run-dir F:/c5-64bit-work/ledger/pending/p7-stock-seed2 --save-name
  P7-STOCK-S2-T000 --label seed2` from `F:/c5-64bit-work/ledger/code/p7-d0183477` (team branch d0183477, harness natives
  built there through `quietlock.py run`); body pid **45532**, calibrate pid 43208, **game pid 71660**; c5h-a's DLL
  (e589306e...) backed up in the run's `backup/`, the pin put in (set-dll rc 0), map pack applied (rc 0). A new game
  from turn 0 with a **new city-state draw**: 32 city states (Jerusalem, Melbourne, Kinshasa, Hanoi, Cahokia, Sydney,
  Ife, Kathmandu, ...; `run.json` `identity`). Its body has the fixed 25-turn save copy. Owner snapshots: a detached
  `owners-keeper` (pid 55292, same snapshot p7-941ec0af) started 3 minutes after the game's turn 0, so its "turn 0"
  checkpoint is the walk at **turn 2**, then 25, 50, 75, ... Seeds started from wave 2's code take them inside the body.
- **Pool claims:** seed 1 on c5h-b, seed 2 on c5h-a, both owner 4203, `--length long --hours 24` (leases to
  2026-11-16 05:07 and 06:06 EST). A claim whose game is alive is never reaped, so a run past the lease keeps it; each
  body releases its claim after the game has ended and the instance is put back (map pack removed, front end restored,
  the DLL e589306e... restored from `backup/` by hash, `cli.py leftovers` clean). Seed 3 only after seed 1 ends, by the
  MC's leave (thread 0330).
- **Progress and forecast** [L]: seed 1: turn 50 at 05:23:31 (committed 2,625.2 MB), **turn 100 at 05:40:19**
  (committed 2,719.6, free 792.6, largest free block 588.9 MB), turn 188 at 06:31 (committed 2,812.5, free 692.2,
  largest 554.0), **turn 297 at 09:00** (committed 2,992.3, reserved 666.9, free 436.8, **largest 262.6**); 10-20 s per
  turn early, **about 2.5 minutes per turn at turn 295** (with P2/P6 beside it until 08:56). Seed 2: turn 86 at 06:31,
  **turn 261 at 09:00** (committed 2,912.5, free 602.9, largest 386.1), also slow now. `tools/stressmap/forecast.py`
  (death line 70 MB free), [I] each: seed 1 over turns 51-101 said **about turn 290** (free -3.74 +- 0.19 MB/turn);
  **corrected 06:35** by turns 101-188 (-1.53 +- 0.06: about 583); **at 09:00** turns 188-297 give free -2.00 +- 0.06,
  committed +1.68, largest free block -2.84 MB/turn, **death by free space about turn 492, by committed about 498**
  (turns 101-297: 526 / 540). Seed 2, turns 101-261: free -1.74 +- 0.04, **about turn 555** (committed 647). The slope
  is not constant: it rose again after turn 188 (the 51-101 window ran fast, 101-188 slow). Outcomes when the seeds
  end: `p7.log`, `outcome.json`, and `giant-stress-game.md` 5.
- **Check:** `python -I perf/profilekit/p7_giant.py status --run-dir F:/c5-64bit-work/ledger/pending/p7-stock-seed<N>`
  (body and game alive, last turn row with memory, memsampler rows); the logs are `p7.log`, `game/run.log`; owner
  snapshots in `owners/index.jsonl` and `owners/c<checkpoint>-t<turn>/` (with `compare-t344.txt`).
- **Stop safely** (never `taskkill`): `python -I perf/profilekit/p7_giant.py stop --run-dir <run> --why "<who, why>"`
  sets autoplay to hand back after the current turn (`Game.SetAIAutoPlay(1, 0)` over the Lua channel); calibrate.py
  then sees the human turn and stops the game with the harness's safe stop (newest save still 10 s, human turn), and
  the body records the outcome as `stopped` and puts the instance back. If the body has died, the same `stop` still
  works for the game; then `cli.py -i <instance> stop --human-turn-hook tools.harness.turns:stop_readiness` once the
  turn is handed back, and `p7_giant.py finish --run-dir <run>` writes the outcome and cleans up. The keepers end by
  themselves when the game is gone.
- **The end** writes `outcome.json` (proposal 9.3): `ended_by` crash / game over / stopped / turn limit (and `stall`,
  `error`), the turn, the last memory sample (`game/memory.csv`, every 5 s) and the largest free block at it, what a
  crash left (minidump in the instance's crash folder, WER report, an error dialog, nothing; `crash/` holds copies),
  and for a crash the marker text ("CTD, out of address space at turn N" when the largest free block was under 64 MB).
  A crash that leaves a dialog keeps the process alive: the run is then `needs-attention`, the claim is kept and a
  coordinator decides (D29: `cli.py dismiss`), then `finish`. Seeds 1 and 2 run older code (fc1c7e19, d0183477) whose
  `write_record` does not seal: when they end, `python -I perf/profilekit/ledger_kit.py seal <run>` (wave 2's code)
  writes their ledger runs (owner snapshots, memsampler, per-turn, 25-turn save sizes, outcome).
- **Exposure** [L]: the body, the driver and the game run in the job object of the agent's shell (breakaway refused;
  that job has no kill-on-close; an outer job, if any, is not known). Every harness game started by an agent is in the
  same position; if the Claude Code process that owns the job ended with kill-on-close, the game would end with it [I].

### 4.4 The ledger wiring (wave 2, `perf/profilekit/ledger_kit.py`) [C, tests `tests/test_ledger_kit.py`]

`ledger_kit.seal_kit_run(kit_dir)` (called by `common.write_record` once; by hand: `python -I
perf/profilekit/ledger_kit.py seal KIT_RUN [--root R] [--grade G] [--force]`) writes one c5-run/1 folder per measured
run (P1: one per repetition, `identity.series_id` = `kit-<kit folder>`, `repetition` 1-3) and names them in the kit's
`run.json` `ledger` (a second call writes nothing). The record: `configuration` as the kit's; `build` from record.py
with the kit's repository commit (the snapshot's `SNAPSHOT_COMMIT`) and the pin by its SHA-256 prefix; `scenario.save`
by name and SHA-256 (the huge save c499d3c6...88c3; P7 its turn-0 save); `asset_set` `vibrant` (D94); `trust.grade`
**C** on the workstation (busy machine, single runs), B on a testbed, `not_for_timing` true on `ws`; `provenance.backfilled`
false (the raw files are the runs' own). Files: `series-memsampler.csv` (exe.* from memsampler's `samples.csv`; P2, P7),
`series-turns.csv` (`turn.wall_s` and `exe.free_blocks_16mb` per turn for P7; `turn.end_to_control_s` for P2/P4),
`series-checkpoints.csv` (tools/harness/memory.py samples at `after-load`, `after-25`), `series-owners.csv` (P6, P7),
`series-footprint.csv` (P4's save, P7's 25-turn saves), `events.csv` (checkpoints; `ui.*` samples), the scalars
(`load.*`, `exit.request_to_exit_s`, `save.write_*`, `camera.*`, `footprint.logs_mb`, P7's `exe.forecast_out_turn` when
the kit stored one), the outcome (P7: `ended_by` crash / game_over / stopped / turn_limit, `stall` and `error` as
`error`; the last memory sample; `crash_left`; the CTD marker as a loud line), and under `kit/` every file of the kit
folder byte for byte except the DLL backup, saves, minidumps and raw walks, which `kit/excluded.txt` lists with their
SHA-256 (they stay in the kit folder). A fault never fails the scenario: it lands in the kit's `run.json`
`ledger_error` (tested). `C5_LEDGER_OFF=1` skips it. The 4204 driver's evidence goes in with `ledger_kit.py owners-m3
DRIVE_DIR --issue 4204 --scenario NAME [--save-name N --save-sha256 H]`.

**Sealed so far** (2026-11-15 06:28, `ledger.py validate F:/c5-64bit-work/ledger`: 12 valid): the six wave-1 kit runs of
4.2 (eight ledger runs with P1's three) and 4204's M3 evidence: `menu-1` (`20261115T102400Z-ws-6f4267`; its "menu"
snapshot was the load screen, 5.3), `huge350-1` (`...T102812Z-ws-f1e95b`), `huge350-2` (`...T103642Z-ws-0a25a2`),
`pack-t000-1` (`...T104351Z-ws-0de0ee`, save 5c21a29f...), `menu-2` (`...T104917Z-ws-4d2c8c`).

### 4.5 P2 and P6 live (stock-32, workstation, not for timing)

**P2 live x3** [L, 2026-11-15 06:15-07:21 EST, c5h-c (short pool place, owner 4203, claimed 06:15, released 07:21,
`leftovers` clean), pin `i0ship-26234371-ia`, `C5H-VP8P-HUGE_0350` turn 350 -> 375, memsampler every 2 s, from the
snapshot `F:/c5-64bit-work/ledger/code/p7-d0183477`; c5h-c's DLL e589306e... restored by hash and the front end
restored after each run; both P7 games, other teams' games and builds ran on the machine: **not for timing**]. Kit
folders `F:/c5-64bit-work/ledger/pending/4203b-live/p2-v1-20261115-{061530-0f96,063544-2f21,065638-ff44}-rep<N>`;
ledger runs `20261115T111530Z-ws-58c1f9`, `...T113544Z-ws-40e671`, `...T115639Z-ws-647081`.

| rep | process to control s | end turn to control s, median (min-max) | private MB after load -> after 25 | largest free block MB after load -> after 25 | private slope MB/turn (95 % half-width) | largest-free slope MB/turn |
|---|---|---|---|---|---|---|
| 1 | 196.0 | 38.2 (25.4-46.1) | 2015.2 -> 2160.6 | 1261.7 -> 934.1 | 1.97 (0.67) | -7.57 (1.45) |
| 2 | 187.3 | 42.9 (25.9-55.1) | 2021.6 -> 2164.9 | 1247.6 -> 936.8 | 2.62 (0.68) | -6.71 (1.30) |
| 3 | 239.5 | 49.0 (35.4-60.5) | 2062.1 -> 2182.1 | 1175.5 -> 924.1 | 2.88 (0.77) | -4.00 (1.17) |

Memory repeats closely (after 25 turns: private 2161-2182 MB, largest free block 924-937 MB, smallest free 1005-1015
MB); the times grew from run to run with the machine's load (rep 3 ran while P7 seed 1 passed turn 215), as expected
on the workstation. 25 AI turns of the huge save take about 330 MB of the largest free block (the big block shrinks by
steps; the per-turn slope's interval is wide on 25 points).

**P2's Standard 0-50 part** [L, 06:40-06:42, workstation, headless]: `perf/turnbench/turnbench.py --game standard
--arms x86 --x86-dll <pins_dir>/dlls/i0ship-26234371/CvGameCore_Expansion2.dll --runs 3 --no-lock` (the
non-instance-aware build of the pin, which the headless host loads), through `quietlock.py run`: 3 of 3 good, boot and
load 22.5-23.7 s, **peak private 181-185 MB, peak working set 186-187 MB**; 0.31 s per turn (machine at 64 % CPU: not
for timing). Output `F:/c5-64bit-work/ledger/pending/4203b-live/turnbench-standard/turnbench.{json,txt}`; not sealed
here (turnbench's adapter is 4202's).

**P6 live x3** [L, 2026-11-15 07:30-08:56 EST, c5h-d (F:, short pool place, owner 4203, claimed 07:30, released
08:56, `leftovers` clean; its DLL was already the pin), `kit.py run P6 --turns 25` from the snapshot
`F:/c5-64bit-work/ledger/code/kit-d837e8da`, `C5H-VP8P-HUGE_0350`, memory only]. Kit folders
`F:/c5-64bit-work/ledger/pending/4203b-live/p6-v1-20261115-{073018-1eaf,075956-0231,082826-72b9}-d-rep<N>`; ledger runs
(sealed by `write_record` itself, the first runs of the wired kit) `20261115T123018Z-ws-33a6e9`, `...T125957Z-ws-fd7344`,
`...T132826Z-ws-220e7f`, each with 27 rows in `series-owners.csv` (`menu`, `after-load`, `after-1` ... `after-25`).
MB, the ledger's owner ids (5.7; free = blocks of 16 MB or more), three runs:

| point | committed | free (largest block) | exe.owner free / fragmentation | heaps | lua | driver | reserved | exe.big / exe.small |
|---|---|---|---|---|---|---|---|---|
| menu | 1839.9 / 1838.5 / 1839.3 | 1722 / 1706 / 1720 (1674 / 1652 / 1669) | 1674 / 1652 / 1669 ; 59 / 64 / 62 | 1255-1256 | 59.3 / 61.5 / 60.8 | 299.8 | 480 / 495 / 480 | 639.9 ; 375-378 |
| after load (t350) | 2339.3 / 2342.1 / 2340.3 | 1219 / 1235 / 1221 (1177 / 1193 / 1174) | 1177 / 1193 / 1174 ; 44 / 45 / 51 | 1711-1712 | 98.0 / 98.3 / 99.2 | 331.8 | 465 / 447 / 461 | 852.2-852.3 ; 503-504 |
| after 25 AI turns (t375) | 2491.4 / 2485.3 / 2482.9 | 1061 / 1062 / 1076 (969 / 947 / 964) | 1018 / 1009 / 1031 ; 73 / 81 / 72 | 1822-1827 | 110.2-111.3 | 331.8 | 466 / 472 / 458 | 868.8-869.7 ; 563-571 |

DLL 37.1, other modules 113.6, mapped 16.9 MB in every snapshot. **Repeatability:** committed within 2.8 MB at the menu
and after the load and within 8.5 MB after 25 turns; the owners that depend on the game within 5 MB (heaps, Lua) and
the build-fixed ones identical; the largest free block varies by 16-22 MB (where a large allocation lands). So one P6
run per build is enough to see a 20 MB change in an owner [I, three runs]. In 25 turns the EXE commits about 145 MB,
115 of it in the heaps (CRT small blocks +60, large blocks +17, the rest process heap and private) and 12 MB in Lua;
the free block 16 MB or larger loses about 160 MB and fragmentation gains 25-35 MB.

**P6 on c5h-c failed first** (07:22-07:45): c5h-c carries a stale `cache/stats.db-journal` of 2026-11-14 03:17 (c5h-a
one of 04:12), the pin writes no stats.db, so `turns.py frontend` reports BUSY for good, the menu wait timed out and the
harness stop ended in "needs a decision"; stopped with the MC's `--coordinator-override` (D60), c5h-c put back by hash
and released (issue 4203 log); the kit run is sealed as `ended_by error` (`20261115T122213Z-ws-7158f6`). The journals
were left for the MC's ticket. A stale journal blocks every front-end check of the harness on that instance [L].

## 5. The owner breakdown (W4, issue 4204) and premise M3

Written by worker-4204, 2026-11-15. Code `perf/ledger/owners/owners.py` (tests `perf/ledger/owners/tests/`, driver
`perf/ledger/owners/m3_drive.py`); evidence `F:/c5-64bit-work/ledger/owners-m3/` (D141).

### 5.1 The tool

`python -I perf/ledger/owners/owners.py PID --out DIR [--label L] [--stats-db PATH] [--lua-heap N]` (or `--instance
NAME`, which takes the pid from the instance's state file). Stdlib and ctypes only, Python 3.8-safe. Read only: the
process is opened with `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`, threads with `THREAD_QUERY_INFORMATION`; it
reads with `VirtualQueryEx`, `GetMappedFileNameW`, `ReadProcessMemory` (PEB, heap headers, the first bytes of each
private allocation, TEBs) and `NtQueryInformationProcess/Thread`. It never suspends, injects or writes. It walks
`[0x10000, 4 GB - 64 KB)` for a large-address-aware 32-bit target (2 GB without the PE flag, read from the image's
header) and `[0x10000, 128 TB - 64 KB)` for a 64-bit one (the server); a 64-bit target from a 32-bit Python is
refused (exit 5). Exits: 0 written, 2 usage, 3 no such process, 4 access denied, 5 bitness, 6 the process ended
during the walk, 7 the output folder cannot be written; each has a test. A walk of the stock EXE takes well under a
second (measured in 5.3). Output in DIR (one function, `write_outputs`, so the ledger's `record.py` call is one line
there): `owners.json` (meta, totals, owners, toolkit owners, items, the method split of each, free figures, modules,
heaps, every region), `regions.csv`, `owners.csv`, `items.csv`, `modules.csv`, `heaps.csv` and `metrics.csv` (metric
ids, 5.2). MB = 2^20 bytes throughout.

### 5.2 Which owner each region goes to, and why `[C]` (the code; synthetic tests in `tests/test_owners.py`)

Every region gets three labels: a coarse **owner** (`dll`, `lua`, `drv`, `exe.image`, `modules`, `heaps`, `mapped`,
`stacks`, `private`, `reserved`, `free`), an **item** in the scheme of the toolkit's Process Memory page (`dll.image`,
`exe.big`, `rsv.heapcrt`, ...; the page's owner is the item's prefix: `dll`, `lua`, `drv`, `exe`, `rsv`, `frag`,
`free`) and the **method** that attributed it. The rules, first match wins:

| region | owner / item | method | why |
|---|---|---|---|
| free | `free`; the largest block is `free.largest`, the rest `frag.holes` | free | the page's split: holes beside the largest block are what fragmentation costs |
| image, committed | by module name: the main image `exe.image`; `CvGameCore_Expansion2*` `dll.image`; `d3d*`, `dxgi*`, `dxcore`, `d3dx*`, `d3dcompiler*` `drv.d3d`; `nv*`, `ati*`, `amd*`, `igd*`, `igc*` `drv.images`; every other image `exe.otherimages` | module-image | `GetMappedFileNameW` on the allocation base |
| image or mapped, reserved | `rsv.sections` | module-image / mapped-name | the page's "shared sections and image tails" |
| mapped, committed | a name with `.nvph` or a driver shader-cache folder: `drv.shadercache`; otherwise `exe.misc` | mapped-name (unattributed when the section has no file name, i.e. pagefile-backed) | |
| private, an NT heap segment | the heap's role (below): CRT `exe.small`, process heap `exe.procheap`, Lua `lua.unprofiled`, "heap 10" and the small heaps `exe.misc`; reserved part `rsv.heapcrt` / `rsv.heap10` / `rsv.heapother` | heap-walk (Lua and heap 10: signature) | the allocation starts with `_HEAP_SEGMENT` (signature `0xFFEEFFEE`) whose `Heap` field points at a heap whose own header carries `_HEAP.Signature` `0xEEFFEEFF` |
| private, a heap's large block | CRT heap: `exe.big`; other heaps as their segments | heap-walk | the allocation is on the heap's `VirtualAllocdBlocks` list (blocks above the heap's virtual-memory threshold, about 508 KB on x86), walked from the heap header after a `Flink->Blink == head` check |
| private, a thread's stack or TEB | committed `exe.misc`, reserved `rsv.stacks` (TEB reserve `rsv.other`) | thread-env | `DeallocationStack` of each thread's TEB (the 32-bit TEB at TEB64 + 0x2000 for a WOW64 target, and the 64-bit one too) |
| private, write-combined or uncached (`PAGE_WRITECOMBINE` / `PAGE_NOCACHE` in the region or the allocation's protection) | committed `drv.writecombine`, reserved `drv.reserve` | signature | CPU-visible GPU buffers; the charging rule (the owner, 2026-09-15) charges known reserve to its owner |
| private, exactly 16 MB, nothing committed, the DLL loaded, the only such allocation | `dll.minidump` | signature | the DLL's `MINIDUMP_EMERGENCY_RESERVE_BYTES`; with two or more candidates none is charged (the count is in `notes`) |
| private, exactly 128 MB with under 2 MB committed, the only one | reserved `rsv.unowned`, committed `exe.private` | signature | the unowned 128 MB reservation of `ownership-and-ceiling.md` 2 |
| private with a guard page, not claimed above | committed `exe.misc`, reserved `rsv.stacks` | signature | stack-shaped (a thread the TEB pass missed) |
| any other private | committed `exe.private`, reserved `rsv.other` | unattributed | |

**Finding the heaps `[L, 2026-11-15, workstation, Windows 11 26100]`.** The 32-bit PEB of the stock EXE lists **one**
heap (`NumberOfHeaps` 1, `ProcessHeaps` = the process heap only; the same in a 32-bit `PING.EXE`), although the
process has 13 NT heaps: on this Windows build the PEB's list is no way to find a WOW64 process's heaps from outside.
The tool therefore finds heaps by their segments: every private allocation whose first bytes are a `_HEAP_SEGMENT`
(`0xFFEEFFEE`) names its heap, and that heap is kept when its own header checks out. The heaps are then numbered by
address (`notes.peb.index_source` says which numbering was used). That number is **not** the DLL's `HeapIndex` (the
order of `GetProcessHeaps` inside the process): the CRT heap was number 4 in one run and 2 in the next, so roles are
given by fingerprint, not by number.

**Heap roles.** The process heap is `PEB.ProcessHeap`; the CRT heap is the heap with the most committed segment bytes
(heap-walk). For `CivilizationV*.exe` two fingerprints (method signature): "heap 10" is the heap that reserves at least
16 MB and more than twice what it commits (`ownership-and-ceiling.md` 1: 72 MB reserved to hold 14 MB; measured here
21.6 MB committed, 77.5 MB reserved); Lua's heap is the largest remaining heap with at least 8 MB committed (measured:
59.5 MB on the load screen, 100.1 MB after the load, 107.7 MB after three turns, the growth Lua shows in
`ownership-and-ceiling.md` 6). `--lua-heap N` overrides the Lua fingerprint by number; for any other image no Lua heap is
guessed. Every heap header is used only when `_HEAP.Signature` reads `0xEEFFEEFF` at the offset for its bitness; its
`TotalFreeSize` (granules of 8 bytes on x86, 16 on x64) is the back-end free list, moved from `exe.small` to
`frag.crtslack` for the CRT heap and from `lua.unprofiled` to `frag.luaslack` for Lua. **No "engine pool" heap was
found** (the hand-made `exe.pool`, 61 MB, "heap index 1, one ~58 MB structured block"): none of the 13 heaps holds 55-65
MB with a single large block, and no allocation of 57-58 MB exists `[L]`; where that block lives in this build is open.

**What a walk from outside cannot split.** (1) The DLL's heap allocations live in the CRT heap beside the EXE's: without
the DLL's allocation hook they stay in `exe.small` / `exe.big`. `--stats-db` moves the `MemHookTags` live bytes of the
latest turn into `dll.*` items (method stats-db) where the DLL writes that table. (2) The heap's busy/slack split is the
back-end free list only: free low-fragmentation-heap slots and block headers count as busy (the hand-made
`frag.crtslack` included overhead from a full heap walk). (3) Lua's per-script items (`lua.vpui`, ...) need the Tier 2
allocator profile; the tool gives `lua.unprofiled`. (4) The driver's private allocations other than write-combined
memory (its heaps, its ordinary `VirtualAlloc`s) are unnamed private memory: the hand-made page's `drv.writecombine`
came from a VMMap capture.

**Metric ids** (in `metrics.csv`, the tool's own file, kept as it was): `owner.<owner>.mb`, `toolkit.<owner>.mb`,
`item.<item>.mb`, `space.{committed,reserved,free,space}.mb`, `free.largest.mb`, `free.below2g.mb`,
`free.largest_below2g.mb`, `free.blocks_ge_1mb.count`, `free.blocks_ge_16mb.count`, `free.blocks.count`. **In the
ledger** (2026-11-15, worker-4203b) the registry's ids are used instead: `exe.owner.*_mb@1` / `server.owner.*_mb@1` and
`exe.item.*_mb@1` (section 5.7); `metrics.csv` is not read by ingest.

### 5.3 Measured snapshots `[L, 2026-11-15 05:40-05:52 EST, workstation]`

Configuration: stock route (the 32-bit DLL in the EXE, no proxy), instance c5h-d (Goldberg copy on F:, pool place
claimed by 4204), DLL `i0ship-26234371-ia` (SHA-256 3C2F47DA...A06C, the current pin, COMMON rule 18), mods cp, vp, eui,
asset set with Vibrant Textures (D94), the instance's own graphics settings. Launched and stopped by the harness
(no-focus launch, guard and start-up watch ok, the harness stop after the save settled). Memory only: the quiet lock was
free but five heavy jobs of other owners ran; address-space figures do not depend on machine load (D106); nothing here
is a timing claim. Driver: `perf/ledger/owners/m3_drive.py`; evidence (owners.json, raw.json, CSVs, drive logs, menu
and load pictures) under `F:/c5-64bit-work/ledger/owners-m3/{menu-2, huge350-1, huge350-2, pack-t000-1}/`.

Saves: **huge** = `C5H-VP8P-HUGE_0350` (Huge Continents 128 x 80, turn 350, SHA-256 c499d3c6...88c3); **pack** = a copy
of the P7 pack game's `PACK-STOCK-T000` (Play The World Extended through the Hormigas map pack, 180 x 94 = 16,920
plots, 21 majors, 32 city states, turn 0; 5c21a29f...; `giant-stress-game.md`), loaded with the pack applied
(`mappack.py apply`, removed again by hash afterwards). **No save of GameId 62 exists** (searched F:/c5-64bit-work,
D:/c5-64bit-work, D:/civ5_archive, D:/civ5_data and the research record); the nearest kin, GameId 88 (`OOM-G88_*`,
180 x 113), is in the owner's saves and its late loads overflow the EXE's message queue (`myths-and-measurements.md` 6), so
it was not used.

| MB (toolkit owners) | main menu | huge, after load (t350) | huge, 3 AI turns (t353) | pack, after load (t0) | pack, 2 AI turns (t2) |
|---|---|---|---|---|---|
| committed | 1842.4 | 2340.7 | 2414.8 | 2485.5 | 2549.6 |
| reserved | 549.2 | 504.9 | 541.1 | 512.2 | 577.6 |
| free | 1704.4 | 1250.4 | 1140.0 | 1098.2 | 968.6 |
| largest free block | 1649.4 | 1194.5 | 1053.4 | 1045.6 | 916.0 |
| free below 2 GB / its largest block | 40.5 / 2.9 | 38.1 / 7.4 | 37.6 / 5.4 | 33.5 / 3.5 | 33.5 / 3.5 |
| free blocks >= 1 MB / >= 16 MB | 7 / 1 | 4 / 1 | 7 / 2 | 4 / 1 | 4 / 1 |
| DLL (image 21.1 + minidump reserve 16.0) | 37.1 | 37.1 | 37.1 | 37.1 | 37.1 |
| Lua (heap, busy part) | 62.3 | 99.4 | 105.5 | 87.5 | 89.1 |
| video driver | 299.8 | 299.8 | 336.1 | 315.8 | 347.8 |
| EXE and the rest | 1490.1 | 1943.8 | 1985.5 | 2085.9 | 2146.1 |
| reserved without a known owner | 492.1 | 462.8 | 458.3 | 466.3 | 500.4 |
| fragmentation (heap free lists + holes beside the largest block) | 65.0 | 58.4 | 120.0 | 57.7 | 59.4 |

- Each walk took 0.18-0.45 s for 6,800-7,500 regions and 158-160 threads (`meta.walk_s`).
- **Repeatability (same save, same pin, two launches, huge350-1 and huge350-2):** committed 2350.7 / 2340.7 after the
  load (10 MB apart) and 2435.3 / 2414.8 after three turns (20.5 MB); every image item, `dll.minidump`, `rsv.unowned`,
  `rsv.sections`, `exe.big` identical or within 0.6 MB (`rsv.stacks` within 2.4 MB); `exe.private` 182.4 / 170.5, `exe.procheap`
  165.9 / 159.3, `rsv.other` 158.1 / 142.0, `drv.reserve` 57.7 / 25.5 after one turn (it moves, below). huge350-1's
  after-load snapshot was taken before the heap fingerprints existed (Lua then in `exe.misc`); its other items count.
- **The 128 MB unowned reservation** sits at **0x1A020000** in every snapshot (0.59 MB committed), the base of
  2026-09-17 (`ownership-and-ceiling.md` 2), not 0x10020000.
- **The large-block mass is set mainly by the map** (corrected 2026-11-15 by worker-4203b; it said "fixed by the map,
  not the turn", from three turns): `exe.big` (the CRT heap's `VirtualAlloc`ed blocks) is 639.9 MB at the menu, 852.3
  MB on the 128 x 80 map (862.8 three turns later, 869 after 25 turns, P6 4.5) and 985.1 MB on the 180 x 94 map at turn
  0 (987.8 at turn 2), but over a long game it grows: **1,036 MB at turn 2 to 1,132 MB at turn 275** on the P7 seeds
  (about 0.35 MB per turn, 5.8) `[L]`. The 2:1 pattern-filled pair (`ownership-and-ceiling.md` 5, open question Q13) is 118.8 + 59.4 MB
  on the 128 x 80 save (GameId 47's 118.75 + 59.38 on a 128 x 80 map) and **137.9 + 69.0 MB on the 180 x 94 map**
  (GameId 62's 180 x 113: 164.53 + 82.27): a third map size for the cell formula.
- **The driver's reserve grows in play:** `drv.reserve` (write-combined or uncached allocations, reserved part) is
  26 MB after the load and 57-67 MB from the first played turn on; `drv.writecombine` (committed) 33-38 MB in game,
  23 MB at the menu.
- Lua's heap, committed: 59.5 MB on the load screen (a debug snapshot of huge350-1), 70.6 at the menu, 100.1 after the
  huge load, 107.7 three turns later; 87.9 and 92.7 on the pack map.

### 5.4 Against the hand-made point (huge map t344, GameId 62)

Script: `python -I perf/ledger/owners/compare.py <owners.json> --points <the toolkit's process_memory.json> --point
t344` (reads the owner's file in place); tables in `F:/c5-64bit-work/ledger/owners-m3/{huge350-2,pack-t000-1}/compare-t344-*`.
The hand-made point is GameId 62 (180 x 113, 20 majors, turn 344 after 344 turns of play, 2026-09-15, a September DLL
with the memory instrument, a VMMap capture for the items marked est). Cause: **=** agrees; **G** a different game or
turn; **B** a different build or environment; **M** the method; **E** an estimate on the hand-made page.

| item (MB) | hand-made t344 | huge t353 | pack t2 | cause |
|---|---|---|---|---|
| `dll.image` | 20.8 | 21.1 | 21.1 | = (B: another DLL build, +0.3) |
| `dll.minidump` | 16.0 | 16.0 | 16.0 | = |
| DLL heap tags (`dll.db`, `untagged`, `danger`, `save`, `ai`, `turn`) | 235.8 | - | - | M: needs the DLL's hook; here inside the CRT heap's items |
| `dll.hook` + `dll.hookreserve` | 40.0 | 0 | 0 | B: the pinned DLL has no instrument |
| Lua heap committed (`lua.*` + `frag.luaslack`) | 141.2 | 107.7 | 92.7 | G (Lua grows with play) |
| ...of it `frag.luaslack` | 45.3 | 2.2 | 3.6 | M: back-end free list only |
| `drv.images` | 209.0 | 209.0 | 209.0 | = |
| `drv.d3d` | 8.1 | 10.8 | 10.8 | M: the tool's D3D family also holds `d3d12core` (2.8) and `d3dx9_42` (1.9) |
| `drv.writecombine` | 65.6 est | 33.4 | 34.8 | E (VMMap; 44.2 in another capture) |
| `drv.reserve` | 30.4 est | 66.8 | 61.2 | E, and it grows in play (5.3) |
| `drv.shadercache` | 16.0 est | 16.0 | 32.0 | = (the pack game mapped a second `.nvph` cache) |
| `exe.image` | 101.1 | 101.1 | 101.1 | = |
| `exe.otherimages` | 119.5 | 113.6 | 113.6 | B: Goldberg's `steam_api_gbe.dll` (15.7) in place of Steam's DLLs (24.5) |
| `exe.big` | 996.3 | 862.8 | 987.8 | G: map size; the 180 x 94 map at turn 2 is within 8.5 MB (0.9 %) |
| `exe.small` + `frag.crtslack` + DLL heap tags | 1039.2 | 565.5 | 564.7 | G: growth of 344 turns (the DLL's 236 MB included) |
| `exe.procheap` | 178.6 | 159.3 | 166.2 | G |
| `exe.pool` | 61.2 | - | - | M: no such heap found (5.2); its bytes are elsewhere |
| `exe.private` | 228.3 est | 166.4 | 167.7 | E: the page's residual from VMMap; here every private byte is counted |
| `exe.misc` | 70.6 est | 48.1 | 48.0 | E |
| `rsv.heapcrt` | 38.5 | 13.6 | 16.8 | G |
| `rsv.heap10` | 73.2 | 77.5 | 77.7 | = (+4.3, 6 %) |
| `rsv.heapother` | 30.8 | 24.2 | 32.2 | G |
| `rsv.unowned` | 127.4 est | 127.4 | 127.4 | = |
| `rsv.stacks` | 42.3 est | 50.3 | 50.3 | E: the page's figure is a VMMap estimate; here 158 threads read from their TEBs |
| `rsv.sections` | 23.5 est | 23.2 | 23.2 | = |
| `rsv.other` | 180.3 | 142.0 | 172.8 | the residual: G and E |
| committed / reserved / free | 3294.3 / 563.6 / 238.0 | 2414.8 / 541.1 / 1140.0 | 2549.6 / 577.6 / 968.6 | G |
| toolkit owner `dll` / `lua` / `drv` | 312.6 / 95.9 / 329.1 | 37.1 / 105.5 / 336.1 | 37.1 / 89.1 / 347.8 | M (DLL heap share) / G / = within 6 % |

### 5.5 Tolerance and verdict on M3

**Tolerance (stated after the first measurement, for the lead to adopt or change):**

1. Items fixed by the build and the environment (every image item, `dll.minidump`, `rsv.unowned`, `rsv.sections`,
   `rsv.heap10`, `drv.shadercache`, and `exe.big` on the same map size): **within 5 MB or 5 %, whichever is larger**.
   Measured: every such item lies within it (0.0 to 4.3 MB) except where a named cause (B, M) explains the gap.
   (2026-11-15, worker-4203b: `exe.big` belongs here only on the same map size **and turn range**: it grows about 0.35
   MB per turn over a long game, 5.3 and 5.8; `score.py` still lists it under "fixed" with that cause.)
2. Owner totals that depend on the game (CRT heap, Lua, process heap, private memory, the reserved residual, free): **only
   on the same save and turn, within 5 % or 20 MB per owner, whichever is larger**. Two launches of the same save repeat
   within 0.8 % in committed (20.5 MB) and 16 MB per item (5.3), so the band holds the method's own noise.
3. Not comparable by construction (M): the DLL's heap share, Lua's per-script items, heap slack beyond the back-end free
   list, the engine pool, and the page's estimates (E). These are reported, never scored.

**Verdict on M3: partly met `[L]`.** A walk sorted by module, heap segment, signature and thread reproduces every
hand-made owner that it can see by construction, within the tolerance of point 1: the EXE and driver images, the DLL
image, the minidump reserve, the unowned 128 MB reservation, heap 10's reserve, shared sections, and the large-block
mass on a map of the same class. It **cannot** reproduce the DLL's share of the heap (236 MB of the page's 313 MB DLL
at t344) without the DLL's own hook, and the pinned DLL carries none; it books that share under the EXE. The owner
totals of t344 themselves (point 2) **could not be checked**: no save of GameId 62 exists, and the two saves used are
other games at other turns (their differences are G in 5.4). DoD item 4 of the proposal needs a same-save check: either
a save of the P7 giant game near turn 344 measured by this tool and by the instrumented DLL in the same run, or a new
hand-made point taken from the stock save the kit uses. For comparing stock with the patched game the gap matters
little: in the patched game the DLL leaves the EXE, so the EXE's walk is complete there, and the server's own walk
(64-bit, the same tool) shows where the DLL went.

### 5.6 What a walk from outside cannot attribute

- **The DLL inside the CRT heap.** The DLL's `operator new` and the EXE share msvcr90's heap; only the DLL's hook
  (`MemHookTags`, the memory-diagnostics DLL `eeb9305e1` and its descendants) separates them. The pinned
  `i0ship-26234371-ia` writes **no `stats.db` at all** `[L]`: the instance's `cache/` holds none after the runs. So
  `--stats-db` (tested on synthetic tables only) is idle on the current pins; a labelled comparison arm with the
  instrumented build (`<pins_dir>/dlls/base-instr-eeb9305e`, not a pin) could fill it.
- **Heap slack.** `TotalFreeSize` is the back-end free list: 1.9-31.2 MB in the CRT heap here, against the page's 188.5
  MB at t344, which came from a full heap walk counting free blocks and overhead after 344 turns. The difference is both
  G and M; a full walk from outside would have to decode the heap's encoded entry headers.
- **Lua per script** (`lua.vpui`, `lua.eui`, ...): the Tier 2 allocator profile only.
- **The driver's private memory** other than write-combined or uncached pages (its heaps, ordinary `VirtualAlloc`s):
  unnamed private memory (`exe.private`). The page's driver figures for these were VMMap estimates.
- **The engine pool** of the page (61 MB, "heap index 1") is not a heap of its own in these processes (5.2).
- **About 170 MB of unnamed private memory** (`exe.private`; largest pieces 30.5, 29.6, 27.8 MB of float and zero data
  and a 96 MB allocation with 13 MB committed at 0xF6xx0000) and **140-175 MB of reserve with no owner** (`rsv.other`,
  among it four 32 MB reservations). Naming them needs an allocation trace (EngineAllocTrace-style, `ownership-and-
  ceiling.md` 2), not a walk.
- **Heap numbers.** The DLL's `HeapIndex` cannot be reproduced from outside on this Windows build (5.2); compare heaps
  by role, never by number.

### 5.7 The ledger's owner ids and the usable-free-block threshold (wave 2, worker-4203b, 2026-11-15) `[C]`

`owners.py` `ledger_values(result)` maps a walk (a fresh result or a saved `owners.json`) to the registry's ids of
CONTRACT.md 6; `write_outputs(result, out_dir, ledger={...})` and the command line's `--ledger --issue I --config C
[--scenario S --grade G --turn N --ledger-root R]` also write one sealed run folder through `perf/ledger/record.py`
(`series-owners.csv` one row per checkpoint with the checkpoint as `label`, a `checkpoint` event, the snapshot's files
under `owners/<label>/` without `raw.json`). Exit 8: the snapshot is written but the ledger run was refused (printed).
Several snapshots go into one run with `ledger_record_snapshots(rec, snaps)` (P6, P7, the M3 drives).

- **Usable-free-block threshold: 16 MB.** A free block of 16 MB or more is headroom (`exe.owner.free_mb`), a smaller one
  is fragmentation (`exe.owner.fragmentation_mb`, together with the CRT and Lua heaps' back-end free lists,
  `frag.crtslack` + `frag.luaslack`). Why 16 MB: it is the size class the registry already counts
  (`exe.free_blocks_16mb@1`, memsampler and the harness sampler), so the owner ids and the block count read the same
  blocks; the blocks the P7 seeds showed at or above it are the large block above 2 GB (554-942 MB) and at most three
  others, while the holes under 16 MB are 600-700 pieces, most of them 64 KB or less (5.3) [I]. Stated in the
  definitions of the eleven `exe.owner.*` ids at **version 1** (nothing had been ingested with them, so the v1 text was
  completed rather than a v2 added; CONTRACT.md 11 point 4 left it open for this). (Since 0362, 7.4: the ledger's fine
  items follow this threshold too; the toolkit's split below stays in each snapshot's `owners.json`.) It differs from the toolkit page's
  `free`, which is the largest block alone; the page's split stays available as `exe.item.free_largest_mb` and
  `exe.item.frag_holes_mb`.
- **Owner mapping** (sum = `exe.owner.space_mb`, rounding within 0.1 MB, tested): `dll.*` -> dll (image, minidump
  reserve, the heap tags where `--stats-db` refines); `lua.*` -> lua; `drv.*` -> driver (images, D3D, write-combined
  committed and reserved, shader caches); `exe.image` -> exe_image; `exe.otherimages` -> other_modules; the committed
  mapped part of `exe.misc` (regions of owner `mapped`) -> mapped; `exe.big`, `exe.small`, `exe.pool`, `exe.procheap`,
  `exe.private` and the rest of `exe.misc` (heap 10, small heaps, stacks, TEBs) -> heaps; `rsv.*` -> reserved; free
  blocks by the threshold; the heap free lists -> fragmentation.
- **The server** (a 64-bit walk, `meta.target_bits` 64): the same eleven owners as `server.owner.*_mb@1` (family B); no
  fine items (the page's items describe the EXE). Its `free` is nearly the whole 128 TB window: context, not headroom.
  Not run on a live server yet [C only].
- **Fine items**: the 34 items the tool can produce as `exe.item.<key with . as _>_mb@1` (registry, appended): the
  DLL's image and minidump reserve and its seven heap tags, `lua.unprofiled`, the five driver items, `exe.image`,
  `exe.otherimages`, `exe.big`, `exe.small`, `exe.pool`, `exe.procheap`, `exe.private`, `exe.misc`, the seven `rsv.*`,
  `free.largest`, `frag.holes`, `frag.crtslack`, `frag.luaslack`. Not produced (no id): `dll.hook`, `dll.hookreserve`
  and Lua's per-script items.

### 5.8 Owner snapshots on P7 and DoD item 4 (wave 2, worker-4203b) `[L, 2026-11-15, workstation]`

The MC accepted (entry 1542) that DoD item 4 is scored on a P7 stock seed near turn 344 instead of the hand-made point's
own save (none exists, 5.5): `p7_giant.py` snapshots the live EXE (read only) at turn 0 after the load, every 25 turns
and at turn 344; for seeds 1 and 2, started with older code, a detached `owners-keeper` does it (4.3). Each snapshot is
compared with t344 (`compare.py`, `compare-t344.txt` beside it) and scored with `perf/ledger/owners/score.py` (the
tolerance of 5.5: fixed items within 5 MB or 5 %; game owners within 20 MB or 5 %; the rest reported). The scored point
is the snapshot at turn 344, or the last one before a crash when the seed dies earlier.

**Snapshots taken** (2026-11-15 to 09:00 EST; each walk 1.2-2.2 s; files `F:/c5-64bit-work/ledger/pending/p7-stock-seed<N>/owners/c<checkpoint>-t<turn>/` with `owners.json`, `compare-t344.txt`, `score-t344.json`): seed 1 at turns 161 (checkpoint 150, the keeper's first), 175, 200, 225, 250, 275; seed 2 at turns 2 (checkpoint 0), 25, 50, ..., 250. Both keepers keep going (300, 325, 344) and end with their game.

| seed, turn | committed | free (largest) | exe.owner free / fragmentation | heaps | lua | exe.big | fixed items within tolerance | game owners within tolerance |
|---|---|---|---|---|---|---|---|---|
| 1, t161 | 2792 | 706 (554) | 643 / 108 | 2115 | 98 | 1078 | 9 / 11 | 3 / 9 |
| 1, t200 | 2837 | 657 (512) | 594 / 103 | 2167 | 101 | 1094 | 9 / 11 | 3 / 9 |
| 1, t250 | 2910 | 573 (406) | 512 / 107 | 2239 | 103 | 1115 | 9 / 11 | 3 / 9 |
| **1, t275** | **2975** | **508 (295)** | 443 / 106 | 2304 | 104 | 1132 | **9 / 11** | **4 / 9** |
| 2, t2 | 2578 | 988 (942) | 942 / 54 | 1951 | 96 | 1036 | 9 / 11 | 2 / 9 |
| 2, t100 | 2719 | 888 (786) | 839 / 79 | 2051 | 96 | 1055 | 9 / 11 | 2 / 9 |
| 2, t200 | 2847 | 678 (520) | 619 / 106 | 2162 | 100 | 1091 | 9 / 11 | 4 / 9 |
| 2, t250 | 2906 | 603 (386) | 544 / 103 | 2229 | 101 | 1117 | 9 / 11 | 4 / 9 |

**The comparison so far (seed 1, turn 275, the latest; the scored point moves to turn 344 or to the last snapshot before
a crash):**

- **Fixed items: 9 of 11 within 5 MB or 5 %** (`dll.image` +0.3, `dll.minidump` 0, `drv.images` 0, `drv.d3d` +2.7,
  `drv.shadercache` 0, `exe.image` 0, `rsv.unowned` 0, `rsv.sections` -0.4, `rsv.heap10` +3.9). The two misses have
  named causes: `exe.otherimages` -8.6 MB (B: Goldberg's `steam_api_gbe.dll` instead of Steam's DLLs), `exe.big` +135.9
  MB (another map, 180 x 94 against 180 x 113, and it grows with the turns: 1,036 MB at turn 2 to 1,132 at 275, so it is
  not build-fixed in a long game; corrected in 5.3 and 5.5).
- **Game owners: 4 of 9 within 20 MB or 5 %**: the CRT heap with the DLL's share (1,937 against 2,036, -98 within
  102), the driver (+2.7), all reserved (+24.6 within 25.8) and the process heap (-2.3). Misses, all G (another game, 69
  turns earlier, 2,975 MB committed against 3,294): Lua's heap (106 against 141; the page's 45 MB of Lua slack is a full
  heap walk, M), free (508 against 238), committed (-320), reserved (+50).
- **Reported, not scored:** the DLL's heap tags (236 MB on the page; inside the CRT heap here), `dll.hook`, the engine
  pool (61 MB, not found), Lua per script, heap slack (`frag.crtslack` 40 against 188.5), the page's VMMap estimates
  (`drv.writecombine`, `drv.reserve`, `exe.private`, `exe.misc`, `rsv.stacks`, `rsv.other`).
- **Verdict so far [L, I]:** the walk reproduces every build-fixed owner within the tolerance on a live giant game at
  turn 275, as it did on the short saves (5.5); for the game-dependent owners the P7 seeds at turn 275 are a lighter game
  than GameId 62 at turn 344 (seed 1 commits 320 MB less), so their misses measure the games' difference, not the tool.
  To score at turn 344: `python -I perf/ledger/owners/score.py F:/c5-64bit-work/ledger/pending/p7-stock-seed<N>/owners/c0344-t0344/owners.json --points <the toolkit's process_memory.json>` once a keeper has taken it (or the last `c*` folder before a crash).

## 6. The explorer page and the scorecards (W5, issue 4205)

Written by worker-4205, 2026-11-15. Code `perf/ledger/page/` (`build.py`, `data.py`, `stats.py`, `scorecard.py`,
`template.html`, `app.js`, `app.css`); tests `perf/ledger/page/tests/` (29, hermetic; 44 since 4207's compare
tests, 2026-11-15); scorecards
`perf/ledger/milestones/`; the tracker tab as a patch for the MC, `perf/ledger/page/tracker-tab.patch`. Marks: the
design is `[C]` (code and tests); the page checks are `[L]` in a browser pane on the workstation.

### 6.1 Commands

```
%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe -I perf/ledger/page/build.py \
    --db F:/c5-64bit-work/ledger/index.duckdb --out F:/c5-64bit-work/ledger/page/ [--milestones DIR] [--plotly FILE]
<venv> -I perf/ledger/page/scorecard.py freeze --milestone NAME --title TEXT [--runs RUN_ID ...] \
    [--column NAME=CONFIGURATION ...] [--state NAME=TEXT ...] [--stock NAME] [--build NAME] [--note T] [--by WHO] \
    [--db F] [--dir perf/ledger/milestones] [--force]
<venv> -I perf/ledger/page/scorecard.py verify perf/ledger/milestones/NAME.json [--db F]
<venv> -I perf/ledger/page/scorecard.py show perf/ledger/milestones/NAME.json
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 <venv> -I -m pytest perf/ledger/page -q
```

Exit codes (both tools): 0 done; 1 refused or failed (no index, a file that is not a ledger index, Plotly not found, a
malformed scorecard, output not writable; freeze: unknown run, a run whose configuration has no column, an existing
file without `--force`; verify: a difference); 2 usage; 3 duckdb missing. Each path has a negative test.

### 6.2 What the page is

- **One file**, `<out>/index.html`, built from the index alone: the index is opened read-only, no run folder is read.
  Plotly.js **3.3.1** is inlined from the venv's PyPI `plotly==6.5.2` (`plotly/package_data/plotly.min.js`, 4,838,938 B;
  the MC's yes, entry 1512); the data is one JSON block (`c5-page/1`, no `<` inside, so it cannot close its script
  element). No request at view time: no `src=`, no `href=` to a file. It runs under the CSP `default-src 'none';
  script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:` with no console error `[L, 2026-11-15]`
  (Plotly 3.3.1 needs no `unsafe-eval` for the traces used: pie, scatter, bar, box).
- **Size** `[L, 2026-11-15]`: 4,963,532 B (4.73 MB) from the worked example (2 runs); 5,174,048 B (4.93 MB) from the
  example plus the 12 sealed runs of 4203b under `F:/c5-64bit-work/ledger/runs/` (15 owner checkpoints). Plotly is
  4.84 MB of it; the data grows with the runs (memory series are reduced to one point per run and turn).
- **Never committed, never published** (D27, D83): the page and the index live on F: (D141).
- **Routes** (hash, as in the toolkit's explorer; a view's choices after `?`, e.g.
  `#compare?dim=configuration&a=stock-32&b=inc1-thin`): `#process-memory`, `#memory`, `#strip`, `#load`, `#turns`,
  `#ui`, `#frames`, `#compare`, `#milestones`, `#runs`. **Every view ends with a table of its runs**: run id and label,
  configuration, build (binaries' SHA-256 prefixes, else the repository commit), machine alias, trust grade, date,
  scenario, how it ended, and tags `not for timing`, `backfilled` (the source in the tooltip), `quiet lock`.

| Route | Shows | Data (index) |
|---|---|---|
| Process memory | two donuts (A and B) of the whole space walked: inner ring the toolkit owner (DLL, Lua, video driver, EXE and the rest, reserved, fragmentation, headroom unfilled), outer ring the items; below, the table owner by owner with A MB, % of space, B MB, % of space, change MB, and the space not accounted. A and B: any checkpoint of any run, or the mean of a configuration's runs at one checkpoint label | `series` rows `exe.owner.*_mb` and `exe.item.*_mb` (also `server.*`); the outer ring uses the fine items where a point has them (owner = the key's first part: `exe.item.dll_image_mb` -> `dll.image`), else the owner ids mapped as in CONTRACT.md section 6 |
| Memory over turns | three panels on one turn axis: committed (with the 4,096 MB line), free, largest free block; one curve per run, colour by configuration, dash by run within it; the events (load end, checkpoint, loud line, leader screen, note, exit) as marks; the forecast turn as a dotted line when it lies in the plotted range (else listed); the CTD marker; the outcome table; the note that the arms play different games | `series` `exe.committed_mb`, `exe.free_mb`, `exe.largest_free_mb`, `exe.private_mb` (highest version), **one point per run and turn = the last sample of the turn**; `events`; `metrics` `exe.forecast_out_turn`; `runs` outcome |
| Address-space strip | one bar of the whole space per checkpoint, split by owner | as Process memory. By owner, not by address: the regions' order is in each run folder's `regions.csv`, which the index does not hold |
| Load | the consecutive phases stacked per configuration (mean over runs) plus "rest of process to control"; each family C metric's distribution per run | `metrics` family C. Only `load.process_to_menu_s`, `load.mods_activation_s`, `load.issued_to_done_s`, `load.done_to_control_s` are stacked (section 1, M2: 64.4 + 37.7 + 60.5 + 2.1 = 164.7 of 165.0 s); the others overlap them. From 4203b's runs: 74.2 + 40.9 + 69.0 + 1.9 s, rest 0.5 s (workstation, not for timing) |
| Turns, UI latency, Frames | per metric (selector): a box per run with its points and the mean with its 95 % interval; a table n, mean, sd, median, min, max, interval | families D, E, F: scalars (one value per run), series rows, event-metric rows (one per timed action) |
| Compare | two groups of runs (6.3) | per-run values (6.3) |
| Milestones | each committed scorecard as frozen: P7's line, the columns with their runs (id, run.json SHA-256 prefix, build, machine, grade, date, flags) and every number with n and its interval | `perf/ledger/milestones/*.json` |
| Runs | every run in the index; the lower metric versions left out of the views | `runs` |

Timing views (load, turns, UI, frames) hide runs that are `not_for_timing` by default; a box shows them as
"indication", greyed. Empty states name the tool that fills the view (the `EMPTY` texts in `data.py`). Colours: the
dataviz reference palette, light and dark steps; owners DLL green, Lua blue, driver red, EXE aqua, reserved violet,
fragmentation grey, headroom unfilled (validated: adjacent CVD 6.5-6.9 in the warn band, carried by the ring labels,
the 2 px gaps and the table); configurations stock-32 orange, inc1-thin blue, inc2 aqua.

### 6.3 The statistics (PERFORMANCE.md section 2 item 3; `stats.py`, computed at build time)

- Per arm: n, mean, standard deviation (n - 1), median, min, max, the **95 % interval of the mean with Student's t at
  n - 1 degrees of freedom**; n = 1 gives no spread and no interval (shown as "–", never 0).
- Two arms: the difference B - A (and in % of A), **Welch's 95 % interval** (Welch-Satterthwaite df, fractional), and
  the **smallest detectable difference = (t(0.975, df) + t(0.80, df)) x SE**: a two-sided test at 5 % with 80 % power.
  For large df this is the 2.8 x SE of `perf/turnbench`; with few runs it is wider. Fewer than two runs in an arm: the
  difference only, with the reason. The reading says "the interval excludes 0" or "the interval holds 0: no difference
  shown (not 'equal')".
- The t quantile is computed (regularised incomplete beta, bisection), not read from a table: checked against
  t(0.975; df 1, 2, 4, 5, 30, 10^6) and t(0.80; df 4, 10) to 2e-6.
- **Groups** ("any two groups"): by configuration, by build (configuration + binaries), by pair (`pair_id | arm`: a
  before/after pair), by series (`series_id | arm`), by machine. Every ordered pair of groups of a dimension is computed
  at build time (k groups: k(k-1) comparisons). A comparison of runs not grouped this way needs a series or pair id.
- **Values compared**: the run's scalars (`metrics`), the outcome (`outcome.end_turn`, `outcome.last_free_mb`,
  `outcome.last_largest_free_mb`) and, for series and event metrics of families D, E, F, the run's mean
  (`mean(<id>@<v>)`). Only one version of an id is ever compared (keys carry the version).
- **Memory never mixed with timing**: two tables (registry `timing` false / true). **Timing refused** unless every
  run of both arms that has the metric held the quiet lock (`quiet_lock` true), ran on `oldpc` or `laptop` and is not
  `not_for_timing` (D106): the arms' numbers are shown greyed, no difference is computed, the reason names each run
  and what it lacks (`not_for_timing (D106)`, `quiet_lock false`, `machine X is not a quiet testbed`; five named, then
  a count). The scorecards use the same rule (`data.timing_refusal`). (Replaces, 2026-11-15, 4207 / 7000's review B1:
  refused only for `not_for_timing` runs, so a run on `unknown` or without the lock counted.)
- **The trust grade shown** `[C]` (2026-11-15, 4207, 7000's review B1): **grade A only when both arms are in one
  `series_id`, on one machine, with one scenario name, scenario version, save, `ai_logging` and `stats_logging`, and at
  least 6 runs per arm**; otherwise the lowest run grade capped at B, with `mixed` naming each broken condition and the
  values each arm holds (shown as a warning list on Compare). A row whose metric has fewer than 6 values in an arm is
  capped at B by itself. Series arms are paired only within one `series_id` (on the real index: 21 series arms, 420
  ordered pairs before, 62 within one series now). The page says that only grade A accepts or rejects a change. A warning when the
  arms ran different scenarios. (Replaces: the grade was the lowest run grade alone.)
- **The review's case on the real index `[L]` (2026-11-15 09:39, rebuilt index, read-only).** `#compare?dim=
  configuration&a=headless-x86&b=headless-x64`: 78 against 42 runs, every run grade A with the quiet lock. Before:
  grade **A**, `turn.ai_mean_s@1` B - A = -1.64 s "significant", pooled over 5 series of perf 7010/7013, huge and
  Standard, AI logging on and off, oldpc and laptop. After: grade **B** ("capped at B, the arms mix: series_id, machine,
  scenario, save, ai_logging"; scenario version and stats_logging are one value and not named); every row graded B.
  In the built page: configuration 12 results (B 2, C 10), series 62 (A 38, B 24), build 56 (A 2, B 18, C 36), machine 6,
  pair 2. **Open for 7000 `[I]`:** all 24 series results at B are capped for `ai_logging` alone: the on/off arms of
  7010-B, 7010-C and 7013-B, whose treatment *is* AI logging; under the rule as written they can never be grade A. If 7000 wants a logging series
  graded A, the rule needs a declared exception (not built). **Built 2026-11-15 by issue 0362 (section 7.10):** the
  declared treatment `identity.treatment`, with 7000's B1 conditions (same configuration and build; for AI logging the
  same game through the last turn, `scenario.same_game_through`); with the three series re-converted the 62 series
  results are A 42, B 20: 7010-C's same-build on/off pairs are A, 7010-B's and 7013-B's are B "different games" (on a
  scratch copy of the store until the MC applies it, 7.12).

### 6.4 The CTD rule (proposal 9.3)

- A run with `outcome.ended_by = crash` gets a marker at `outcome.turn`; its text is **"CTD, out of address space"**
  when the last sample's largest free block is under **64 MB** (the threshold of `p7_giant.py`), else "CTD". The marker
  sits at the end of the run's curve in each panel (the last sample's committed, free and largest free block, else the
  curve's last point); its tooltip gives the last sample and what the crash left.
- **Range over seeds**: crashed runs of one configuration and one scenario (name and version) with at least two
  crashes give a band from the earliest to the latest crash turn, labelled "CTD range t a-b (n runs, configuration)".
- The view's note: the curves are lined up by turn; after a while two arms play different games, so they read as
  memory against turn, not as the same game state.

### 6.5 The scorecard's frozen format (`c5-scorecard/1`)

One JSON file per milestone, `perf/ledger/milestones/<milestone>.json` (LF, sorted keys): `schema`, `milestone`,
`title`, `frozen_utc`, `frozen_by`, `index` (the index it was frozen from; empty when no run was read), `contract`
(`c5-run/1`), `stock_column`, `build_column`, `columns` (in order; each: `name`, `configuration`, `state` (`measured`,
`no runs`, or the `--state` text), `runs` (each: `run_id`, **`run_json_sha256`**, label, configuration, build, machine,
grade, not_for_timing, quiet_lock, started_utc, backfilled and source, scenario and version, map size, ended_by,
end_turn, crash_left, last sample), `group` (the group facts as on Compare), `numbers` (`{key: {n, mean, sd, median,
min, max, ci95, unit, timing, better, refused}}`)), `lines` (P7's line) and `note`. P7's line: "stock: CTD[, out of
address space,] at turn x" (several runs: "at turns a-b (k of n runs)"; no crash: "no CTD (ended_by, last turn t)"),
then "this build (NAME): turn T reached with N MB free (largest free block L MB), forecast turn y; x+100 = x' reached /
NOT reached" (since 0362, 7.5: T is the build's WORST run, named with "by its worst of k runs (run_id)", and the forecast
names its version, "forecast turn y (exe.forecast_out_turn@v[, mean of k runs])"; replaces: the best run and an
unlabelled `@1` mean), or the column's state ("not built"). `verify` recomputes every number from a later index and compares the
run.json hashes; any difference exits 1.

**First scorecard** `[C, 2026-11-15]`: `first-playable-0324.json`, "First playable build (0324) against stock",
columns `stock` (stock-32) and `0324` (inc1-thin). Frozen **without runs**: P7 stock seeds 1 and 2 were still running
(seed 1 at turn 175 at 06:19 EST) and no stock P7 run was sealed into the ledger; 0324's build does not exist. Line:
"stock: pending: ...; this build (0324): not built". **To do** (lead-0330 or 4203): when P7 stock's runs are sealed and
ingested, `scorecard.py freeze --milestone first-playable-0324 --title "First playable build (0324) against stock"
--runs <P7 stock run ids> --column stock=stock-32 --column 0324=inc1-thin --state "0324=not built" --force`, and again
with 0324's runs once its build exists.

### 6.6 Data it was built from; what is empty and why `[L, 2026-11-15 06:10-06:40 EST]`

- `F:/c5-64bit-work/ledger/index.duckdb` **did not exist** (4202's adapters and backfill run in parallel). The page at
  `F:/c5-64bit-work/ledger/page/index.html` was built from a scratch index on C: (`C:/c5-64bit-work/0330/4205/
  combined.duckdb`, not kept) made from the worked example **plus the 12 sealed runs under
  `F:/c5-64bit-work/ledger/runs/2026-11/`** (4203b's P1, P4, P5 and M3 runs, all `ws`, grade C). Those 12 runs carry
  metrics that are only in the registries of the branches `task-4203-profile-kit-2` (`exe.item.*`, `load.*`,
  `camera.*`, `save.*`, `footprint.*`) and `task-4202-ledger-adapters`: with this branch's registry, 11 of them are
  refused at ingest ("unknown metric or version"). For the check they were ingested with `--registry` set to the union
  of those two registries (scratch, not committed). Once the epic branch carries the merged registry, the page builds
  from the real index with no extra step.
- **What shows today:** Process memory and the strip (15 checkpoints of 4203b's runs with fine items, plus the
  example's two with owner ids); Memory over turns (the example's two synthetic P7 runs: stock's CTD marker at turn
  317, "out of address space"; the thin client to 417; forecasts 321 and 1,210); Load (the P1 runs' phases, shown as
  indications: all from `ws`); Turns (the example's `turn.ai_s`, `turn.ai_mean_s`); Frames (P5's `camera.*` scalars, as
  indications); Compare (configurations, builds, the example's pair, machines); Milestones (the frozen card); Runs.
- **Empty and why:** UI latency (no family E event metric in the index: P4's frame-probe samples are not written as
  event rows yet); the compositor's frame-time series (no inc1 or inc2 run); a CTD range over seeds (no two crashed runs
  of one configuration and scenario yet); every real memory curve (no sealed P7 run yet: seed 1 still running).
- Owner ids against items in 4203b's runs: at the menu checkpoint the owner ids put 2,128.4 MB in `free` and 172.9 in
  `fragmentation`, the items 2,001 in `free.largest` and 301 in `frag.holes` (the two schemes split free space
  differently: usable-block threshold against "beside the largest block"). The page shows the items where they exist;
  the difference is 4203b's and 4204's to reconcile, not the page's. **Reconciled 2026-11-15 by issue 0362 (7.4):** the
  ledger's items now use the owners' 16 MB split (`exe.item.free_usable_mb@1`, `exe.item.frag_holes_mb@2`).
- The tracker tab: `perf/ledger/page/tracker-tab.patch` adds a "Metrics ledger" link to the tracker's tabs and a
  read-only `/ledger/` route (the file at `C5_LEDGER_PAGE`, default `F:/c5-64bit-work/ledger/page/index.html`, served
  with the CSP above) with a server test. The MC applies it with the epic's merge request and restarts the server
  itself (MC, 2026-11-15).

## 7. 7000's follow-ups of 0330 (issue 0362)

Written by implementer-0362 (account B), 2026-11-15, branch `task-0362-ledger-followups`. Marks: the code is `[C]` (read
in it and its tests: the `perf/ledger` and `perf/profilekit` suites); the counts on the store are `[L]` (2026-11-15
09:55-10:15 EST, workstation; read only on the real store, the re-conversion on a scratch copy). The registry entries of
7.2-7.4 are new versions under 7000's review (PERFORMANCE.md 2.7); none is edited in place (the pins test holds them).

### 7.1 Item 1: the forecast's version on the memory view

`exe.forecast_out_turn@1` (memsampler's projection: largest free block over its slope) and `@2` (stressmap
`forecast.py`: free MB down to the 70 MB death line over the slope of free MB) are different methods. The page took the
highest version per run and showed it unlabelled. Now `data._forecasts` gives each run `{value, metric, version,
others}` and `forecast_versions` (the definitions of the versions present); the memory view labels every forecast line,
the outcome table and the "beyond the plotted turns" note with `(vN)` and adds a note with each version's definition
("never compared"). On the real index three runs carry `@1` (the kit's P2 runs, 1,549-6,949) and three `@2` (9069's
calibrations, 487-986). Restricting the view to one version was not chosen: the two sets come from different tools and
never share a run.

### 7.2 Item 2: `exe.committed_slope`, one method per version

`@1`'s definition is memsampler's (least squares over all per-turn rows), but the adapters also wrote stressmap
calibrate's per-turn fit (another sampler and sample point) and the backfill's document figures (6.54 MB/turn "late
part", 2.476 over turns 20-317: another range per run) under it. Now: `@1` memsampler only (entry unchanged; its
`source` text still names the backfill, which no longer writes it); **`@2`** stressmap calibrate's rows
(`adapters/stressmap.py`); **`@3`** the figure as a document states it (`adapters/backfill.json` points). On the real
store `@1` is carried by 3 memsampler runs, 3 stressmap runs and 4 backfill points; after the re-conversion (7.12) 3 / 3 /
4 under `@1` / `@2` / `@3`.

### 7.3 Item 3: `better` of the EXE's image

`exe.item.exe_image_mb@1` said `none`, `exe.owner.exe_image_mb@1` `lower` (and every other image item `lower`).
**`exe.item.exe_image_mb@2`** is `@1` with `better: lower`; `owners.py` writes `@2` (`ITEM_VERSION`). The owner id is
unchanged.

### 7.4 Item 4: the item and owner free-space splits

The owner ids split free space by the 16 MB usable-block threshold (5.7); the fine items by the toolkit page's "largest
block / every other block", so `free` and `fragmentation` read differently whenever a point had items (the page then
used the items: at 4203b's menu checkpoint 2,128.4 / 172.9 MB by owners, 2,001 / 301 by items, 6.6). Now
`owners.ledger_values` writes **`exe.item.free_usable_mb@1`** (definition in 7000's wording, 2026-11-15, changed in place before any ingest used it:
"every free block of at least 16 MB (the usable-block threshold); equals exe.owner.free_mb@1 by construction") and
**`exe.item.frag_holes_mb@2`** (free blocks < 16 MB; with `frag.crtslack` and `frag.luaslack` equal to
`exe.owner.fragmentation_mb`) instead of `free_largest@1` / `frag_holes@1`: the items nest in the owners (test
`test_ledger_items_nest_in_the_owners`). The toolkit's split stays in each snapshot's `owners.json` (`items_mb`). Why
not the other way round: at the end of a giant game the largest block falls below 16 MB (GameId 62: 4.0 MB at t314),
where "largest block = headroom" stops being true; the 16 MB threshold was stated and accepted (MC entry 1542).
Found on the way and fixed: the backfill wrote the toolkit points' `free.largest` as `exe.owner.free_mb@1` and all of
`frag.*` as `exe.owner.fragmentation_mb@1`, against those ids' definitions. A toolkit point now carries no owner
free/fragmentation (the page gives no 16 MB split) and its free space as `exe.item.free_largest_mb@1`,
`frag_holes_mb@1`, `frag_crtslack_mb@1`, `frag_luaslack_mb@1`; the page's rings take, per toolkit owner, its fine items
where the point has any, else its owner ids (`data.rings`, source "items and owners"): t344 DLL 312.6, Lua 95.9, driver
329.1, EXE and the rest 2,370.5, reserved 516.0, fragmentation 344.2, headroom 127.6 MB, nothing unaccounted.

### 7.5 Item 5: P7's line holds the build's worst run

`scorecard.p7_line` took the build's best run (the highest turn reached). Now the worst (the lowest turn; a run without
a turn counts as 0), named: "this build (b): turn 360 reached by its worst of 2 runs (<run_id>) with 200 MB free ...;
x+100 = 400 NOT reached" (test `test_p7_line_holds_the_builds_worst_run_against_stock`). The forecast in the line names
its version and whether it is a mean. Stock's `x` is the **latest** stock crash (7000's review of 0362, 2026-11-15;
replaces: the earliest), and stock runs that survived are counted: "stock: CTD, out of address space, at turns 290-320
(2 of 3 runs; x = the latest); no CTD in 1 of 3; ..." (test `test_p7_line_uses_stocks_latest_crash_and_names_survivors`).

### 7.6 Item 6: the validator's grade-A rule

A record with `trust.grade` A faults without `identity.series_id` or without `trust.quiet_lock = true`; the seal refuses
it (`SealError`). On the real store no run breaks it (120 grade-A runs, all in a series with the lock; query of
2026-11-15 09:55).

### 7.7 Item 7: `not_for_timing` on `unknown`

The library sets `not_for_timing` true on `unknown` as on `ws` (`record.not_for_timing_of`); the validator faults a run
on `unknown` without it ("timing metric from unknown without trust.not_for_timing (D106): ..." or "a run on unknown
must carry ..."). The page already refused timing from `unknown` (4207). No `unknown` run is in the store.

### 7.8 Item 8: free commit at the start and the end

Optional key `trust.free_commit = {start_mb, end_mb}` (PERFORMANCE.md 2.4): a live run reads the machine's free commit
(`GlobalMemoryStatusEx` `ullAvailPageFile`, as `gate.free_commit_bytes`) at `start()` and at `seal()`; an adapter passes
its source's figures (turnbench's `freeCommitGB` x 1024, also kept as `machine.free_commit_mb@1` rows). Index columns
`free_commit_start_mb`, `free_commit_end_mb` (added by `ALTER TABLE ... ADD COLUMN IF NOT EXISTS`, so an old index takes
them at its next ingest); the runs table shows a "free commit A→B MB" tag. After the re-conversion 120 runs carry it,
start minimum 11,735 MB, end maximum 35,563.5 MB.

### 7.9 Item 9: `trust.window` (CONTRACT 11.1)

Optional key `trust.window = {who, what, by, start_utc, end_utc}`, only on `ws`, `by` = `MC`, the run's start inside
it. With it and the quiet lock held, the library sets `not_for_timing` false and the page counts the run's timing
("machine ws outside an MC timing window" otherwise). `ledger.py new --window WHO` reads the open window of WHO from
`perf/quietlock` (`quietlock.py window`); `record.window_record()` converts such an entry. No tool uses it yet: the first
MC window with a timing series is its first user.

### 7.10 Item 10: the declared treatment

Optional key `identity.treatment` (`ai_logging` or `stats_logging`; needs `series_id`). The page's grade-A check
(`data.declared_treatment`, `data.mixed_fields`) lets the arms differ in that field only when every run of both arms
declares the same treatment, all are in one `series_id`, each arm holds one value of it, **and the two arms match in
configuration and build** (7000's B1, 2026-11-15: before, x64off against x86on of 7010-B was grade A with bitness and
logging both changing; now the exception does not apply and `ai_logging` caps it at B). **For `ai_logging` the arms must
also be the same game** through the scenario's last turn: the optional key `scenario.same_game_through` (the last turn
through which the run's game equals the series' other-logging arm, from `perf/tbjobs/series.py --compare`'s statscmp
reports: the first differing turn minus one, the target turn when IDENTICAL) must be recorded on every run and at or
beyond `scenario.turns.to`; otherwise the comparison is capped at B with the mixed entry "different games" ("same game
through 354 of 355"; PERFORMANCE.md section 4: AI logging changes the game). `stats_logging` needs no such record. Any
other mixed field still caps at B, and an arm that is not one value of the treatment caps too (tests
`test_declared_*`, `test_stats_logging_treatment_needs_no_same_game_record`). The `tbseries` adapter declares
`ai_logging` for a series whose `meta.arms` set logging differently (`:on` / `:off`) and reads `same_game_through` from
the `cmp-<A>-<B>/compare.txt` reports beside `series.json` for every pair of arms of one kind that differ in logging
(the lowest over the pairs; none when no report: test `test_tbseries_same_game_through_from_the_compare_reports`);
`--treatment` for other adapters. In the evidence of perf 7010 `[C]`: 7010-B and 7013-B (huge 350-355) on/off arms
differ first at turn 355 (`MapUnitsState`), so same game through 354; 7010-C (Standard 0-50) on/off arms are IDENTICAL
through 50. Index column `runs.same_game_through`. PERFORMANCE.md 2.7's text is unchanged here: the MC adds 7000's
exception text at the merge.

### 7.11 Index and page on the real store `[L]`

The real index (`F:/c5-64bit-work/ledger/index.duckdb`, SHA-256 prefix `c1856916`, 154 runs) was not changed. The real
page was rebuilt from it with this branch's code at 10:12 EST (7,070,843 B) and again after 7000's B1/B2 at 10:33 EST
(7,076,828 B, 154 runs, 96 owner checkpoints, 1 scorecard): item 1 shows; items 4, 8 and 10 show only after the re-conversion (7.12).

### 7.12 The re-conversion: done on a scratch copy, to do on the real store after the merge

`perf/ledger/adapters/supersede_0362.py ROOT [--move DEST]` moves **exactly the 129 run ids the 13 backfill entries
produce** (7000's B2, 2026-11-15): it builds the entries' run specs as the backfill does (`backfill.entry_run_ids`,
writing nothing) and moves those folders only, never selecting by a run's tool or content, so live series runs and the P7
seeds stay. It refuses, moving nothing (exit 1), when the entries do not produce exactly 129 ids, any of them is not a
sealed folder under `ROOT/runs`, any of them is already in the 0362 form, or `DEST` exists (test
`test_supersede_0362_moves_exactly_the_entries_runs`). On the real store (read only, 2026-11-15 10:25): 129 = 7010-A 30,
7010-B 24, 7010-C 24, 7010-D 18, 7013-B 24, the four points, the two toolkit points, three stressmap runs. (Replaces:
the first version selected by the runs' tool and content.) The backfill's new `--superseded DIR [--why TEXT]` writes
`provenance.supersedes = {run_json_sha256, why}` of the moved record into each re-written run (optional key; 7000's
follow-up).

On a copy of the store's `runs/` (288 MB, `C:/c5-64bit-work/0362/store`, deleted at hand-back), 2026-11-15 10:28-10:31:
`supersede_0362.py <copy> --move <aside>` (129 moved), `backfill.py --root <copy> --only 7010-A 7010-B 7010-C 7010-D
7013-B-loggedon mi-g62-run15 mi-g47-before-fix mi-g47-after-fix mi-g40-run9 toolkit 9069-stock-cal4 9069-stock-tsl150
9069-pack-cal1 --superseded <aside> --why "issue 0362 re-conversion"` (129 written, 0 failures, 129 with
`provenance.supersedes`), `validate` 154 valid, a fresh ingest 154 runs 0 errors 9.78 s, a second ingest 0 ingested.
The page on it, with B1: series comparisons 62 = **A 42, B 20** (before 0362: A 38, B 24). Grade A newly: 7010-C's four
same-build on/off pairs (x64on/x64off, x86on/x86off, both orders; same game through 50 of 50). Still B: 7010-B and
7013-B same-build on/off pairs (4 each, "different games": same game through 354 of 355) and every on/off pair across
bitness (4 per logging series, `ai_logging`: the exception does not apply). Configuration 12 (B 2, C 10), build 56 (A 2,
B 18, C 36), machine 6 and pair 2 unchanged. `same_game_through`: 7010-B and 7013-B 354, 7010-C 50, 7010-A and 7010-D
none (no logging treatment). `exe.committed_slope` `@1` / `@2` / `@3`: 3 / 3 / 4 runs.

**To do on the real store, after the merge** (not done from the branch, so that `main`'s tools never meet records they
cannot validate): `supersede_0362.py F:/c5-64bit-work/ledger --move F:/c5-64bit-work/ledger/superseded/0362` (and a
line in its README.txt), the backfill command above with root `F:/c5-64bit-work/ledger` and `--superseded
F:/c5-64bit-work/ledger/superseded/0362`, then the index moved aside and rebuilt as in 2.3, and the page rebuilt.

## 8. The ledger as a release acceptance record (the owner, 2026-11-15, decision D151; one-off 9071)

Written 2026-11-15 by one-off 9071. The owner (D151, 20:35 EST) [reworded for privacy]: the page was too cluttered; the ledger is to be a release acceptance check showing clearly labelled runs with the client version and measurements for 10 turns afterwards, to measure memory growth per turn. This section replaces the explorer as the page's purpose; sections 1-7 stay as the record of
how the store and its tools were built.

### 8.1 What changed `[C, tests perf/ledger/page/tests/test_acceptance.py]`

- **The store.** The 174 runs the index held at 2026-11-15 20:50 EST (not 154: P7, P2/P6 and series runs had been
  sealed since 0362) were moved, each checked against the index's run.json hash first, into
  `F:/c5-64bit-work/ledger/superseded/d151/runs/<month>/<run_id>`, with the old index
  (`index-pre-d151.duckdb`), the old page (`page-pre-d151.html`) and `moved.json` (every run's from/to); a line in
  `superseded/README.txt`. Nothing deleted. Script: `research/08-performance/evidence/9071/move_aside.py`. A run
  folder being written (run.partial.json only) is never in the index and was not touched.
- **The record.** An optional top-level key `acceptance` of `c5-run/1` (`record.acceptance_record`,
  `acceptance_faults`; CONTRACT 3 and 12): `{label, client, map, turn, client_build, turns_played}`, label =
  "<client> <map> turn <N>" = `identity.label`; a run with it must name its save, start at that turn, and list the
  client's binaries (`exe`, `dll`; the patch also `proxy`, `server`). Records without the key stay valid.
- **The ingest filter.** `ledger.py ingest` ingests only runs with the key (`SKIP ... not an acceptance run (D151)`);
  `--any-run` restores the old behaviour for a history or scratch index (and the mechanics tests). Every other tool
  still writes its run folder (into the ledger root or its ticket's folder); it is simply not in the index.
- **The registry** (7 entries, under 7000's review): `exe.private_slope@2`, `exe.largest_free_slope@2`,
  `server.private_slope@2` with their 95 % half-widths (`@2`, `server.private_slope_ci95@1`): least squares over the
  acceptance run's per-turn rows (its own method, never compared with `@1`); `turn.autoplay_wall_s@1` (timing: the wall
  time between two turns seen by the Lua channel, polled every 5 s).
- **The driver** `perf/profilekit/acceptance.py run INSTANCE --save ... --turn N --client ... --map ... --config ...
  --client-build ... --out DIR` (then `record DIR`, which `run` calls): copies the save into the instance by hash, loads
  it through the harness front end with the mods, checks the loaded turn, arms `Game.SetAIAutoPlay(10, 0)`, lifts the
  frame cap, samples the EXE (`tools/harness/memory.py`) and its child `server.exe` at every new turn
  (`tools/stressmap/calibrate.py`'s loop and helpers), safe stop, front end restored, save copy removed by hash, and
  seals the acceptance run (grade C on the workstation, `not_for_timing` set by the library).
- **A box ends the turn loop at once (ticket 0524, D155; 2026-11-17)** `[C, tests
  perf/profilekit/tests/test_acceptance_box.py]`. Found in one-off 9074 on 2026-11-16 `[L]`: the VP crash handler's
  fatal box "Game Crash" opened at 21:27:29 during turn 1100 (stock EXE, 43-civ test DLL, c5h-i); the dialog watcher
  marked it at once, but the turn loop checked only `pid_alive` and the Lua channel (which stops answering while the
  box is open, with the EXE alive), so `run` went on polling until the owner closed the box by hand (21:38, code 6) and
  would otherwise have waited the whole `--turn-timeout` (1,500 s, code 5) with the client on the box. The load wait
  (`wait_game.py`, rc 8) never had this gap. Now each pass, before the Lua call, `open_box` reads (1) the watcher's
  marker of this launch (`dialogwatch.marker_dialog`, any box the watcher holds), then (2) `crash.dialog_state(pid)`
  (the EXE and its children; about 25 ms a read, run-and-drive-the-game.md section 17): a fatal box by the watcher's
  table (`dialogwatch.classify`) ends the run at once; any other box without a marker only after 60 s
  (`BOX_GRACE_S`: the watcher may still press OK on an assert under `record-and-continue`, its 15 s poll plus 10 s
  settle). The run ends with **exit 8** (as `wait_game.py` and every harness wait), nothing pressed, no further Lua
  call or harness command, no ledger run sealed; the record's `dialog` holds source (watcher or live), kind, title,
  text head (400 characters), the box's `Location ...:` lines (the crash handler's box carries "Location (in file)"
  and "Location (live memory)"), buttons, signature, `pressed: "nothing"`, and `end_reason` names the box and its
  Location. The owner then closes it (`cli.py dismiss --button OK --fatal`, `stop --after-fatal`, COMMON 29); `record`
  on the folder afterwards seals a fatal-box run as a crash with `crash_left: "dialog"` and the box's signature in
  `loud_lines`. The tests drive `run` against a fake game whose box opens after autoplay is armed: without the change
  4 of the 5 fail with code 5 "no new turn for 1500 s after turn 1102"; the fifth (an assert gone after 25 s does not
  end the run) passes both ways. No live game run was made for this change.
- **The page** (`perf/ledger/page/build.py`, default; `acceptance.py`, `acceptance.html`, `acceptance.js`): one card
  per save, its runs side by side (stock first): growth per turn with the 95 % interval as the headline, first and last
  value of each memory series, outcome ("CTD at turn x" for a crash), turn time marked "indication, not for timing",
  client build, binary hashes, pins, run_id; per-turn curves. Nothing is averaged over runs. `build.py --explorer` keeps
  4205's explorer for the superseded index (its scorecard moved to `perf/ledger/milestones/explorer/`).
- **Frozen comparisons**: `perf/ledger/page/acceptance.py freeze|verify|show` (*path added 2026-11-21, one-off 9080:
  not the driver `perf/profilekit/acceptance.py`, which has `run` and `record`*), schema `c5-acceptance/1`, under `perf/ledger/milestones/`.
- **One picture per turn** (the owner through the MC, 2026-11-15 22:20, D151 addendum): `acceptance.py run` takes, at the
  loaded turn and at each turn played, an acceptance picture as the player sees it (D112): the EXE's popups closed
  through the game's own popup queue (`tools/scenario/lua/popups.lua`, D114), the camera put on one plot with
  `UI.LookAt` (default: the largest city at the loaded turn, so both arms of one save show the same place; Large
  Communitu t200: plot 60,8, population 21), the harness's PrintWindow capture of the cloaked window (no focus),
  registered in Visual Checks. PNG and a 640 px JPEG stay on F: beside the run; the record's `acceptance.shots` holds
  paths, SHA-256 and the Visual Checks id, or `missing` with the reason. The page shows each thumbnail per turn
  beside that turn's memory; a click opens the full-resolution picture in the tracker's picture viewer (below).
  *(Corrected 2026-11-16, one-off 9072. Replaced: "The page inlines each thumbnail after a hash check (the tracker
  serves the page as one file with `img-src data:`)".)*
- **How the pictures are served** (one-off 9072, 2026-11-16) `[C]` code, `[L]` the built-in browser against a branch
  instance of the tracker server on the real index (4 runs):
  - The fault the owner reported: 9071's page inlined each 640 px thumbnail as a `data:image/jpeg;base64,...` URL and made
    the picture a link to that same data URL with `target=_blank`. A click is a page-initiated navigation to a `data:`
    URL, which Chromium blocks for the top frame (the owner [reworded for privacy] saw an odd base64 path); "open link in new tab" showed the
    thumbnail, the only picture the page had. The full PNG was never reachable from the page.
  - Now nothing is inlined. `build.py` checks the thumbnail **and** the full PNG against their SHA-256 at build (a
    changed or gone one shows the turn as missing), links them by content-hash name (`/ledger/thumbs/<sha256>.jpg`,
    `/ledger/images/<sha256>.png`) and writes `shots.json` beside the page (`c5-acceptance-shots/1`: hash -> path, for
    exactly the pictures the page links; written before index.html). The pictures stay where the run wrote them on F:.
  - The tracker server serves those names the way it serves Visual Checks pictures (`tracker/server/ledgerpics.py`
    beside `visual.py`): read-only, only names of the two forms, only hashes listed in `shots.json` (re-read when it
    changes), and only while the file's SHA-256 is still its name; `Cache-Control: private, max-age=604800,
    immutable`. `/ledger/`'s policy gained `img-src 'self'`; script stays inline only and `connect-src 'none'`.
  - The viewer is the Visual Checks one, moved into `tracker/viewer.js` (`PicViewer`): the tracker page loads it with a
    script tag, the ledger build inlines the same file. A plain click opens it over the page (the PNG fitted to the
    window, a "full size" link to the PNG in a new tab, Close / Esc / a click on the picture); ctrl-, shift- or
    middle-click and "open link in new tab" open the full PNG directly. "Open image in new tab" on a thumbnail gives
    the thumbnail (the link, not the image, is the full picture).
  - Measured 2026-11-16 on the real index (4 runs, 41 pictures, 3 turns missing as recorded): the page is 4.68 MB
    (4,905,358 B; was 8.6 MB with the thumbnails inlined), all 41 thumbnails load (640 px), a click opens the viewer
    in place with the 2554x1411 PNG, and the "full size" URL opens the PNG at 2554x1411 in a tab `[L]`.
  - A page built with this code needs a tracker server with the 9072 routes: the server on main before the merge
    shows such a page without pictures. Rebuild F:'s page only after the merge and a server restart. **The picture of the loaded turn shows grey ground on both arms** [L, 22:31]: the terrain is not drawn yet
  right after the load; from the first turn played on, it is (both arms alike, so it is not the patch).

### 8.2 Making the saves headless: what it takes `[L, 2026-11-15]`

- **The server's own database does not fit a client.** The first Large Communitu game (server `run_server.py` with the
  descriptor's test database, `%USERPROFILE%/Documents/civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db`,
  46 civilisations) drew `CIVILIZATION_HININ_MUSASHI`, which VP 5.4.8 in mods form (the instances, the players) does not
  have (45 civilisations). The stock client loading that save stopped in the DLL's fatal assert `eLeaderHeadNum > -1`
  (`CvGlobals.cpp` line 3876) during the load (c5h-d, 20:51). **Fix: give the server the client's own database**
  (`run_server.py --db <instance>/cache/Civ5DebugDatabase.db`, copied after the client has activated the same mods):
  the game is then drawn from what the client has, and the save loads.
  **The assert comes from the save, not the build** [L, 2026-11-15]: the same stock build (`i0ship-26234371-ia`,
  `3C2F47DA...`) loads `9071-LC21B-T200` (made on the client's database) and plays 10 turns; only the save made on the
  descriptor database fails. The MC also dumped the box before the dismiss
  (`F:/c5-64bit-work/9071/dumps-crashbox/c5h-d-x86-24444-20261115-222020.dmp`).
  **Who can hit the assert** [C/L for the parts, I for the conclusion, 2026-11-15]: only saves written by a server that
  ran on a database other than the loading client's, which is our headless test path (`run_server.py` with its
  default descriptor database). The shipped build's server never picks its own database: it copies the engine's
  (the client's) database through the SetGameDatabase hook into `<run>/db/Civ5DebugDatabase.<n>.db`
  (`research/03-dll-engine-boundary/proxy-design.md`, "Cause", 2026-11-13; `server/src/remote/srv_remote_main.cpp`),
  so a player's game holds only civilisations the player's own database has. A player could meet the same assert only
  the way the 32-bit game can, by loading a save whose civilisations the loading game's database lacks (a save made
  under a different mod set; where the descriptor database's 46th civilisation comes from was not traced); that is
  stock behaviour, not the patch's. c5h-d's dialog was
  dismissed under the MC's D60 override at 22:24 (the EXE then exited through the assert; evidence
  `F:/c5-64bit-work/9071/c5h-d/evidence/dialog_dismiss.jsonl`).
- **A map pack's setup is the client's.** The Hormigas pack places every civilisation and city state from its own
  tables through its overridden `MapGenerator.lua` during a load driven by its front end, and its SQL renumbers the city
  states; a headless spec cannot do that. So the 43-player game is **set up in the client and played headless**: the
  client (c5h-g, 43 thin client, `calibrate.py --pack`) wrote the turn-0 save `9071-EARTH43-THIN-T000` (sha256
  a1df12a9...), and the x64 server with the release-43 layout continued it (`run_server.py --load ... --db <c5h-g's
  cache database, with the pack active> --turns 400`). The owner's intent (20:56): both saves headless, only the acceptance
  runs in the client.
- **Headless is much faster than the client's autoplay**: Large Communitu 0-200 with 21 majors in about 6 min
  (0.2 s per turn early, 3-4 s at turns 180-200); the 43-player game in the client took 15-28 s per turn over turns
  1-31, headless 2 s per turn to turn 50 and 9 s at turn 130, 25 s at turn 233, 36-53 s at turns 290-331 (workstation, other teams' load; indications only, D106).

### 8.3 The first acceptance set `[L, 2026-11-15, workstation, not for timing]`

Saves: `F:/c5-64bit-work/9071/saves/` (`9071-LC21B-T200.Civ5Save`, sha256 `e3da1d17...`: Large Communitu, Large, 21
majors + the observer's slot, VP 5.4.8 with EUI, headless on a stock client's database). Clients: stock 32-bit = the
stock EXE with the pinned VP 5.4.8 DLL `i0ship-26234371-ia` (`3C2F47DA...`); increment 1 RC = the staged
`VP64-first-playable-VP5.4.8-x64-04b1211b-pfdee656` installed as INSTALL-FIRST-PLAYABLE.md says (proxy `75C40CEC...`,
x64 DLL `29F58E17...`, server `5B254430...`, tag DLL `262aee88...` as the fallback; `pack=1` boot ok, no LOUD line).
Both arms of a pair ran at the same time on F: instances (c5h-i / c5h-f, then c5h-d / c5h-f). Growth per turn: least
squares over turns 201-210, with the 95 % interval.

| Large Communitu turn 200 | EXE private MB/turn | EXE largest free MB/turn | server.exe private MB/turn | EXE private t200 -> t210 MB | largest free t200 -> t210 MB |
|---|---|---|---|---|---|
| Stock 32-bit, pair 2 (22:28, pictures; `20261116T032840Z-ws-9963fd`) | +1.67 ± 3.12 | -8.01 ± 3.27 | - | 2054 -> 2175 | 1148 -> 1022 |
| Increment 1 64-bit, pair 2 (22:28, pictures; `20261116T032841Z-ws-cdfc8b`) | +0.08 ± 2.71 | -3.90 ± 2.46 | +1.05 ± 0.27 | 1906 -> 1984 | 1481 -> 1333 |
| Stock 32-bit, pair 1 (21:02, no pictures; superseded) | -1.88 ± 2.29 | -3.83 ± 1.23 | - | 2096 -> 2137 | 1132 -> 1057 |
| Increment 1 64-bit, pair 1 (21:02, no pictures; superseded) | -1.39 ± 3.09 | -1.53 ± 1.29 | +0.56 ± 0.92 | 1961 -> 1992 | 1316 -> 1285 |

- **Ten turns do not resolve a growth of about 1 MB/turn in the EXE's private bytes**: the intervals are ±2-3 MB/turn,
  and the same arm from the same save moved from -1.9 to +1.7 between two plays (AI decisions differ from play to play
  after a load). The EXE's largest free block shrinks in every run; the RC's starts 184-333 MB larger (1481/1316 MB
  against 1148/1132 MB at turn 200), the game state being in the server [I]. server.exe's private bytes grow
  +0.6 and +1.0 MB/turn. A headline that must resolve 1 MB/turn needs more turns or repeated plays per arm [I].
- Turn times (indications): stock 22-36 s, RC 22-39 s per turn, both arms sharing the machine with each other and with
  other teams' games.
- Pair 1's records are in `F:/c5-64bit-work/ledger/superseded/9071-no-pictures/` (the addendum wants a picture per
  turn); pair 2 is in the index and on the page, with 11 pictures per arm, none missing.

**Earth TSL turn 400** (save `9071-EARTH43-T400.Civ5Save`, sha256 `83a146c6...`: the Hormigas Enhanced Map Pack's
PlayTheWorldExtended Earth, Huge, 43 majors set up (42 + the observer's slot; 36 alive at turn 400), city states
max, VP 5.4.8 with EUI and the pack; set up in the client at turn 0, played headless to turn 400 by the release-43
server in 3 h 06 min, 20:54-00:00, on c5h-g's game database). Clients: stock 32-bit 43-civ = the stock EXE with the 43
build of the pinned DLL `i0ship-26234371-m43-ia` (`204FBFCC...`, c5h-i); increment 1 RC 43-civ = the release-43 layout
`x64-04b1211b-m43-p9aa30f1` installed with `provision.py set-proxy` (proxy `D6A43056...`, x64 DLL `CF981126...`, server
`D529D5D0...`, the 32-bit 43 DLL as the fallback; c5h-g; no LOUD line). Both played 00:02-00:50 at the same time; the
load took 309 s (stock) and 337 s (RC).

| Earth TSL turn 400 | EXE private MB/turn | EXE largest free MB/turn | server.exe private MB/turn | EXE private t400 -> t410 MB | largest free t400 -> t410 MB | free address space t400 -> t410 MB |
|---|---|---|---|---|---|---|
| Stock 32-bit 43-civ (`20261116T050244Z-ws-8362d3`) | **+5.15 ± 2.02** | -5.15 ± 5.25 | - | 2614 -> 2783 | 607 -> 404 | 663 -> 452 |
| Increment 1 64-bit 43-civ (`20261116T050244Z-ws-a28a72`) | +0.23 ± 2.95 | 0.00 ± 0.00 | **+4.58 ± 2.02** | 2294 -> 2395 | 1012 -> 868 | 1072 -> 944 |

- **Stock grows by about 5 MB/turn in the EXE at turn 400 with 43 players, and the growth is resolved** (the interval
  excludes 0). Its address space has 452 MB free and a largest free block of 404 MB at turn 410; at 5-20 MB per turn
  that is some tens of turns, not hundreds, before an allocation fails [I: a straight extrapolation; player-limits.md
  8.4.8 forecasts stock's death at about turn 460 or earlier].
- **The RC moves the growth out of the EXE**: its EXE stays flat (+0.2 ± 2.9) and its largest free block stays at
  868 MB after the first turn played; server.exe (64-bit, no ceiling) grows +4.6 ± 2.0 MB/turn, the same order as
  stock's EXE growth: the game state's growth now lives in the server [I].
- Both arms drop most in the first turn after the load (stock 607 -> 468, RC 1012 -> 868 MB largest free block):
  the fit starts at turn 401, so this step is not in the headline.
- Turn times (indications, both arms on the workstation at once): stock 178-344 s, RC 147-318 s per turn.
- **Pictures**: 10 of 11 for stock (turn 400 and turn 410 missing), 10 of 11 for the RC (turn 400 missing). Missing
  turns: the popup-closing chunk got no answer within 15 s (the 43-player game's interface state does not answer
  while an AI turn runs; turn 400's chunk ran right after autoplay was armed). Fixed in the driver afterwards: the
  loaded turn's picture is taken before autoplay starts, and the popup and camera chunks are retried (3 x 30 s).
  The camera sat on the same plot (6,47, the largest city, population 55) on both arms; the stock view is shifted a
  little against the RC's (the 2 s settle after `UI.LookAt` may not cover the camera's move) [L, pictures turn 401].
- Frozen: `perf/ledger/milestones/first-acceptance-inc1.json` (both saves, four runs; `acceptance.py verify` matches
  the index); page: the tracker's `/ledger/` (`F:/c5-64bit-work/ledger/page/index.html`, 8.6 MB with 41 pictures
  inlined; since one-off 9072 the pictures are linked, not inlined, see "How the pictures are served").

### 8.4 The worst-case stress map replaces Earth TSL (D157, D160, D161; one-off 9073) `[L, 2026-11-16, workstation, not for timing]`

Written 2026-11-16 by one-off 9073. The owner (D157, 02:20 EST): replace Earth TSL with the worst case one can think of on
Tectonic or Continental Drift (maximum civs and city states, Marathon, maximum land and size) and get stock crashing
between turns 200 and 300. **The turn window is corrected by D161 (03:53): a stock crash at any turn up to 400
qualifies.** D160 (03:47): Standard speed + Deity replaces Marathon if it crashes stock. Marathon's game goes on to
about turn 2000 in one-off 9074.

**The map.** `tools/stressmap/tectonic_stress.py` writes a copy of VP 5.4.8's `Tectonic v6 - VI.lua` (sha256
`346e9bf8...`) with the size multiplier forced (the copy stays on F:, never committed, D83). Size is capped by the
EXE, not by the DLL or the server: the stock EXE loads about 20,000 plots and overflows its stack while loading
21,879 (research/02-exe-internals/allocations-and-memory-map.md 6.1). The largest map in reach is Tectonic's own
Gigantic, factor 1.20 = **180x112 = 20,160 plots** (`Tectonic Stress 1.20.lua`, sha256 `34504c18...`). Options
(`Map.GetCustomOption` index = value): temperature 2=2, rainfall 3=3, sea level 4=1 (Shallow), resources 5=3
(Abundant), islands 6=6 (Abundant), plate motion 7=2, cohesion 8=3, land 9=5 (More), plates 10=3, ratio 11=3, size
12=9 (120 %), ocean rift 13=1 (Tight), polar ocean 14=1 (Tight), coastal start 15=2, start placement 16=3 (Start
Anywhere; with 42 majors it failed in VP's start placement on 1.35 and 1.50 with seed 9071, `ipairs` nil in
`FindStartWithoutRegardToAreaID`, and passed on 1.20). Players: the 43-civ build, 42 majors + the observer's slot,
20 city states (the build's maximum), Ancient era, max turns 3000; victories: the spec asked for none (`victories: []`, server.cfg `victories=` empty), but the loaded T200 save reports all five enabled (time, space, domination, cultural, diplomatic; the driver's facts at load, 2026-11-16) [L], so an empty list does not switch them off (not traced; no victory had happened by turn 272), seeds map 9071 /
sync 9072. Spec: `research/08-performance/evidence/9073/spec-std-deity.json`.

**Making the game.** Turn 0 and every 25 turns headless by the release-43 server (`x64-04b1211b-m43-p9aa30f1`) on the
stock client's game database (`F:/c5-64bit-work/9071/lc-db/Civ5DebugDatabase.db`, sha256 `aa4484b3...`, 43 playable
civilisations; 8.2), `evidence/9073/segments.sh` (the server writes a save only at its end, so the game is played in
segments). Saves `F:/c5-64bit-work/9071/stress/saves/TSstd-deity-TNNN.Civ5Save`: T000 `6f04c282...`, T150 `08fd75e2...`,
T175 `3551e8fb...`, **T200 `225b2e5020343ed846a1519bdeb8e7b3e023104bdbbe0e6bf79cf8a1c12480df`**, T225 `84d811d1...`,
T250 `d1ed0634...`. Server time per turn (workstation, shared; indications): 0.2-10 s to turn 100, 4-11 min per
turn at turns 260-272. The segment to 275 hit run_server's 2 h timeout at turn 272 and was not repeated.

**Marathon cannot meet the target on the largest loadable map** `[L]`: stock loaded the Marathon game's turn-250 save
and played 250 -> 260 clean with about 600 MB free and a 466 MB largest free block. At turn 300 the game had 177 cities,
population 1,137, 870 units and about 16 techs.

**Standard + Deity crashes stock** `[L]` (stock = the stock EXE with the 43 build `i0ship-26234371-m43-ia`, `204FBFCC...`,
on c5h-h; `evidence/9073/probe.sh`, 25-turn chunks so that every turn is played):

| loaded save | turns played | largest free block at the load -> end | result |
|---|---|---|---|
| T100 | 100 -> 110 | 683 -> 252 MB | clean |
| T150 | 150 -> 175 | 477 -> 43 MB | clean |
| T175 | 175 -> 200 | 151 -> 46 MB | clean |
| T200 | 200 -> 225 | 494 -> 1.15 MB (36 MB t209, 13 MB t216, 2.3 MB t222) | **crash right after turn 225** (11:41:36) |

- The crash: 0xC0000005, write to 0xb421e000 at `CivilizationV_DX11.exe+0x001252c8` (file offset), the DLL's crash
  handler's "32-bit address space nearly exhausted", Sub2G largest free block 304 KB, free 23.7 MB. Crash files:
  `F:/c5-64bit-work/9071/stress/cal/stock-std-deity-t200/crash/`.
- **The crash depends on play time since the load, not on the turn alone**: a fresh load gives back several hundred MB
  of largest free block (T175's chunk ended at 46 MB; T200's load started at 494 MB), and about 25 turns of play at
  turns 175-225 use it up. A save a few turns before 225 would not crash stock within 10 turns [I].

**The acceptance row** (the MC's ruling, 2026-11-16 11:55): save T200, both arms from it at the same time for up to 30
turns or until stock crashes; the ledger keeps D151's 10-turn growth as the standard measure (comparable with the other
rows) plus the full run: stock's "CTD at turn x" with its largest-free-block series, the RC's 30 turns. Clients:
stock 32-bit 43-civ = stock EXE + `i0ship-26234371-m43-ia` (`204FBFCC...`, c5h-h); increment 1 RC 43-civ =
`x64-04b1211b-m43-p9aa30f1` by `provision.py set-proxy` (proxy `D6A43056...`, x64 DLL `CF981126...`, server
`D529D5D0...`, fallback `204FBFCC...`; c5h-g). Both instances were brought to VP 5.4.8's files by `provision.py
align548` and `verify` (0423) before the series (c5h-h: VPUI tips added, MinorCivSounds replaced, Squads copied;
c5h-g already aligned); the client-build names say so. Runner: `evidence/9073/accept.sh` (a watcher closes a fatal
crash box as the claim's owner, D155, so the driver sees the EXE gone).

**Stock failed at turn 202 (supporting evidence): probable address-space exhaustion, symptom DEVICE_REMOVED** `[L; cause I]`.
The first stock arm of the pair (c5h-h, from T200, 12:00) stopped at 12:21:31 on a fatal 'Render Error' box:
"FGXRenderer11::Present failed hr=DXGI_ERROR_DEVICE_REMOVED. Driver failure". It had played turn 201 and was in turn
202. The MC first ruled it a test-machine fault (12:25), then revised it (12:40): it is a stock failure, most likely out
of address space with DEVICE_REMOVED as its symptom [I]. The GPU load on the shared board (12.5 of 16 GB, 8 clients
running, per the MC) is a possible contributor. This run is supporting evidence; stock's recorded result is the 08:52
run's crash at turn 225 (the MC's final ruling, below; it replaced "the rerun's result is the recorded one", 12:40). Its driver was stopped by exact pid before the box was closed, so it has no ledger record.
Evidence (memory.csv, per-turn.csv, run.log, acceptance-run.json, the harness's crash folder
`20261116_122131_pid34680`): `F:/c5-64bit-work/9071/stress/accept/stock-render-error/`. Its two pictures (turns 200 and
201) were removed with the run folder by mistake; both are still registered in Visual Checks
(`vc-20261116-120552-workstation-5492` for turn 200).

| stock from T200 | EXE private MB | free address space MB | largest free block |
|---|---|---|---|
| 08:52 run, row at turn 201 | 2,905 | 287 | 44.8 MB |
| 08:52 run, row at turn 225, then crash (11:41:36) | 3,069 | 38 | 1.15 MB (the crash box: Sub2G largest 304 KB, total free 37.5 MB) |
| 12:00 run, row at turn 201 | 3,008 | 251 | 74.8 MB |
| 12:00 run, at the box in turn 202 (12:21) | 3,106 | 72 | 12.1 MB |
| 12:25 rerun, row at turn 201 | 2,988 | 281 | 71.3 MB |
| 12:25 rerun, at the box in turn 209 (13:34:07; flat at 31.9 MB since turn 207, lowest 18.5 MB at 13:18) | 3,172 | 168 | 31.9 MB |

The stock rerun (c5h-h, from T200, started 12:25:13, `evidence/9073` runner `accept-stock.sh` with a watcher for the
Render Error box) stopped the same way: the same DEVICE_REMOVED box in turn 209 at 13:34:07, closed as the claim's owner
(D155). Evidence and the harness's crash folder `20261116_133407_pid61224`:
`F:/c5-64bit-work/9071/stress/accept/stock-rerun-render-error/`. Unlike T202 it came with 168 MB free and a 31.9 MB
largest block, so address-space exhaustion is less clear for it; GPU load is the other candidate [I].

**Why the loaded turn's row read 494 MB largest free in one run and 220 MB in the other** `[L]`: what differed is when
that row was taken, not the game's state. Same instance (c5h-h), same build (`204FBFCC...` with the stock EXE
`04CFDC71...`), same mods (cp, vp, eui), same window (2554x1411 client area, the instance's own graphics settings),
same save and camera plot (107,31). The only file change between them was align548's VPUI tips and MinorCivSounds
(text and sound XML). The memory after the game came up follows the same curve in both: 08:57:34 (27 s after the game
came up) 526 MB, 12:05:48 (2 s after) 461 MB; 5 min after, 311 MB against 237 MB. The 08:52 run took its turn-200 row
38 s after the game came up, the 12:00 run 5.5 min after (the step between the picture and the row was slow: the Lua
channel under the shared load [I]). At turn 201 the two runs read 44.8 and 74.8 MB. The headline fit starts at turn
N+1, so the loaded turn's row is not in it. Other clients on the GPU differed (more at 12:00), which may explain some of
the gap in the minutes after the load [I].

**The MC's final ruling (2026-11-16 13:45, option b).** Stock's recorded result is the 08:52 run: out-of-memory crash at
turn 225, 25 turns after loading T200, with its full memory series; its first 10 turns are the D151 standard measure.
Both DEVICE_REMOVED runs (T202: 12.1 MB largest / 72 MB free; T209: 31.9 MB / 168 MB) are supporting evidence,
"probable address-space pressure, GPU load a possible contributor" [I]. No more stock tries. The 08:52 run predates
align548 on c5h-h (the only file change was the VPUI tips and MinorCivSounds XML, above).

**Stock's record from the probe run** `[L]`. The probe ran with `--no-ledger`, and its driver saw autoplay hand back at
turn 225 a moment before the crash box, so its own file says code 0. `evidence/9073/record_t225.py` copies the probe
folder (`F:/c5-64bit-work/9071/stress/cal/stock-std-deity-t200`) to `F:/c5-64bit-work/9071/stress/accept/stock-t225`
without the pictures (they stay where they were taken and the record names them by path and SHA-256). It keeps the
driver's file as `acceptance-run.probe.json` and changes four fields, listed in the record's `recorded_by`: the map name
(the row's name), the client build (9071's name for it), code 0 -> 6, and the end reason (the crash). Then it seals the
record with the driver's own `record` step: **`20261116T135156Z-ws-5b7da4`**, "Stock 32-bit 43-civ Tectonic Stress
180x112 Deity turn 200", outcome crash at turn 225 (last sample: committed 3,399 MB, free 38.2 MB, largest 1.15 MB),
26 of 26 pictures.

**The fit over turns N+1..N+10, also for longer runs** `[C, perf/profilekit/tests/test_acceptance_driver.py]`. The
driver used to fit the headline slopes over every turn played. The registry defines them over N+1..N+10 (D151's
standard measure), so a 30-turn run would not be comparable with the 10-turn rows. `perf/profilekit/acceptance.py`
now fits only turns N+1..N+10 (`FIT_TURNS = 10`). The later turns stay in the series and set the outcome. Test
`test_a_longer_run_fits_only_its_first_ten_turns`. The RC arm was started before the fix and sealed
`20261116T170029Z-ws-4039d7` with the all-turns fit. It was re-recorded from the same folder as
**`20261116T170029Z-ws-2810a0`**, and 4039d7 (never ingested) went to
`F:/c5-64bit-work/ledger/superseded/9073-rc-all-turns-fit/` with a README line. For the record, 4039d7's all-turns fit
was EXE private +2.97 ± 0.39 MB/turn and largest free -5.84 ± 1.16 MB/turn.

**The increment 1 RC arm** `[L]` (c5h-g, from T200, 12:00-14:56, 30 turns, 4-8 min per turn, not for timing):
completed, autoplay handed back at turn 230, no crash and no box. Its client is still the 32-bit EXE (the shell), so
its address space is still the 32-bit one, but the world renderer and the DLL live in the server.

| RC from T200 | EXE private MB | free MB | largest free block MB | server private MB |
|---|---|---|---|---|
| turn 200 (loaded) | 2,476 | 840 | 788 | 428 |
| turn 201 | 2,525 | 771 | 647 | 440 |
| turn 210 | 2,550 | 685 | 519 | 512 |
| turn 220 | 2,587 | 656 | 477 | 523 |
| turn 230 | 2,621 | 584 | 376 | 540 |

- 10-turn fit (record 2810a0): EXE private **+3.29 ± 1.95 MB/turn** (stock +8.75 ± 2.18), largest free -8.05 ± 7.18
  MB/turn (stock -0.49 ± 0.59: stock's largest block had already collapsed by turn 209), server private +7.0 ± 2.55
  MB/turn.
- Over the 30 turns the EXE's largest free block fell 788 -> 376 MB, about 9 MB per turn after turn 210 (518.69 MB flat
  at turns 209-217, then steps down to 477, 408 and 376). Stock had 1.15 MB left at turn 225 under the same load; the RC
  at turn 230 still has 376 MB in one block and 5 free blocks of 16 MB or more.
- The turn-230 picture is missing: PrintWindow on the game window did not return within 10 s after the hand-back (the
  window's thread was busy; no box, no crash files). The record names it with the reason; 30 of 31 pictures are present.
  This is why the page counts 78 pictures for 79 turns.

**Earth TSL leaves the acceptance set** `[L]`. `evidence/9073/supersede_earth_tsl.py` (checks every run's run.json
against the index's hash first) moved 20261116T050244Z-ws-8362d3 (stock) and 20261116T050244Z-ws-a28a72 (RC), the
Earth TSL turn 400 pair, and the index that held them to
`F:/c5-64bit-work/ledger/superseded/d157-earth-tsl/` (moved.json, index-before.duckdb), with a README line. Their
numbers stay in 8.3. Then `ledger.py ingest` (4 runs: the Large Communitu t200 pair with pictures and the stress pair)
and the page was rebuilt: 2 saves, 4 acceptance runs, 2 frozen comparisons.

**Milestone `perf/ledger/milestones/acceptance-inc1-d157.json`** (frozen 2026-11-16 by impl-9073): Large Communitu
t200 (`20261116T032840Z-ws-9963fd`, `20261116T032841Z-ws-cdfc8b`) and the stress row (`5b7da4`, `2810a0`); `verify`
matches the index. `first-acceptance-inc1.json` stays as frozen history. Against today's index its verify reports
its two Earth TSL runs as "not in the index". Against `superseded/d157-earth-tsl/index-before.duckdb` it matches.

### 8.5 The increment 2 release candidate i2-rc1 (ticket 0559, D189/D154, 2026-11-18) `[L, workstation, not for timing]`

Written 2026-11-18 by impl-0559 (account B). The owner (D189, 2026-11-18): increment 2 ships as i2-rc1 as taken by ticket
0509 (no new candidate; the farm-field crash on huge loads is a known, accepted risk). D154: the owner sees the ledger's
acceptance runs before they approve a release.

**Scope (the MC, #5868, 2026-11-18).** The Large Communitu turn 200 pair only. **Stress row not applicable to i2-rc1:
the release has no 43-civ variant (0509 item 4); inc1's stress row stays the latest** (`acceptance-inc1-d157.json`).
A 43-civ i2 arm would test a build nobody gets (the installer is std only, and no 43 build of pin batch 3 exists), and a
stock-only rerun on batch 2 adds nothing to inc1's stock row.

**Instance and mods.** One place was free (c5h-i, F:), so the two arms ran one after the other on it (inc1 ran each
pair at the same time on two instances). c5h-i had been left with `(4a) Squads for VP` enabled in its mods database by
an earlier ticket. It was unticked before any ledger run by one menu-only launch whose patched front end enables exactly
cp, vp and eui and disables every other enabled mod through `Modding.DisableMod` (the Mods menu's own call; Lua.log
"disabled 3645dbca...@1", 15:20-15:25). Its enabled set, read from a read-only copy of
`cache/Civ5ModsDatabase.db`, then equalled the untouched pool instances c5h-b and c5h-c (`(1) Community Patch`
d1b6328c@151, `(2) Vox Populi` 8411a7a8@17, `(3a) VP - EUI Compatibility Files` 24923240@1) `[L]`; lists in
`perf/evidence/0559/modsdb-*.json`, `modsdb-compare.txt`. `provision.py align548` and `verify` passed first.

**Clients.**
- Stock 32-bit: the stock EXE with the 32-bit pin of i2-rc1, `i0ship-39f17f75-ia` (pin batch 3, `7DE39F71...`), put in
  with `provision.py set-dll`.
- Increment 2 64-bit: i2-rc1 as a player gets it. VP's own DLL in place first (the tag's `262AEE88...`, the player's
  state), then the installer `VP64-Patch-i2-rc1-for-VP5.4.8.exe` (SHA-256 `6ed1dad6...cf51b`) run silently into c5h-i
  (`run_installer.py`, test mode so that only c5h-i's processes count as a running game); compositor and ServerRenderer as
  installed; pack manifest `3fa8269f...`. Removed with the same installer afterwards and the instance verified.

Save `F:/c5-64bit-work/9071/saves/9071-LC21B-T200.Civ5Save` (sha256 `e3da1d17...`, the same as 8.3), camera plot 60,8
(8.3's), `perf/profilekit/acceptance.py run` (10 turns of AI autoplay from turn 200, one acceptance picture per turn).
Scripts and logs: `C:/c5-64bit-work/0559/` (not committed); run folders `F:/c5-64bit-work/0559/runs/{stock,i2}-lc200`.

**The install** `[L, 16:01-16:03]`: installer exit 0, "install i2-rc1 (std, i2, mods form)", VP's DLL found in the
release manifest. The My Games tree gained exactly the payload (proxy `8D809D01...` in place of VP's DLL, VP's DLL kept
as `CvGameCore_Expansion2_x86.dll`, `shell_composite.dll` `8053F8C7...` with its `.ini` `283641b3...` and `.sha256`,
`VP64\` with the x64 DLL `87B07172...`, `server.exe` `54E873A2...`, `manifest.json` `3fa8269f...`, `purity.tsv`,
the helper, the notices, `vpproxy.ini` `mode=on pack=1`), the install record and two backups; `config.ini` was saved and
changed by the helper's `on`, and `VP64Options.ini` plus two side files were added (`perf/evidence/0559/
i2-tree-before-vs-installed.txt`, 27 rows). The game's proxy log: pack ok, `[Renderer] ServerRenderer = 1: composite
... shell_composite.dll`, `render=1 ... render_handover=ring`, server attached (1,056.7 ms), `kRt_Boot ok`, composite
loaded, 0 FALLBACK and 0 LOUD lines. **Removal** (16:14-16:16): exit 0, the tree **identical by SHA-256 outside the run
outputs** (Logs, cache, saves; `i2-tree-before-vs-removed.txt`); then `set-dll --original` (`3C2F47DA...`, the DLL
c5h-i had at the start), `provision.py verify` ok, the enabled mods still cp, vp, eui.

| Large Communitu turn 200 | EXE private MB/turn | EXE largest free MB/turn | server.exe private MB/turn | EXE private t200 -> t210 MB | largest free t200 -> t210 MB | free address space t200 -> t210 MB |
|---|---|---|---|---|---|---|
| Stock 32-bit, `i0ship-39f17f75-ia` (`20261118T202724Z-ws-675c0b`, 15:27-15:52) | +4.36 ± 3.27 | -7.33 ± 4.47 | - | 2079 -> 2159 | 1154 -> 1047 | 1203 -> 1105 |
| **Increment 2 64-bit, i2-rc1 installed** (`20261118T210350Z-ws-0d1c39`, 16:03-16:15) | **+2.30 ± 1.41** | **-5.28 ± 3.73** | **+1.04 ± 0.44** | **1606 -> 1695** | **1692 -> 1598** | 1736 -> 1649 |
| inc1, for comparison (8.3, pair 2): stock 32-bit `i0ship-26234371-ia` | +1.67 ± 3.12 | -8.01 ± 3.27 | - | 2054 -> 2175 | 1148 -> 1022 | - |
| inc1, for comparison (8.3, pair 2): increment 1 64-bit RC | +0.08 ± 2.71 | -3.90 ± 2.46 | +1.05 ± 0.27 | 1906 -> 1984 | 1481 -> 1333 | - |

- **i2-rc1's EXE starts about 300 MB smaller than inc1's and about 470 MB smaller than stock's** (private 1606 MB at
  turn 200, against 1906 and 2079), and its largest free block is about 210 MB larger than inc1's and 540 MB larger than
  stock's (1692 against 1481 and 1154 MB) `[L]`. The world renderer now draws in the server (the ServerRenderer path),
  so the EXE no longer holds the terrain's tile caches [I: from the design, not measured by owner here].
- **The growth per turn is not resolved by ten turns**, as in 8.3: the intervals of the EXE's slopes overlap between all
  four arms (stock +4.4 ± 3.3 here, +1.7 ± 3.1 in 8.3 on the same save), and plays from the same save differ. i2-rc1's
  EXE private slope (+2.3 ± 1.4) excludes 0; inc1's RC did not (+0.1 ± 2.7). server.exe grows +1.0 ± 0.4 MB/turn, as
  inc1's (+1.05 ± 0.27) `[L]`. A claim of a difference in growth between i2-rc1 and inc1 needs more turns or repeated
  plays per arm [I].
- Times (indications, D106; both arms on the workstation at different times beside other teams' games): load 735 s
  stock, 370 s i2-rc1; turns 30-114 s stock, 16.5-29.5 s i2-rc1. Not for timing.
- **Pictures**: i2-rc1 11 of 11. Stock 9 of 11: turns 202 and 204 were played while the driver was still taking the
  previous turn's picture (turn 201's row at 15:43:41, its picture registered at 15:44:59; turn 203 likewise), so the
  driver saw 201 -> 203 and 203 -> 205; the record names them "not reached", a wording that does not fit this case. On
  the stock arm the AI turn runs in the EXE, and the popup and camera chunks of the Lua channel wait for it (8.3, Earth
  TSL) [I]; on i2-rc1 the AI runs in the server and every picture was taken within the turn. The fit uses the 9 rows
  there are (turns 201-210 less 202 and 204). Visual Checks: 20 entries for issue 0559, none undescribed.
- **No box, no crash** on either arm (the dialog watcher's marker empty after each run; no `exe-crash.txt` in the i2
  run folder).
- **The records' `build.pins` name pin batch 3, what both arms ran** (i0 `39f17f754` `i0-ship-batch3`, x64 `4f4daac70`
  `x64-batch3`) `[C, L]`: corrected 2026-11-18 17:25-17:29 EST by ticket 0564 (section 8.6), which took the pins from
  each arm and checked them against its dll; run.json `a6257efa...` -> `c268f534...` (stock) and `20ea3e84...` ->
  `626edda8...` (i2-rc1), the old records in `F:/c5-64bit-work/ledger/superseded/0564/`. (Replaces, 0559's text: "the
  records' `build.pins` name pin batch 4, not what ran": `build_facts` had read the repository's lock files at record
  time, batch 4 since 0517.) For inc1 (8.3) the field was right: 0564's audit attests all four inc1 records
  (`perf/evidence/0564/audit.txt`).

Ledger `[L, 16:16-16:20]`: `ledger.py validate` ok for both, `ingest` 2 ingested (second ingest 0), index backed up
first (`F:/c5-64bit-work/0559/index-before-0559.duckdb`); **milestone `perf/ledger/milestones/acceptance-i2-rc1.json`**
(1 group, the two runs; the note carries the scope ruling and the pins remark), `acceptance.py verify` matches the index
for it and for `acceptance-inc1-d157.json`; the page rebuilt: `F:/c5-64bit-work/ledger/page/index.html` (4,956,732 B,
2 saves, 6 acceptance runs, 3 frozen comparisons, 98 pictures in `shots.json`), served by the tracker at `/ledger/`.
(Since 0564, 2026-11-18 17:25-17:29: the two records, the index, the milestone (pins, run.json hashes, note) and the page
corrected to batch 3; section 8.6.)

### 8.6 The arm's pins (ticket 0564, 2026-11-18) `[C, tests; L on the real store]`

Written 2026-11-18 by impl-0564 (account B). Found by 0559 (8.5): `record.build_facts` read `build.pins` from the
repository's `deps/dll.lock` and `deps/i0.lock` at record time, so a record made after a re-pin named the new batch
beside the old batch's binaries.

**The rule** (`perf/ledger/record.py` `arm_pins`, tests `perf/ledger/tests/test_arm_pins.py` and
`perf/profilekit/tests/test_acceptance_driver.py`, offline, with a throw-away repository `tests/pinrepo.py` that copies
the real history's shape). The pins are tied to the run's `dll` binary by its full SHA-256, from one of two sources;
anything else is refused with what disagrees, never guessed:
- **Lock files at a revision** (`--pins REV`, a commit of this repository; default the working tree): the dll's SHA-256
  must be in a `release`, `instance`, `release-43` or `instance-43` entry of `deps/dll.lock` or `deps/i0.lock`, and
  **that entry must name the lock's own pinned commit** (its first 8 hex digits). The second condition is needed
  `[C, git history]`: the batch-4 re-pin `cf416d9c1` (0517, 2026-11-17 21:37) changed the `commit` lines while the
  `release`/`instance` entries still held batch 3's builds until `93b5bfcfc` (04:21 the next day), and the batch-3 pin
  `cd3b335e8` likewise kept batch 2's hashes until `396b78303`. A hash-only rule would have tied batch 3's DLL
  `7DE39F71` to batch 4 at `cf416d9c1`. The `-bN` entries of older batches never attest. A refusal names the lock
  revisions that do attest the hash (`revisions_attesting`), as a hint for `--pins`.
- **A release manifest** (`--release-manifest`, the pack's `VP64/manifest.json`; `acceptance.py run` takes the
  instance's one beside the x64 DLL by default for the patch and copies it into the run folder as
  `release-manifest.json`, which `record` checks by SHA-256 and adds to the record): `x64_dll.sha256` must equal the
  dll's and `files["server.exe"]` the server's; `built.fork_commit` is the x64 pin, and the pins are the lock files at
  the revisions whose `deps/dll.lock` pins that commit (they must agree; `--pins` names one and is checked against it).
  This is the only source for a release take: i2-rc1's x64 DLL `87B07172` was built at tools `7bf0d579` and is in no
  lock file (the batch-3 lock names the `f8e9f424b` take's `2D737BD5`).

`build.pins_source` (optional, validated) says which: `{kind: locks | release-manifest, rev (full commit, null = the
working tree), attested: {role: where}, manifest: {name, sha256_16, fork_commit}}`. `acceptance.py run` checks the pins
before the launch (refused with exit 1 before any game starts; with `--no-ledger` the refusal is only noted
as `pins_refused`), and `record` checks them again (exit 1, nothing written). Other callers of `build_facts` (the
profile kit, the turn study, testbed jobs) are unchanged: without `pins` the lock files at record time, as before.
`perf/ledger/CONTRACT.md`'s `build.pins` row still says "read from deps/*.lock at the run" (not in 0564's file list).

**The correction on the real store** `[L, 2026-11-18 17:25-17:29 EST]` (`perf/evidence/0564/correct_pins.py`, outputs
beside it): `runs` with `675c0b=pins:396b78303` (the stock DLL `7DE39F71` attested by `i0.lock instance` there) and
`0d1c39=manifest:F:/c5-64bit-work/0509/installer/payload/std/VP64/manifest.json` (`3fa8269f...`, the pack i2-rc1's
installer laid down; x64 DLL and server.exe attested, fork commit `4f4daac70`, revision `396b78303`): each old run.json
copied to `F:/c5-64bit-work/ledger/superseded/0564/<run_id>/` by SHA-256 and checked, then rewritten with `build.pins`,
`build.pins_source` and `provenance.supersedes {run_json_sha256, why}`, validated with the seal (the sealed files
unchanged). The index moved aside the same way (`index.duckdb.9436ccd10541c696`) and rebuilt: 6 ingested, 0 errors, a
second ingest 0 (the same six runs as before). The milestone `acceptance-i2-rc1.json`: pins and run.json hashes of the
two runs from the index, note rewritten (old copy `acceptance-i2-rc1.json.167ae22b45df0a63` there). `acceptance.py
verify` matches the index for `acceptance-i2-rc1` and `acceptance-inc1-d157`; `first-acceptance-inc1` lists its two
Earth TSL runs as not in the index, as before 0564 (moved out by 9073, D157). The page rebuilt:
`F:/c5-64bit-work/ledger/page/index.html` (4,957,004 B, 6 acceptance runs, 3 frozen comparisons, 98 pictures; old page
and `shots.json` backed up there): batch 4's `c29442e8c`/`e96457fd5` 0 times in it (8 before), `4f4daac70` 10.
`audit_pins.py` over the store: the four inc1 records' pins attested by the lock history, the stock i2 arm's too, the
i2-rc1 arm's by its manifest only.

### 8.7 The page for a lay reader, and the Increment 2.5 stress series (one-off 9079, 2026-11-21 to 23) `[C, tests; L, the arms 2026-11-22/23, workstation, not for timing]`

Written 2026-11-21 by oneoff-9079. The owner (2026-11-21 22:06, about 22:08 and 22:09 EST, ticket 9079): the 43-civ stress
series for main's build with the server drawing everything ("Increment 2.5") against the unpatched game, like against
like, and [reworded for privacy] charts meant for a lay audience that point out and annotate the crashes. About 22:20 the owner asked
for the new arms to run to turn 250 (the 2026-11-16 arms: stock to its crash at 225, increment 1 to 230). The
Increment 2 arm (i2-rc1 rebuilt as a 43-civ build) was dropped by the owner before any work on it.

**What was wrong** (the owner's screenshot of the stress card, the page of 0564's build, picture
`vc-20261121-222736-workstation-f259`): the x axis had `dtick: 1` (31 overlapping labels); each chart had its own
Plotly legend under it with the full run labels, cut at the chart's width ("... Deity tur"); the grid's items kept
Plotly's width (`minmax(320px, 1fr)` without `min-width: 0`), so a chart could run past the card; the turn-time title
read "turn time (indication) (s), indication only"; a crashed run's line simply stopped.

**The page now** (`perf/ledger/page/acceptance.py`, `acceptance.js`, `acceptance.html`, `app.css`) `[C]`:
- `acceptance.py` gives the page what it says, so the Python tests check it: `BUILDS` (the build of a run from its
  `acceptance.client`: "Stock" -> Unpatched game, "Increment 1", "Increment 2", "Increment 2.5"; a prefix counts only
  as the whole client name or followed by a space, so "Increment 2.5 ..." is never Increment 2) with one fixed colour
  each (the dataviz reference palette's categorical slots 1-4, `--s1`..`--s4`, light and dark steps already in
  `app.css`) and one marker shape each; an unknown client gets the next of `--s5`, `--s7`, `--s6`, `--s8` and keeps
  it on the whole page. `story(run)`: one plain sentence per run, and for a crash the chart label; a crash is told as
  "it ran out of memory" when the last sample's largest free block is below 64 MB or a crash line names the address
  space, else "the cause is not recorded in this run". `GROUP_TEXT`: a plain heading and lede per map name (the
  stress map and Large Communitu; others fall back to "<map>, from turn N"). `SERIES` gained `title` (plain, with the
  unit), `explain` (meaning and which way is good), `tech` (the technical name, shown in brackets) and `chart` (order:
  memory left, biggest free piece, what the game program uses, the 64-bit helper, time per turn).
- The palette check (the dataviz validator, 2026-11-21): the four slots pass on the adjacent pairlist that lines use;
  under all pairs no four of the eight hues pass (blue/violet and orange/yellow are too close), so each build also has
  its own marker, and each line is named at its end (the validator's secondary encoding).
- **Like against like**: a group is now one (map, turn, save, EXE build); runs of the same save on another EXE build
  are their own card, the newest first, the others headed "(earlier measurement)" with a line saying so. On the real
  index (6 runs, one EXE build) the page has the same two cards as before.
- `acceptance.js`: per card the plain heading and lede, the saved game in small type, "What happened" (one line per
  run, a swatch of its line and marker, the player's name of the build, the technical client name small, a "crash" or
  "no crash" tag and the story), then the charts: one per series, a plain title, the explaining line with the
  technical name, no Plotly legend, turn ticks every 2 turns (runs of up to 12 turns) or 5, `hovermode: "x unified"`
  (each build's value at that turn), memory-left charts from zero; every crash on every chart: a dashed line in the
  critical colour at its turn, a cross on the run's last point and the label above the plot; every run that did not
  crash: its name at its line's end, pushed apart where ends meet; a repeat run of the same build in one card is dashed
  and named "(run 2)". The charts wrap (`minmax(min(100%, 440px), 1fr)`, `min-width: 0`), and are redrawn when the
  theme changes (the colours are read from the page's CSS). The table of exact numbers (plain row names, technical in
  brackets) and the turn-by-turn pictures are in two closed sections below the charts.
- Tests (`perf/ledger/page/tests/test_acceptance.py`, 2026-11-21): the build of each client name (10 cases), fixed
  distinct colours and markers present in all three theme blocks of `app.css`, a plain title, unit and explanation for
  every chart, the stories (out of memory by the last sample or by the crash line, cause not recorded, finished,
  ended otherwise), the payload with four builds and an unknown one, the EXE-build split, and
  `tests/pagejs_harness.js`: node runs `acceptance.js` against a fake page and returns every `Plotly.react` call; the
  test checks no legend, `dtick`, the crash shape and label on every chart of the crashed card, the end labels equal
  the drawn lines less the crashed one, colour and marker by build, no crash mark where nothing crashed. 206 tests of
  `perf/ledger` pass `[L, 2026-11-21 22:2x]`.
- Pictures (diagnostics of the page, not of the game; headless Chrome at 1280 px, `--blink-settings=
  preferredColorScheme=0|1`, the page served from a local folder): before `vc-20261121-222736-workstation-f259`,
  after dark `vc-20261121-222737-workstation-7a69`, after light `vc-20261121-222738-workstation-68d6`.

**The arms** `[L]` (milestone `perf/ledger/milestones/acceptance-stress-i25.json`, frozen 2026-11-23 by oneoff-9079,
`acceptance.py verify` matches the index). Both on the Goldberg instance c5h-new-b, EXE build 26577625 (sha256
`a7b4f520...`), save `TSstd-deity-T200` (`225b2e50...`, 8.4), the same driver, camera (107,31) and sampling
(`perf/profilekit/acceptance.py run ... --turns 50 --turn-timeout 3600 --camera 107,31`, wrapper
`C:/c5-64bit-work/9079/run_arm.sh`), one after the other. Not for timing: other agents' builds and runs shared the
workstation both times.

| | Stock 32-bit 43-civ | Increment 2.5 64-bit 43-civ |
|---|---|---|
| Run id | `20261122T092809Z-ws-46e996` | `20261123T072336Z-ws-3e8406` |
| Ran | 2026-11-22 04:28-05:38 EST | 2026-11-23 02:23-05:30 EST |
| Ending | crash during turn 215 (box 05:38:36, after the turn-214 row): out of address space | autoplay handed back at turn 250, no crash |
| T200 EXE private / free / largest free (MB) | 2712 / 607.5 / 554 | 1943 / 1414 / 1356 |
| T214, last common turn | 2980 / 189 / 23.5 | 2166 / 1184 / 1058 |
| T250 | - | 2325 / 1064 / 953 |
| Server private T200 / T214 / T250 (MB) | - | 3514 / 5040 / 5279 |
| Fit T201-210, EXE private (MB/turn, ci95) | +8.45 (1.63) | +8.72 (4.88) |
| Fit T201-210, EXE largest free | -16.16 (4.03) | -6.49 (6.17) |
| Fit T201-210, server private | - | +82.8 (42.0) |
| Turn wall T201-214, mean | 270 s | 186 s (all 50 turns: 216 s) |

- **Stock**: the 0.1b pin's instance-aware 43 DLL `ba520010...` (`deps/i0.lock` instance-43, attested with
  `--pins 846b57d63`; pins dll x64-0.1b `1b87dd185`, i0 i0-0.1b `f33fc46a4`), laid by `provision.py set-dll` from the
  copy `C:/c5-64bit-work/9079/inputs/m43-ia.dll`. The crash box said the 32-bit address space was nearly exhausted
  (access violation on a write; total free 177 MB, largest free block 24064 KB, Sub2G free 30 MB). Evidence: crash
  files, minidump and the turn-214 autosave copy in `C:/c5-64bit-work/9079/arms/stock/crash` (not committed). The
  2026-11-16 stock arm (8.4) crashed at turn 225 on EXE build 4390913; that it crashes 10 turns earlier on 26577625 is
  `[I]` (one run each, other EXE build, other day): not root-caused, the cause of both crashes is the address space.
- **Increment 2.5**: main at `cbfe2f6dc` (9079's branch = main `f9891ef72` merged, which carries `65afea3a9`; no product
  file changed by the branch) built by the one-off (MC #11069/#11077, no candidate carried a 43-civ take):
  `tools/x64-dll/build_all.py --src <pin 1b87dd185> --config release --major-civs 43` -> x64 DLL `08a79346...`
  ("Release-5.4.8-80-g1b87dd185+m43 Copy", smoke "Supporting 43 major civilizations"), `server/build_server.py --deps`
  of it -> server.exe `cb7c3828...`, proxy `9f7435d5...`, vp64helper `991b04b6...`; `shell/build.cmd` ->
  shell_composite.dll `fa085c88...`. 32-bit fallback beside the proxy: the same `ba520010...` as the stock arm. Laid by
  `research/02-exe-internals/evidence/4967/lay.py prep --composite-dll --composite-ini` (no installer), then
  `VP64Options.ini` written as the installer writes it (`perf/evidence/9079/write_options.py`) from the second beta
  candidate's option set (`C:/c5-64bit-work/cand2/Release-5.4.8-beta2.json`): [Renderer] ServerUnits 1, ServerCombat 1,
  ServerCities 1, ServerLandmarks 1, ServerFarmFields 1, ServerArc 0, ServerPath 0; [Performance] AiUpdateTickMs 0,
  AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1, GameEventsOneCrossing 0; then `lay.py helper on` added
  ServerRenderer 1 and the patch's config.ini keys (Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons). Every other
  option at the server's built-in default. Pins attested at `9095f5cb1` (9079's branch) by a `deps/dll.lock` release-43
  entry naming these hashes, which the sealed record cites. Corrected 2026-11-23 on the MC's review: `release-43` is
  the release pipeline's 43-civ build (`tools/release/stages/build.py`, `take_release.py` read it), so on the merged
  branch it is 0334's again, exactly as on main, and this test build is `stress-43` / `verified-stress-43`;
  `perf/ledger/record.py` `LOCK_HASH_KEYS` gained `stress-43` (test `test_arm_pins.py`
  `test_a_stress_43_entry_attests_the_x64_test_build_and_never_touches_release_43`). The record stays valid: `9095f5cb1`
  is reachable through the merge (was: "release-43 at 9095f5cb1, 0334's kept as release-43-b1"). Removed by `lay.py restore`
  (restore ok, by hash), `set-dll --previous` (back to `ff0f6e86...`), `provision.py verify` ok.
- **The page**: the like-for-like stress card (these two runs), a second card with Increment 1 of 2026-11-16 added
  (`with_earlier`), and the 2026-11-16 card as "earlier measurement"; built into `C:/c5-64bit-work/9079/page-final`
  (the live page `F:/c5-64bit-work/ledger/page/` is rebuilt by the MC after the merge). Pictures (diagnostic, of the
  page): stress card light `vc-20261123-053333-workstation-3094`, dark `vc-20261123-053333-workstation-fd3c`, the card
  with Increment 1 `vc-20261123-053334-workstation-00ec`. Previews sent to the owner while the arms ran
  (`perf/evidence/9079/preview.py`: live `per-turn.csv` + the index read-only, nothing written to the ledger).

**Not verified**: the Increment 2.5 arm is not the installer's layout: the installer's VP Lua fixes (ticket 0638) were
not laid (named in the run's client build and the page's table), and the composite's `shell_composite.ini` is the
shell build's shipped default; no HI journey, parity or save test ran on this x64 build; turn times are from a busy
machine; one run per arm; `ba520010`'s own build record is lost (i0.lock verified-43).

**What cost time** (2026-11-22): `--client-build` over 80 characters or with a comma is refused before launch; a
worktree without `tools/harness/native/bin` ends at launch with exit 3 (both in the runbook 5.4); after a crash box the
driver leaves the front end patched (`provision.py verify` fails until `python -I tools/harness/frontend.py -i <inst>
restore`) `[L]`.

