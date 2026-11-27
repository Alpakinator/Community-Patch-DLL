# Upstream export: the D83 audit of the notes group

Ticket 4717 (epic 0468, package E2, the audit half of 4703), 2026-11-18 05:58-06:40 EST, account B. **Read-only: no
notes file was changed.** This file is the list the later E2 edit slices work from. D83: nothing taken from the EXE or
the game's files is committed or shipped; our own descriptions of behaviour may go (plan
`../07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md` 4.4 points 1 and 2). The export's other
checks (paths, names, private terms) are in `upstream-export.md`; this file does not repeat them.

**Delta of 2026-11-25 (ticket 0768): section 8** audits what was added or changed up to main 0b11dd083 (210 files):
17 replace passages in 10 files (13 new, plus `addresses.md`'s four of 4.1 re-found), J1 28 and J2 47 places. Sections
1-7 stay the reading of 02605bab9.

## 1. Result [C, reading of `main` 02605bab9, 2026-11-18]

- **Scope:** the notes group of `tools/export/allowlist.txt` at 02605bab9 (`check.py --groups notes --list`): 147
  files, 146 Markdown files and one SVG, 92,711 lines, 9.1 MB. List: `tools/export/evidence/4717/notes-files.tsv`.
- **26 passages to remove or replace, in 13 files, 167 lines, 19.3 KB** (0.2 % of the group's bytes): 15 in section 02
  (5 files), 11 in section 05 (8 files). None in sections 03, 04, 06, 07, 08 or the READMEs. By kind: byte signatures
  of EXE code or data 10, instruction sequences or shader instructions quoted from a disassembly 6 (one of them also
  with bytes), decompiler text 1, import lists 3, address lists from a scan or a tool's output 2, shader indexes 3
  (one of the scan lists is one too), a transcription of a game data file 1, a table of the EXE's strings 1. Every one
  is a **replace**: in each case the surrounding text already says, or can say in one line, what the passage shows.
  Nothing needs to be removed without a replacement.
- **No picture of the game and no save is in the notes group** (plan 4.4 point 2). The only image is our own diagram
  `../07-architecture-decisions/ownership-end-product.svg` (pure vector, no embedded raster, 18,115 B). The notes name
  pictures 166 times and saves 66 times by file name or path; none of those files is in the group (section 6).
- **Proposed kept, for the MC's or the owner's judgement (section 5):** five classes, each a decision for the whole class,
  not per passage: the EXE's own strings used as names (J1, 23 flagged lines or blocks in 12 files), single
  instructions quoted to identify a site (J2, 40 flagged lines or blocks in 18 files), the contents and measurements of
  game model files (J3, 2 tables in one file), shader behaviour written as formulas (J4), and two names of the owner's own
  saves (J6, a privacy point, not D83).
- **Kept as our own description:** 259 of the 273 flagged blocks and 69 of the 100 flagged lines (the J places above
  among them; the rest are address and offset tables with what each address does in our words, file names the server
  must load from the player's install, our own builds' code, Windows' code, false hits). 71 files have a flagged
  passage; the other 75 Markdown files have none. The per-file counts are in section 4.3; every flagged passage with
  its class is in `tools/export/evidence/4717/classified.tsv`.
- This file itself is in the notes group (`research/0*/*.md`) and quotes no extract: the tables name the passages by
  line range and describe them; the evidence files carry no copy of the passages' text.

## 2. Method

### 2.1 Search [C]

Scripts in `tools/export/evidence/4717/` (Python 3.11, read-only on the worktree):

1. `notes-files.tsv`: `python -I tools/export/check.py --rev 02605bab9 --groups notes --list --no-terms
   --no-env-terms`, the allow-list as `check.py` reads it (the `[unclear]` and `[private]` sections already win).
2. `scan.py` splits each Markdown file into blocks (fenced code, tables, list runs, paragraphs) and counts per block:
   EXE-range addresses (`0x` and 5-8 hex digits, or bare 8-digit VAs), `+0x..` offsets, instruction shapes (a
   mnemonic followed by a register, a memory operand or a size keyword: prose such as "call 0x6D2340" does not count),
   Ghidra pseudo-C tokens (`FUN_`/`DAT_`/`LAB_`, `param_N`, `iVarN`, ...), runs of 8 or more two-digit hex tokens,
   import names (`dll!name`, `__imp_`, the system and game library names), allocation words with addresses, asset file
   names (`.dds .gr2 .fx* .hlsl .xml .fpk ...`), quoted strings and `TXT_KEY_`, mangled C++ names. A block is flagged
   when a count passes a threshold (`classify()` in `scan.py`: for example 12 addresses in a table or list, 15 in a
   paragraph, 3 instruction shapes, 2 byte runs, 10 asset names, 12 quoted strings in a table). Output `hits.tsv` (273
   blocks in 69 files; the counts only).
3. `linescan.py` searches line by line for the narrow kinds: 6 or more hex bytes (with `??` wildcards), two
   instruction shapes on one line, decompiler tokens, import slots and thunks, the EXE's source paths
   (`.\App\...`), profiler labels and allocator tags, and lines that give 6 or more addresses from a "byte scan",
   "callers", "references", "readers" or "writers" search. Output `linehits.tsv` (100 lines in 25 files; line numbers
   and kinds only).
4. A further search for shader bytecode disassembly (`dcl_*`, `sample_*`, `mad r`, `dp4 r`, `cbN[i].xyzw`) over the
   whole group: 33 lines in 11 files, all single instructions or register names inside descriptions (classed J2 or
   kept), no shader listing.
5. `pics_summary.py` over the picture and save references (`pictures.tsv`, `pictures-classified.tsv`), plus a search
   for Markdown images, `<img`, `data:image` and base64: one image (the SVG), one `data:image` mention in prose
   (`../08-performance/metrics-ledger.md` 1286, about a page's thumbnails), none embedded.

### 2.2 Reading

Every flagged block and line was read by hand at least to its first lines, and every passage proposed for replacement
and every judgement passage in full. `classify.py` records the reading: an override per passage read (file, line
range, kind, class, action, note) and a default per search kind for the rest (an address table or offset table: kept
as K1/K2; an asset-name block: K4; a quoted-string block: K0; a single instruction: J2). The run refuses to finish
while a block with decompiler, disassembly, byte or import hits has no override (0 left). `tables.py` prints section
4's tables from `passages.tsv` and `per-file-classes.tsv`; `jlist.py` prints section 5's place lists.

### 2.3 The classes

| Class | What | Proposed action |
|---|---|---|
| E1 | bytes of the EXE's code or data (signatures, patterns, a constant's bytes) | replace: say what is located or checked; the product's checks keep their own patterns |
| E2 | a sequence of two or more instructions, or a line of a game shader's disassembly | replace by the behaviour in words (usually already beside it) |
| E3 | decompiler output (pseudo-C) | replace by the behaviour in words |
| E4 | an import list: the import directory's libraries, slots or thunks as a list | replace by one line; a single slot the product or an instrument uses stays (K5) |
| E5 | a list of addresses copied from a scan or a tool's output (readers, call sites) | replace by the count and what they belong to |
| E6 | a shader index: rows per shader blob of the EXE's `.shader` section | replace by a pointer to the product's generated table and the decoder |
| E7 | a transcription of a game data file (rows copied from an XML) | replace: the values are in the player's own file, which the product may read at run time (D83) |
| E8 | a table of the EXE's strings with their sites | replace by what the strings identify |
| J1 | one of the EXE's own strings used as a name (a profiler label, a source path, an allocator tag, a literal) | keep, proposed (class decision) |
| J2 | a single instruction quoted to identify a site (a crash instruction, a gate's test) | keep, proposed (class decision) |
| J3 | contents or measurements of a game file (mesh names, counts, sizes) | keep, proposed (class decision) |
| J4 | a game shader's behaviour written as formulas in our notation | keep, proposed |
| J6 | a name of one of the owner's own saves | keep for D83; privacy is the MC's call |
| K0 | a false hit (our own JSON, tracker text, prose) | keep |
| K1 | an EXE address or address table with what it does in our words | keep: our description of behaviour, for interoperability |
| K2 | a member offset table with what each member holds | keep, as K1 |
| K3 | code that is not the EXE's or the game's: our DLL, server and linker output, Windows' system DLLs, SQLite | keep: outside D83 (Windows' few bytes are Microsoft's; named, not copied at length) |
| K4 | game file names, formats and locations the server needs to load the player's own files | keep: D83 lets the product read assets from the player's install at run time, and it has to name them |
| K5 | a single named import slot or a static import the product or an instrument relies on | keep |

### 2.4 Limits [I]

- **A search finds what it is shaped for.** 75 files have no flagged passage; they were not read line by line. A
  mechanical extract written in a shape the patterns miss (for example a list of addresses of fewer than 12 rows with
  no "scan" word, or an EXE string table of under 12 strings) would not be found. The thresholds were set by reading
  the hits of a first, wider run (523 blocks) and checking what the tightened run dropped.
- **Long passages were read at their flagged parts.** For tables of a hundred rows or more (`addresses.md` 97-365),
  the line-level search found the rows with bytes and imports; the other rows were read in samples.
- **Product files are out of scope.** Byte signatures also live in the product's source and tests (the shell's
  signature checks, the generated create-site table `render/src/shaders/create_sites_table.inc`); whether those count
  as extracts under D83 is a product question for E1 and the MC, not answered here.
- The classes K1, K2 and K4 follow D83's own words ("re-implemented EXE algorithms are written from the knowledge base's
  description of their behaviour"): an address with our account of what it does is that description. If the MC reads
  D83 more strictly for a public export (no addresses at all), most of sections 02 and 05 would need rewriting, a much
  larger job not sized here.

## 3. Proposed edits by class

- **E1 (10 passages).** Every byte signature in the notes duplicates a check the product performs; the note should
  say what is checked and where, and leave the bytes to the product. In `disable-systems-and-masks.md` 755-768 the
  table keeps its RVA and name columns.
- **E2, E3 (7 passages).** The behaviour is already stated next to each quoted sequence; the edit deletes the quote and,
  where needed, adds the words "the call at X returns to Y".
- **E4 (3).** One line each. The single slots and thunks the product and the instruments use stay (K5, section 5.3).
- **E5, E6 (5).** The create-site and shader tables of `asset-pipeline.md` and `unit-draw-filter.md` came out of the
  create-site tool; the product already ships the generated table, so the notes can point at it and keep the
  material-to-system mapping, which is our finding.
- **E7 (1).** `overlay-system.md` 500-530 copies 29 rows of the game's `Highlights.xml`. The note keeps the counts per
  style type and says the values are in the player's own file (no product code reads it today [C: search of
  `render/src`, `server/src`, `shell` on 02605bab9]).
- **E8 (1).** `effects-formats.md` 206-221 lists 14 groups of the EXE's strings with their sites; the role column is
  the finding and stays as a list of loaders.

## 4. The passages, per section

### 4.1 Remove or replace, and kept with a stated reason

The tables give the file (relative to its section folder), the line range at 02605bab9, the kind, the class, the size
and the proposed action. Rows marked **keep** are passages the search flagged that are kept with the reason given
(K3 and the containers) or are judgement passages (J); section 5 lists the judgement classes' other places.

#### 02-exe-internals

| File: lines | Kind | Class | Size | Proposed action |
|---|---|---|---|---|
| `addresses.md`: 79-84 | import list | E4 | 6 L, 463 B | **replace**: the import directory's per-library table addresses, 19 libraries; one line: the IAT slots the product uses are named where they are used |
| `addresses.md`: 135 | byte signature | E1 | 1 L, 135 B | **replace**: drop the parenthesised signature (12 B of code bytes) |
| `addresses.md`: 137 | byte signature | E1 | 1 L, 128 B | **replace**: drop the parenthesised signature (12 B of code bytes) |
| `addresses.md`: 254 | byte signature | E1 | 1 L, 133 B | **replace**: drop the parenthesised signature (18 B of code bytes) |
| `allocations-and-memory-map.md`: 22-26 | import list | E4 | 5 L, 504 B | **replace**: enumeration of the 30 imported libraries and of named imports from imports.txt; one line: which kinds of library the EXE links (CRT, D3D9/11, D3DX, Steam, Lua, the two game libraries, Windows) |
| `allocations-and-memory-map.md`: 36 | byte signature | E1 | 1 L, 396 B | **replace**: drop the quoted prologue bytes (5 B) from the allocator row |
| `camera-and-picking.md`: 209-217 | byte signatures | E1 | 9 L, 905 B | **replace**: four code patterns with their bytes (about 70 B); one line each: what is located where, 'by a byte pattern the product checks' |
| `camera-and-picking.md`: 263-265 | import thunk list | E4 | 3 L, 327 B | **replace**: eight D3DX9 import thunks by address and name; one line: 'the D3DX9 math thunks lie at 0x8A86A4..0x8A8734' |
| `camera-and-picking.md`: 333-342 | byte signatures and disassembly | E1,E2 | 10 L, 1,134 B | **replace**: 11 patterns given as bytes and instruction sequences; keep the list of what is checked, drop the bytes and instructions |
| `disable-systems-and-masks.md`: 95 | disassembly | E2 | 1 L, 85 B | **replace**: a 4-instruction sequence; line 94 already says it in words: keep line 94, drop the quoted sequence |
| `disable-systems-and-masks.md`: 323-327 | byte signatures | E1 | 5 L, 320 B | **replace**: four signatures of 12-22 B; one line: the four sites that are verified before the write |
| `disable-systems-and-masks.md`: 566-567 | shader instruction | E2 | 2 L, 235 B | **replace**: a line of a game shader's disassembly (one instruction); the formula after it says the same in words |
| `disable-systems-and-masks.md`: 638-641 | scan result list | E5 | 4 L, 470 B | **replace**: 21 reader sites of one global from a byte scan; replace by the count and the systems they belong to, keep the two writers |
| `disable-systems-and-masks.md`: 755-768 | byte signature table | E1 | 14 L, 1,312 B | **replace**: 13 rows of code bytes (9-27 B each); keep the RVA and 'name in the log' columns, drop the bytes column, one line on the operand check |
| `world-systems-and-stubs.md`: 87-92 | decompiler text | E3 | 6 L, 281 B | **replace**: a 4-line pseudo-C excerpt with Ghidra's automatic names; the paragraph before it describes the crash in words |
| `addresses.md`: 97-365 | address table | K1 | 269 L, 25,544 B | **keep**: the main VA table: each row an address and what it does in our words; rows 135, 137 and 254 carry signature bytes (rows above) |
| `disable-systems-and-masks.md`: 466-490 | shader behaviour as formulas | J4 | 25 L, 2,718 B | **keep**: a game shader's last steps written as formulas in our notation, the reasoning for the white mask |
| `world-systems-and-stubs.md`: 165-174 | EXE strings and instruction quotes | J1,J2 | 10 L, 1,016 B | **keep**: two profiler labels, two source paths and two mask tests quoted to name the sites |

#### 03-dll-engine-boundary

| File: lines | Kind | Class | Size | Proposed action |
|---|---|---|---|---|
| `combat-on-the-product-path.md`: 111-129 | instruction quotes | J2 | 19 L, 2,080 B | **keep**: single instructions (a return with its pop count, an argument load, an address computation) quoted inside the argument-use proof |
| `error-report-exe-crash.md`: 305-316 | disassembly of our DLL build | K3 | 12 L, 1,191 B | **keep**: the x64 DLL is built from GPL source by this project |
| `save-determinism.md`: 53 | save name | J6 | 1 L, 251 B | **keep**: names one of the owner's own saves (not a D83 question: privacy, the file does not travel) |
| `server-host.md`: 212 | save name | J6 | 1 L, 118 B | **keep**: names one of the owner's autosaves (as above) |
| `server-host.md`: 561-569 | disassembly of our server.exe | K3 | 9 L, 889 B | **keep**: LLD's delay-load thunk in server.exe |
| `x64-dll-port.md`: 144-145 | linker symbols | K3 | 2 L, 233 B | **keep**: __imp_ symbols of our own build |

#### 05-graphics-composition

| File: lines | Kind | Class | Size | Proposed action |
|---|---|---|---|---|
| `asset-pipeline.md`: 383-391 | shader index | E6 | 9 L, 935 B | **replace**: rows per shader blob of the EXE's .shader section (address, stage, size, call site, decoded declarations); one line pointing at the product's generated create-site table and the decoder |
| `asset-pipeline.md`: 467-479 | shader index | E6 | 13 L, 3,450 B | **replace**: per material: the SM5 blob addresses of every pass; keep the material -> EXE system and draw column, drop the blob lists |
| `asset-pipeline.md`: 488 | shader index | E6 | 1 L, 859 B | **replace**: the same table's last row |
| `effects-formats.md`: 206-221 | string table | E8 | 16 L, 2,039 B | **replace**: 14 rows of EXE strings (file paths and log formats) with their site VAs; replace by the list of loaders it identifies, in words |
| `fog-of-war.md`: 1898 | byte signature | E1 | 1 L, 115 B | **replace**: drop the pattern bytes (10 B) |
| `lockstep-probe.md`: 40-42 | disassembly | E2 | 3 L, 349 B | **replace**: a 7-instruction sequence at 0x51E1C3; keep 'the call at 0x51E1C3, return address 0x51E1C8' in words |
| `overlay-system.md`: 500-530 | game data transcription | E7 | 31 L, 2,485 B | **replace**: 29 rows copied from the game's Highlights.xml (style, type, width, texture, colour); one line: the styles and their parameters are in the player's own Highlights.xml; keep the counts per style type |
| `path-models.md`: 237 | data bytes | E1 | 1 L, 110 B | **replace**: drop the float constant's four bytes; its value stays |
| `shell-foundations-verification.md`: 324-330 | disassembly | E2 | 7 L, 803 B | **replace**: the two writer instructions quoted; keep the byte-scan method and the counts |
| `shell-foundations-verification.md`: 335-336 | shader instruction | E2 | 2 L, 235 B | **replace**: the same game-shader line as disable-systems-and-masks.md 566 |
| `unit-draw-filter.md`: 238-251 | create-site table | E5,E6 | 14 L, 1,131 B | **replace**: 12 rows: program, blob VA, push / call / store sites, handle global, from the create-site tool; one line naming the handles the filter reads |
| `asset-pipeline.md`: 311-341 | method and results | K1,J2 | 31 L, 3,381 B | **keep**: how the create sites were found, in words, with two operand forms quoted |
| `landmarks-and-models.md`: 281-300 | game file measurements | J3 | 20 L, 2,472 B | **keep**: per-file sizes, compression and section sizes of 18 game models: our measurements of the format, no content |
| `landmarks-and-models.md`: 353-369 | game file contents | J3 | 17 L, 3,040 B | **judgement**: per model: mesh names, vertex / triangle counts, skeleton and material names, bounding boxes; the names are strings of the game's files. Proposed (5.4): keep the counts and formats, drop the name lists |
| `lockstep-probe.md`: 43-56 | address list | K1 | 14 L, 1,386 B | **keep**: the probe's design in words; lines 40-42 (above) quote the instruction sequence |
| `terrain-bake-residency.md`: 223-230 | false positive | K0 | 8 L, 916 B | **keep**: 'undefined' in prose |

#### 06-tooling-and-workflows

| File: lines | Kind | Class | Size | Proposed action |
|---|---|---|---|---|
| `headless-and-parallel-testing.md`: 105-117 | Ghidra names and an EXE string | J1 | 13 L, 1,171 B | **keep**: FUN_ names (addresses) and the mutex's name, which is the behaviour being described |
| `headless-and-parallel-testing.md`: 236-242 | two named imports | K5 | 7 L, 716 B | **keep**: that the EXE imports d3d9.dll statically (two functions named) is the behaviour the proxy relies on |
| `headless-and-parallel-testing.md`: 559-583 | EXE strings and Ghidra names | J1 | 25 L, 2,399 B | **keep**: the EXE's two wide-string literals of the folder names and Ghidra's FUN_ names: the folder rule |
| `run-and-drive-the-game.md`: 578-627 | Windows system DLL bytes | K3 | 50 L, 4,930 B | **keep**: first bytes of kernelbase!CreateProcessInternalW on two Windows builds (Microsoft's, not the game's) |
| `run-and-drive-the-game.md`: 693-697 | Windows system DLL bytes | K3 | 5 L, 533 B | **keep**: the same, per machine |
| `run-and-drive-the-game.md`: 783-808 | Windows system DLL instructions | K3 | 26 L, 2,568 B | **keep**: ntdll / shell32 / kernel32 stub shapes on Windows 11 |
| `run-and-drive-the-game.md`: 1056 | file magic | K3 | 1 L, 113 B | **keep**: SQLite's journal header magic |

#### 08-performance

| File: lines | Kind | Class | Size | Proposed action |
|---|---|---|---|---|
| `dse-scanlimit-turn-time.md`: 55-62 | disassembly of our DLL build | K3 | 8 L, 986 B | **keep**: compiler output of the DLL's own source |

### 4.2 Sections without a passage to change

`04-memory-investigation` (9 files), `07-architecture-decisions` (6 Markdown files and the SVG), every section README
and `research/README.md`: no passage to remove or replace. Sections 03, 06 and 08 have no extract either; their rows
above are flagged passages kept with a reason. `07-architecture-decisions/epic-0040-breakdown.md` 334-383 (a tracker
diff of our own) was flagged only as a block of quoted strings.

### 4.3 Flagged passages per file

Every file with a flagged block or line, with the count per class (section 2.3). Files not listed had no flagged passage.

| File | Classes |
|---|---|
| `02-exe-internals/README.md` | K1 1 |
| `02-exe-internals/addresses.md` | E1 3 E4 1 J1 1 K1 17 K5 2 |
| `02-exe-internals/allocations-and-memory-map.md` | J1 1 J2 1 K1 3 |
| `02-exe-internals/arrows-and-movement-path.md` | J1 1 |
| `02-exe-internals/camera-and-picking.md` | E1 5 E1,E2 4 E4 1 J1 1 K1 8 K2 2 |
| `02-exe-internals/crashes-huge-load.md` | K1 4 K2 1 |
| `02-exe-internals/disable-systems-and-masks.md` | E1 18 E2 1 E5 1 J1 4 J2 7 J4 1 K1 6 |
| `02-exe-internals/farm-builder-guard.md` | J2 1 K1 5 |
| `02-exe-internals/farm-field-double-free.md` | K1 4 |
| `02-exe-internals/game-core-thread.md` | K1 4 K5 3 |
| `02-exe-internals/leader-scene-player-limit.md` | K1 3 |
| `02-exe-internals/load-sequence.md` | K1 3 K5 1 |
| `02-exe-internals/message-queue-and-fixed-buffers.md` | J1 1 J2 2 K1 3 K5 2 |
| `02-exe-internals/world-systems-and-stubs.md` | E3 2 J1 1 J1,J2 4 J2 4 K1 16 K5 2 |
| `03-dll-engine-boundary/README.md` | K1 1 |
| `03-dll-engine-boundary/combat-on-the-product-path.md` | J2 1 |
| `03-dll-engine-boundary/error-report-exe-crash.md` | K1 1 K3 3 |
| `03-dll-engine-boundary/player-limits.md` | K1 2 |
| `03-dll-engine-boundary/proxy-design.md` | J2 2 K1 5 |
| `03-dll-engine-boundary/server-host.md` | K3 2 |
| `03-dll-engine-boundary/ui-lua-lock-holds.md` | K1 1 |
| `03-dll-engine-boundary/upstream-13406-product.md` | K1 2 K2 1 |
| `03-dll-engine-boundary/upstream-fix-batch-1.md` | J2 1 |
| `03-dll-engine-boundary/upstream-fix-batch-2.md` | K1 2 |
| `03-dll-engine-boundary/x64-dll-port.md` | K3 2 |
| `05-graphics-composition/README.md` | K1 1 |
| `05-graphics-composition/asset-pipeline-verification.md` | K1 1 |
| `05-graphics-composition/asset-pipeline.md` | E6 3 J1 1 K1 5 K1,J2 1 K4 1 |
| `05-graphics-composition/cities-and-landmarks.md` | J1 2 K1 9 K2 1 K4 2 |
| `05-graphics-composition/combat-choreography.md` | K1 4 |
| `05-graphics-composition/effects-formats.md` | E8 1 |
| `05-graphics-composition/fog-of-war.md` | E1 1 K1 6 |
| `05-graphics-composition/hooks-overlays-and-ceg.md` | J2 1 |
| `05-graphics-composition/landmarks-and-models.md` | J3 2 K1 1 K4 1 |
| `05-graphics-composition/lockstep-probe.md` | E2 1 K1 3 |
| `05-graphics-composition/model-reader-verification.md` | K0 1 K1 1 K4 1 |
| `05-graphics-composition/model-reader.md` | K4 1 |
| `05-graphics-composition/overlay-system.md` | E7 1 K1 2 K4 1 |
| `05-graphics-composition/path-models.md` | K1 1 |
| `05-graphics-composition/polar-ice.md` | J2 2 K1 7 K2 2 |
| `05-graphics-composition/proxy-route-compositor.md` | J2 1 |
| `05-graphics-composition/renderer-in-server-design.md` | J2 2 |
| `05-graphics-composition/shell-foundations-verification.md` | E2 1 |
| `05-graphics-composition/terrain-bake-blend.md` | K1 1 |
| `05-graphics-composition/terrain-bake-comparison.md` | K1 1 |
| `05-graphics-composition/terrain-bake-passes.md` | K1 3 K2 2 |
| `05-graphics-composition/terrain-bake-records.md` | K1 1 K2 1 |
| `05-graphics-composition/terrain-bake-residency.md` | J1 1 K0 1 K1 2 |
| `05-graphics-composition/terrain-decals.md` | J2 2 K1 11 K2 1 K4 3 |
| `05-graphics-composition/terrain-rendering-inventory.md` | K1 1 K2 1 K4 1 |
| `05-graphics-composition/terrain-surface.md` | K1 5 K2 1 K4 1 |
| `05-graphics-composition/trees.md` | J2 1 K1 6 K2 1 K4 2 |
| `05-graphics-composition/unit-controller.md` | K4 1 |
| `05-graphics-composition/unit-draw-filter.md` | E5,E6 1 J2 3 |
| `05-graphics-composition/unit-formations.md` | K1 4 |
| `05-graphics-composition/unit-renderer.md` | K1 1 K4 1 |
| `05-graphics-composition/unit-server-scene.md` | J2 3 K1 2 |
| `05-graphics-composition/unit-state-ii.md` | J2 1 |
| `05-graphics-composition/water-system.md` | K1 10 K2 4 |
| `06-tooling-and-workflows/headless-and-parallel-testing.md` | J1 2 K1 1 K5 5 |
| `06-tooling-and-workflows/human-input-testing.md` | K0 1 |
| `06-tooling-and-workflows/installer-notes.md` | K0 1 |
| `06-tooling-and-workflows/offline-scene-harness.md` | K0 1 |
| `06-tooling-and-workflows/release-driver.md` | K0 1 K1 2 |
| `06-tooling-and-workflows/reverse-engineering-setup.md` | J1 3 K1 1 |
| `06-tooling-and-workflows/run-and-drive-the-game.md` | K0 1 K3 11 |
| `07-architecture-decisions/epic-0040-breakdown.md` | K0 1 |
| `08-performance/dse-scanlimit-turn-time.md` | K3 1 |
| `08-performance/marathon-stress-t2000.md` | K1 4 |
| `08-performance/metrics-ledger.md` | K0 1 |
| `08-performance/turn-time-baselines.md` | K0 1 |

## 5. For the MC's or the owner's judgement

Each is one decision for its whole class. The audit's proposal is in bold; until the MC rules, the E2 edit slices
leave these places as they are.

### 5.1 J1: the EXE's own strings as names (proposed: **keep**)

The notes name EXE functions and objects by the EXE's own profiler labels, allocator source paths and a few string
literals (a mutex name, the user-folder names), because the EXE has almost no RTTI (45 descriptors,
`../02-exe-internals/allocations-and-memory-map.md` 1). They are short identifiers used as names, not tables of strings
(the one table, `effects-formats.md` 206-221, is E8 above). Places (23 flagged lines or blocks in 12 files, merged
where a line lies in a block): `02-exe-internals/addresses.md` 373; `allocations-and-memory-map.md` 28; `arrows-and-movement-path.md` 42; `camera-and-picking.md` 26;
`disable-systems-and-masks.md` 30, 34, 529, 647; `message-queue-and-fixed-buffers.md` 81; `world-systems-and-stubs.md`
94, 165-174; `05-graphics-composition/asset-pipeline.md` 603; `cities-and-landmarks.md` 93, 96;
`terrain-bake-residency.md` 76; `06-tooling-and-workflows/headless-and-parallel-testing.md` 105-117, 559-583;
`reverse-engineering-setup.md` 132-134. The alternative (strict): replace each by our own name for the system; the
names would then no longer match what a reader finds in the EXE.

### 5.2 J2: single instructions quoted to identify a site (proposed: **keep**)

A crash instruction from a dump, the test a gate performs, or the one instruction a hook depends on, quoted once
inside a description (40 flagged lines or blocks in 18 files, merged below where a line lies in a block; a range is
a block with one or two such quotes):
`02-exe-internals/allocations-and-memory-map.md` 59-91; `disable-systems-and-masks.md` 34-59, 82-85, 113-122, 124-149,
638-677, 1393-1414, 1487-1561; `farm-builder-guard.md` 13-40; `message-queue-and-fixed-buffers.md` 16-27, 134-165;
`world-systems-and-stubs.md` 81-85, 165-174, 211-213, 310-321, 365-408;
`03-dll-engine-boundary/combat-on-the-product-path.md` 111-129; `proxy-design.md` 2504-2511, 3064-3067;
`upstream-fix-batch-1.md` 832-845; `05-graphics-composition/asset-pipeline.md` 311-341; `hooks-overlays-and-ceg.md`
120-134; `polar-ice.md` 111-112, 445-453; `proxy-route-compositor.md` 213-218; `renderer-in-server-design.md` 113-120,
1383-1406; `terrain-decals.md` 832-835, 2692-2712; `trees.md` 289; `unit-draw-filter.md` 227-236, 771-819, 831-839;
`unit-server-scene.md` 1681-1769, 2299-2353; `unit-state-ii.md` 97-103. Some of these are our own DLL's or a
register name, not the EXE's (for example `proxy-design.md` 2504-2511 is SQLite in the game database DLL); they are
listed so the class is complete. The alternative (strict): put each in words ("the crash is a byte compare through a
NULL pointer"); about 40 small edits.

### 5.3 K5 kept for reference: single named imports

Not a judgement item, listed because the plan names import lists: the single IAT slots and static imports the product
or an instrument relies on stay (15 places in 6 files): `02-exe-internals/addresses.md` 72, 382;
`game-core-thread.md` 31, 81, 109; `load-sequence.md` 91; `message-queue-and-fixed-buffers.md` 23, 104;
`world-systems-and-stubs.md` 107, 379; `06-tooling-and-workflows/headless-and-parallel-testing.md` 174, 236-242, 546,
551, 624. The rows `addresses.md` 291-296 (six CRT import slots the allocation instruments hook, one per row with what
the hook does) are kept as K1 for the same reason.

### 5.4 J3: contents and measurements of game files (proposed: **keep 281-300, replace the names in 353-369**)

`05-graphics-composition/landmarks-and-models.md` 281-300 gives per game model file the size, compression and
section sizes (our measurements of the format, no content); 353-369 gives per model the mesh, skeleton and material
names (strings of the game's files), counts and bounding boxes. The counts and formats are what the model reader
needed; the names add nothing a reader of the player's own files cannot see. Proposed: keep 281-300, keep 353-369's
counts and drop its name lists. The K4 tables of game file names (16 places in 12 files, `classified.tsv`) name
files the server loads from the player's install and stay.

### 5.5 J4: a game shader's behaviour as formulas (proposed: **keep**)

`02-exe-internals/disable-systems-and-masks.md` 466-490 writes the last steps of a building pixel shader as formulas
in our notation (the colour key, the FOW lerp, the distance fog), the reason the white mask works. Shaders themselves
are read from the player's install at run time (D83); this is our description of what one computes, the same kind of
text as the terrain and water shader descriptions in section 05 (`terrain-surface.md`, `water-system.md`).

### 5.6 J6: the owner's own save names (not a D83 question)

`03-dll-engine-boundary/save-determinism.md` 53 and `server-host.md` 212 name one of the owner's own saves and one of their
autosaves (a path under the game's saves folder). No save travels. Whether a save's name is a private detail is the
private-terms check's question (`upstream-export.md` 2.3); listed for the MC. The other 64 save names in the notes
are the project's test-save copies (`C5H-...`, `9020-HUGE_0350`, ...) or placeholders (`NAME.Civ5Save`).

## 6. Pictures and saves (plan 4.4 point 2) [C]

- **In the group:** one picture, `07-architecture-decisions/ownership-end-product.svg`, our diagram (18,115 B; no
  `<image>`, no base64, no external reference). Its Markdown page embeds it once (`ownership-end-product.md` 8).
  **Keep** (plan 4.4 point 2: diagrams we drew go).
- **Named, not included:** 166 picture file references in 43 files (128 `.png`, 35 `.jpg`, 3 `.bmp`; 7 of them paths
  into `evidence/` folders) and 66 save file references (`.Civ5Save`) in 24 files (1 into an `evidence/` folder); the
  search's other 17 "save" hits are words such as "save/load". None of the files named is in the notes group:
  pictures live in the Visual Checks store and in `evidence/` folders, saves in the instances and the pins folders,
  all private. **Keep the names as text.** The 8 paths into `evidence/` will point at nothing in the export;
  `disable-systems-and-masks.md` 692, `asset-pipeline-verification.md` 250, `insertion-point-and-frame-structure.md`
  231, `shell-foundations-verification.md` 201, `terrain-bake-comparison.md` 279 (a work-drive path),
  `terrain-surface.md` 210, `run-and-drive-the-game.md` 1820 and `upstream-fix-batch-1.md` 364 (a save). Proposed:
  leave them; the notes' README (E2, plan 4.4 point 4) can say once that `evidence/` paths name private records.
  Per-reference list: `tools/export/evidence/4717/pictures-classified.tsv`.

## 7. Not verified

- The 75 files without a flagged passage were not read line by line (2.4).
- Line numbers are at `main` 02605bab9. A slice that edits these files later re-finds each passage by its content;
  files touched by tickets after 02605bab9 may have moved. (`addresses.md`'s four passages re-found at main 0b11dd083:
  section 8.3.)
- Whether the MC reads D83 as allowing EXE addresses with our descriptions in a public export (K1, K2) was assumed,
  from D83's wording and the plan's 4.4 point 1 ("our own descriptions of behaviour ... go").
- The product group was not audited (2.4).

## 8. The delta at main 0b11dd083 (ticket 0768, 2026-11-25) [C, reading of `main` 0b11dd083]

Ticket 0768 (epic 0468, package E2, split from 4703 by lead-0763), 2026-11-25 15:48-16:15 EST, account A. **Read-only:
no notes file was changed.** It audits what was added to or changed in the notes group since sections 1-7 (main
02605bab9), with section 2's method and classes unchanged, and lists the replace passages the next E2 edit slices take.
Line numbers in this section are at main 0b11dd083.

### 8.1 Result

- **Scope:** the notes group at 0b11dd083 (`check.py --rev 0b11dd083 --groups notes --list --no-terms
  --no-env-terms`): 210 files, 209 Markdown files and the same SVG (02: 26, 03: 38, 04: 9, 05: 65, 06: 33, 07: 8,
  08: 30, `research/README.md`). List: `tools/export/evidence/0768/notes-files.tsv`.
- **17 passages to replace, in 10 files, 53 lines, 6,055 B.** 4 of them are this file's `addresses.md` passages that
  waited for epic 0437 (4718 did the other 22 of section 4.1 on 2026-11-18, `upstream-export.md` 12); 13 are new, in 9
  files. By section: 02 nine passages in 3 files, 03 one, 05 six in 5 files, 08 one; none in 04, 06, 07 or a README.
  By kind: byte signatures or patterns of EXE code 7 (two of them with an instruction sequence), instruction sequences
  from a disassembly 4, import lists 3, address lists from a scan 2 (callers of one function; functions from an operand
  scan), a create-site table 1 (E5, E6, as `unit-draw-filter.md`'s). Every one is a **replace**: the text beside each
  says, or can say in one line, what the passage shows. Table: 8.3.
- **Judgement classes for the owner's read (J1, J2; the MC's ruling of 2026-11-18 keeps both for now, `upstream-export.md`
  11):** at 0b11dd083, **J1 28** flagged or hand-found places in 16 files (23 carried from sections 5.1, 5 new) and **J2
  47** in 24 files (39 carried from 5.2, 8 new). The new places are in 8.4. No new J3, J4 or J6 place.
- **Nothing else to change:** of the 186 new or changed hits, 14 lie in 8.3's passages and 7 are J2 places (8.4); the
  other 165 are our descriptions (K1 100, K2 4, K4 6, K5 10), our own code (K3 9: the proxy's farm-guard stubs, the
  4725 spike's own TU, the x64 DLL's crash instruction) or false hits (K0 36). No picture of the game, no save and no embedded image
  in the group (three prose mentions of `<img` / `data:image`; the one SVG as in section 6).

### 8.2 Method [C]

Scripts in `tools/export/evidence/0768/` (Python 3.11, read-only on the worktree; outputs carry file names, line ranges,
kinds and classes, never note text):

1. `delta.py` runs 4717's `scan.py` and `linescan.py` (unchanged, from their folder) over three states: the base
   (02605bab9, 4717's 147 files), 4718's slice at its last commit 6a1b09810 (its 14 files) and the head. It
   reproduces 4717's counts at the base (273 blocks, 100 lines). Each base hit takes 4717's reading (its override or
   default), each 4718 hit 4718's (`hits-after.tsv`). A head block is **carried** when a hit with the same text (lines
   stripped, SHA-1) exists in either; a head line when one with the same text and kinds does. Head: **389 blocks (150
   new or moved) and 109 lines (36 new)**, 498 units, 186 new. Outputs `delta-hits.tsv`, `delta-lines.tsv`. About
   3.5 min on the workstation.
2. `show.py` prints, per new block, the lines not present in the same file at the base or in 4718's slice (for an
   added file, all), optionally only those with an extract's shape (the line kinds, a `.shader` blob VA, 8 or more
   addresses, a short quoted byte pattern). Every new block was read at its new lines with an extract's shape and at
   its first lines; every passage in 8.3 and 8.4 in full. `mnem.py` lists the instruction-shape matches of the blocks
   that default to J2.
3. `classify0768.py` records the reading as 4717's `classify.py` does (an override per passage, 4717's defaults
   otherwise, the half-overlap rule) and refuses to finish while a new block or line with decompiler, disassembly,
   byte or import hits has no override (0 left). `refind.py` re-finds `addresses.md`'s four passages by content
   (`addresses-waiting.tsv`). Outputs: `classified.tsv`, `passages.tsv`, `per-section.tsv`, `jclasses.tsv`,
   `classify-output.txt`.

Limits beyond 2.4: a block whose text changed is read again whole if flagged, though only its new lines can hold a new
extract; a carried hit keeps 4717's or 4718's class unread. scan.py's instruction shape counts `and r3` (a run name
after "and") as an instruction: 14 of the 23 new blocks that default to J2 are such false hits (K0 in `classified.tsv`).
Several new notes name a work-drive or scratch path (`D:/c5-64bit-work/...`, `F:/c5-64bit-work/...`): not a D83 point,
the export's path check's (`upstream-export.md` 2).

### 8.3 Replace passages, per section

File (relative to its section), lines at 0b11dd083, kind, class, size. The proposed replacement for each, in our own
words, is in `tools/export/evidence/0768/passages.tsv` (the `note` column: what the passage is, then "Proposed:"; it
does not copy the passage). Edit tickets: 0772 (16 passages, 9 files) and 0769 (`combat-choreography.md`, held by
epic 0042), lead-0763 on the 0768 thread.

| Section | File: lines | Kind | Class | Size |
|---|---|---|---|---|
| 02 | `addresses.md`: 85-88 | import list (4717's 79-84) | E4 | 4 L, 405 B |
| 02 | `addresses.md`: 150 | byte signature (4717's 135) | E1 | 1 L, 135 B |
| 02 | `addresses.md`: 152 | byte signature (4717's 137) | E1 | 1 L, 128 B |
| 02 | `addresses.md`: 275 | byte signature (4717's 254) | E1 | 1 L, 133 B |
| 02 | `exe-build-table.md`: 753-757 | code bytes of two builds and an instruction sequence | E1,E2 | 5 L, 594 B |
| 02 | `exe-build-table.md`: 761 | byte pattern | E1 | 1 L, 105 B |
| 02 | `exe-build-table.md`: 767 | two byte patterns | E1 | 1 L, 120 B |
| 02 | `new-exe-build-26577625.md`: 78-81 | import list (libraries and counts) | E4 | 4 L, 431 B |
| 02 | `new-exe-build-26577625.md`: 296 | import list (dropped imports by name) | E4 | 1 L, 629 B |
| 03 | `proxy-design.md`: 4821-4828 | disassembly (three sequences) | E2 | 8 L, 808 B |
| 05 | `city-renderer.md`: 485-496 | create-site table (10 rows) | E5,E6 | 12 L, 921 B |
| 05 | `combat-choreography.md`: 299-300 | callers of one function by address | E5 | 2 L, 232 B |
| 05 | `live-terrain.md`: 4770 | byte pattern | E1 | 1 L, 121 B |
| 05 | `unit-combat-server.md`: 1460-1462 | two byte patterns | E1 | 3 L, 358 B |
| 05 | `unit-combat-server.md`: 4270-4271 | byte pattern and its instructions | E1,E2 | 2 L, 234 B |
| 05 | `unit-server-scene.md`: 3270-3272 | functions from an operand scan | E5 | 3 L, 357 B |
| 08 | `turn-time-study.md`: 2061-2063 | disassembly (call sequence) | E2 | 3 L, 344 B |

**Files per edit slice:** 02: `addresses.md`, `exe-build-table.md`, `new-exe-build-26577625.md`; 03: `proxy-design.md`;
05: `city-renderer.md`, `combat-choreography.md`, `live-terrain.md`, `unit-combat-server.md`, `unit-server-scene.md`;
06: none; 07: none; 08: `turn-time-study.md`. As section 3: the byte patterns are the product's checks and stay in
the product (the note names the row or file); instruction sequences become the behaviour in words; import lists one line
with the slots the product uses kept (K5); scan lists their count.

### 8.4 New J1 and J2 places (for the owner's read; proposed keep, as 5.1 and 5.2)

- **J1, 5 new (the EXE's own strings used as names):** `02-exe-internals/exe-audio.md` 74-76 (the Sound trigger's two
  description strings) and 709-711 (a reader's description string and five attribute names);
  `05-graphics-composition/live-terrain.md` 427 (the EXE's Lua globals, about a dozen of 68 names);
  `unit-combat-server.md` 9951-9952 (three era names); `unit-controller.md` 1811-1812 (one class-name string).
- **J2, 8 new (single instructions naming a site):** `02-exe-internals/addresses.md` 267 and
  `08-performance/d165-pacing-fix-design.md` 78-82 (the frame's 2-byte indirect call); `02-exe-internals/exe-build-table.md`
  1010-1021 (derivations by one instruction or opcode each; line 1019 also flagged alone);
  `05-graphics-composition/city-renderer.md` 2565-2569 (three texture-handle reads); `08-performance/turn-time-study.md`
  2856-2864 (one byte compare); and two of 5.2's places re-flagged after an unrelated edit,
  `disable-systems-and-masks.md` 34-59 and `world-systems-and-stubs.md` 364-407.
- Full lists with line ranges (carried and new): `tools/export/evidence/0768/jclasses.tsv`.

## 9. History

- 2026-11-18: written by ticket 4717 (account B), read-only audit; the dated line in `upstream-export.md` is the
  MC's at the merge (ticket 4716 holds that file).
- 2026-11-25: section 8 added by ticket 0768 (account A), read-only delta audit at main 0b11dd083; `upstream-export.md`
  is not in its touches (its dated line is the lead's or the MC's at the merge).
