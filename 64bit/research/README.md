# Research knowledge base

Everything the project knows about Civilization V's EXE, the game DLL, their boundary, the memory
problem, the graphics composition and the tooling, compiled so that no agent repeats work another
agent has done. Read the rules in `AGENTS.md` section 1 before adding anything.

Compiled 2026-11-10 from the September-November 2026 investigations (memory investigation,
thin-client study, DisableSystems probes, Gates 1 and 3, stub sizing, load-time measurement). The
originals are under `sources/` and are never edited.

## For readers of the exported notes

If you are reading this folder as part of the published `64bit` source, these points explain what you find in it.

- **What it is.** The working notes of the people and agents who built the 64-bit patch: how the 32-bit EXE and the
  game DLL behave as far as the patch needs them (described in our own words, for interoperability), the designs of
  the 64-bit server, proxy and renderer, the measurements behind them, and the negative results. Nothing in it is
  copied from the EXE or the game's files; the patch reads shaders and assets from the player's own install at run
  time.
- **Confidence marks.** `[C]` confirmed by a decompile or a dump, `[L]` verified live in the game, `[I]` inferred.
  Measurements name their date, save, DLL build and configuration.
- **Numbers.** A four-digit number (for example 0437, 4703 or 9081) is a ticket of this project's own tracker; an
  "epic" is a ticket that groups others. `D` followed by a number (D83, D228) is a project decision: every one is
  resolved in one line by [`07-architecture-decisions/decision-digest.md`](07-architecture-decisions/decision-digest.md).
  `Q` numbers are the open questions of `07-architecture-decisions/open-questions.md`. "The owner" is the person
  who makes the project's decisions; the MC (master coordinator), the Architect, account A and account B are roles
  of the agents that did the work.
- **What is not exported.** The `evidence/` folders (raw logs, captures, run records), the full decision log, the
  Architect's proposals (`07-architecture-decisions/architect-*.md`), `sources/`, the tracker and the project's
  operating documents stay private. A note may still name such a file; the link then has no target in the export,
  and the digest or the note itself carries what the reader needs.
- **Addresses** are virtual addresses at ImageBase `0x400000` of the DX11 EXE unless a note says otherwise.
- **"[reworded for privacy]"** marks a place where the owner's own words were quoted. The published notes give a
  paraphrase of what the owner meant instead of their words; the original stays private.

## How to use it

1. Search this directory for the address, function, file, symptom or number you are about to
   investigate. `02-exe-internals/addresses.md` is the single table of EXE addresses.
2. Read the section README for your area, then the file. Each file ends with a Sources line.
3. Confidence marks: `[C]` confirmed by decompile or dump, `[L]` verified live in the game, `[I]`
   inferred. Measurements name the save, DLL build and configuration they were taken with.
4. When you learn something, update the file on that topic in the same session and add it to the
   section README if it is a new file. Correct old statements in place with a dated note.

## Sections

| Section | What is in it | Start with |
|---|---|---|
| [`02-exe-internals/`](02-exe-internals/README.md) | The inside of `CivilizationV_DX11.exe`: the 24 `[DisableSystems]` keys and the two masks; the five world systems that crash when disabled, their causes, stubs and live results; camera and picking; the engine allocator and the large blocks; the 4 MB queue and fixed buffers; the measured load sequence; the address table | `world-systems-and-stubs.md`, `disable-systems-and-masks.md`, `load-sequence.md` |
| [`03-dll-engine-boundary/`](03-dll-engine-boundary/README.md) | The EXE<->DLL interface, its measured traffic per frame and per AI turn, IPC design and latencies, the x64 DLL port (first x64 compile and link 2026-11-10: 336 closed-lib symbols, 248 real; closed libs, `size_t`, floating point: the 32-bit DLL is SSE, not x87, so only C runtime maths and squad order can drift, corrected 2026-11-10; STL drift audit), the headless host, effort estimates by date | `interfaces-and-traffic.md`, `x64-dll-port.md`, `ipc-design-and-measurements.md` |
| [`04-memory-investigation/`](04-memory-investigation/README.md) | Why the 32-bit game dies at 4,025 MB: ownership and ceiling, the leak that was fixed and the growth that remains, the myths that were measured away, the instruments, what was ruled out | the README's "what every agent must know", then `ownership-and-ceiling.md` |
| [`05-graphics-composition/`](05-graphics-composition/README.md) | How the 64-bit server draws into the shell's frame: shared texture and compositor, the frame structure and insertion point, hooks, overlays and the CEG constraint | `shared-texture-and-compositor.md`, `insertion-point-and-frame-structure.md` |
| [`06-tooling-and-workflows/`](06-tooling-and-workflows/README.md) | Safety rules; building and installing the DLL; running and driving the game without taking focus; headless and parallel testing (Goldberg); reverse-engineering setup (Ghidra, RenderDoc, cdb); the DLL-side instruments and their opt-in markers | the README's safety box, then `run-and-drive-the-game.md` |
| [`07-architecture-decisions/`](07-architecture-decisions/README.md) | The decision digest (every decision D1 onwards in one line, the public form of the private decision log; added 2026-11-25, ticket 4703), the decision log (D1-D19 at 2026-11-10; D15 x64 toolchain, D18 sorted save containers, D19 server straight to x64), the comparison with the pure 64-bit port, the ranked open questions Q1-Q14 (Q14: STL change and gameplay drift, audited by epic 1600) | `decision-log.md`, `open-questions.md` |
| [`08-performance/`](08-performance/README.md) | Performance numbers and how far each can be trusted: the inventory of every measured turn time, load time, IPC latency, crossing count, frame rate and memory figure (2026-11-11), contradictions, gaps; the performance standards team's later benchmarks | `inventory.md` |

The architecture itself, the increments and the work packages are specified in
`../docs/MASTER-COORDINATOR-HANDOFF.md`.

## Sources (verbatim, do not edit)

| Path | What | Date |
|---|---|---|
| `sources/exe-world-subsystems/PROBES.md` | DisableSystems probes, all 24 keys and combined configs, huge save | 2026-11-09 |
| `sources/exe-world-subsystems/*.py`, `allocsites.txt`, `imports.txt` | Static-analysis scripts and outputs over the DX11 EXE | 2026-11-09 |
| `sources/shell-gates/GATES.md` | Gate 1 and Gate 3 live validation, verdicts, alloc trace, camera | 2026-11-09 |
| `sources/shell-gates/STUBS.md`, `stubs/*.md` | Stub sizing for Terrain, Unit, LandmarkSystem, River, Arrows; coupling | 2026-11-10 |
| `sources/shell-gates/GATE2.md` | Gate 2: asset reuse (textures shareable, shaders in the EXE's `.shader` section, meshes not; terrain has no vertex buffer) | 2026-11-10 |
| `sources/shell-gates/LOADTIME.md`, `loadtime/` | Load-time attribution on the huge save: static phase plus live cdb sampling (six launches 2026-11-10 01:44-02:07); the ~20 s gap is Landmark's farm-field layout; ValidateGameDatabase cost; tools and live data. Final, commit c60138b82 on `claude/shell-gates-d8e422` | 2026-11-10 |
| `sources/shell-gates/CAMERA.md` | Camera ownership static analysis | 2026-11-09 |
| `sources/shell-gates/TOOLING.md` | Ghidra, ghidra-mcp, RenderDoc, Sysinternals, MinHook install record | 2026-11-09 |
| `sources/shell-gates/compositor-README.md`, `ab_table.md`, `g3_ab.txt` | Compositor prototype and its measurements | 2026-11-09 |
| `sources/shell-gates/nofocus-README.md`, `tools/civ_bg.py` | No-focus game driving | 2026-11-09 |
| `sources/shell-gates/alloc-trace-README.md` | EngineAllocTrace instrument | 2026-11-09 |
| `sources/shell-gates/tools/*.py` | view_mask, disable_systems, addrspace, gate1_checklist, end_turn, wait_turn, scan_exec, ab_live | 2026-11-09/10 |
| `sources/shell-gates/BRANCH-COMMITS.txt` | Commit list of `claude/shell-gates-d8e422` in the DLL fork | 2026-11-10 |
| `sources/boundary-benchmark/REPORT.md`, `RESULTS.md`, `results_table.md`, `results.json`, `projection.txt` | Live EXE<->DLL traffic benchmark and IPC micro-benchmarks | 2026-11-09 |
| `sources/thin-client-study.html` | The published feasibility study page | 2026-11-09 |
| `sources/REVIEW.md`, `shell-architecture-review.html` | The architecture review and its published page | 2026-11-10 |
| `sources/memory-notes/*.md` | The research sessions' memory notes (investigation log, leak, Lua profiling, UMDH, instruments, skills, save rules, studies) | 2026-09-03 to 2026-11-10 |
| `sources/owner-words-reworded-2026-11-26.md` | The owner's verbatim words that the exported notes and comments quoted, each with the "[reworded for privacy]" text that replaced it (77 places; private, never exported) | 2026-11-26 |
| `sources/skills/*.md` | The `vp-dll-dev` and `civ5-game-ui` skill files | 2026-11-09 |
| `sources/probe-logs/*/` | The game's stopwatch, system_init and net_message_debug logs from the A5 and B4 probe runs (load timeline evidence) | 2026-11-09 |
| `sources/civ5-headless/` | The civ5-headless host's docs (engine contract, profiles, why 32-bit, running, saves, verification, plan, ledger) | 2026-11-10 copy |
| `sources/pure_vp-listing.txt` | Listing of the Goldberg game install on the Desktop | 2026-11-10 |

## Pending at compile time

- `GATE2.md` landed and is folded into D13 and Q5; `05-graphics-composition/` should get a short
  asset-pipeline file from it (not yet written). `LOADTIME.md` (static-phase draft) was copied into
  `sources/shell-gates/` on 2026-11-10 and replaced by the final version (live phase included) on
  2026-11-10; tracker issue 9003 folded it into `02-exe-internals/load-sequence.md`, `addresses.md` and
  `07-architecture-decisions/open-questions.md` (Q7) (commit ef49ef0, 2026-11-10). *Corrected 2026-11-10 by the
  librarian; this line said the fold was still to do.*
- Known contradictions between sources are listed in each section README; the one with the
  largest stake is Q13 (who owns the 118.75/59.38 MB heap pair).
