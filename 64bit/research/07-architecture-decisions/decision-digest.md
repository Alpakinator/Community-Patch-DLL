# Decision digest

Every decision of the project, D1 to D261 (2026-11-10 to 2026-11-25), one line each: its number, its date, what was
decided in our own words, and its status. This is the public form of the decision log. The full log
(`decision-log.md`, with each decision's wording, rationale and evidence) stays private and is not exported.

How to read it:

- **D-numbers.** `D` followed by a number is a project decision, numbered in the order it was made. The notes in
  this folder cite decisions by number; this file resolves each one. A decision is made by the owner (the person
  who decides for the project) or, within limits the owner set, by the MC (the master coordinator, the agent that
  runs the work).
- **Ticket numbers.** A four-digit number (for example 0437, 4703, 9081) is a ticket of the project's own tracker;
  an epic is a ticket that groups others, a one-off a small separate task. The tracker is private.
- **Roles.** The MC, the Architect (an advising agent), account A and account B (the two accounts the agents run
  on), the worker-account MC, the dispatcher, the Jira Demon (the tracker's housekeeping agent), the performance
  team and the effectiveness auditor are roles of the agents that did the work, not people.
- **Words.** The EXE is the 32-bit game executable, kept as a shell; the DLL is the game DLL
  (`CvGameCore_Expansion2.dll`); the server is the patch's 64-bit process; the proxy is the patch's DLL inside the
  EXE that forwards to the server. Increments 0 to 4 are the project's stages; VP is Vox Populi.
- **Status.** `in force`; `done` (a one-time action carried out); `replaced by`, `amended by`, `narrowed by`,
  `suspended by` or `cancelled by` a later decision, which is named; `dropped`. A later amendment that changes only
  part of a decision leaves the rest in force.
- Dates are the dates the decisions were made (US Eastern time).
- **Internal operations.** A decision about the project's own operations (which machines are commissioned or used
  for what, agent and usage budgets, which account's places run which work, and the management hierarchy and its
  roles) is shown as &lt;internal operations decision&gt;, with its number, date and status kept so that other
  decisions and the notes can still cite it.

| # | Date | Decision | Status |
|---|---|---|---|
| D1 | 2026-11-10 | The original 32-bit EXE stays as a shell, and a new 64-bit server owns game state, AI and the world renderer; it is neither a pure port nor only a thin client. | in force |
| D2 | 2026-11-10 | Work proceeds in increments 0, 1, 2, 3, where increment 2 puts terrain-side drawing in the server while units and cities stay in the shell. | in force; amended by D36 |
| D3 | 2026-11-09 | Communication between the processes uses a shared-memory section with spin-then-block waiting and batched messages, with no kernel events on the hot path. | in force |
| D4 | 2026-11-09 | The EXE's code bytes are never patched; only data writes, import slots, vtables and calls into the EXE's own functions are used. | in force; amended by D228 |
| D5 | 2026-11-10 | The world is composed through a shared D3D11 texture drawn at a tagged point in the command list with a depth hand-off, using inline hooks or drawing from the EXE's render path. | in force |
| D6 | 2026-11-09 | The server is built from the design of the headless game host, and the x64 DLL is produced by replacing the closed-library symbols the DLL needs. | in force |
| D7 | 2026-11-10 | Terrain in increment 2 first keeps the EXE's terrain system running without drawing it; skipping the EXE's bake and having the server supply the heights comes later as its own package. | in force |
| D8 | 2026-11-10 | This repository holds the server, proxy, compositor, tooling, tracker and research, while DLL changes live on a branch of the Community Patch DLL fork and are pinned from here. | in force |
| D9 | 2026-11-10 | DLL logic is tested in the headless host, EXE-touching tests run in the real game through the no-focus launcher, and parallel instances use the Goldberg install, never Steam. | in force |
| D10 | 2026-11-10 | Save compatibility is a hard gate: the x64 and x86 DLLs must write byte-identical saves for the same state, and the EXE's save container stays untouched. | in force; amended by D18, D24 |
| D11 | 2026-11-10 | &lt;internal operations decision&gt; | replaced by D76 |
| D12 | 2026-11-10 | &lt;internal operations decision&gt; | in force; amended by D14, D39, D68 |
| D13 | 2026-11-10 | The server parses static assets from the game files itself and takes from the EXE only what the EXE computes, and the terrain renderer is rebuilt as a vertex-ID grid over a height texture. | in force |
| D14 | 2026-11-10 | &lt;internal operations decision&gt; | in force; amended by D39 |
| D15 | 2026-11-10 | The x64 DLL and the server are built with clang-cl x64 against the MSVC 14.4x STL, sharing one CRT heap, while the 32-bit proxy keeps the older VC9 ABI. | in force |
| D16 | 2026-11-10 | The Goldberg Steam emulator may be built from its upstream source and modified for parallel testing, used only in test-instance copies and never in the Steam install or the shipped patch. | in force |
| D17 | 2026-11-10 | Instead of a slot-poll lock fix in increment 0, epic 1300 measures how long the UI thread waits on the game-core lock and recommends whether any change is needed. | done |
| D18 | 2026-11-10 | The three save containers written in hash-table order are written in sorted key order on both x86 and x64 builds, so byte identity holds between builds carrying the change. | in force |
| D19 | 2026-11-10 | The server goes straight to x64 with a host that loads the x64 DLL and runs a fixed-seed headless game, dropping the intermediate 32-bit server step. | in force |
| D20 | 2026-11-10 | No lock change is made in increment 0, because measurement shows the UI thread barely waits on the game-core lock and low frame rates come from leader screens. | in force |
| D21 | 2026-11-10 | The proxy keeps UI and gameplay Lua in the EXE's VM, crosses calls through a generic value codec, keeps void calls synchronous for now, and encodes handle kind and owner. | in force; amended by D30 |
| D22 | 2026-11-10 | The repository is licensed GPL-3.0-or-later instead of MIT, so code from the headless host and the DLL may be copied with its origin named. | in force |
| D23 | 2026-11-10 | The two stock crash fixes, a divide by zero in the religion AI after a load and a counterspy log precondition, ship in the increment 0 DLL and merge into the x64 branch. | in force |
| D24 | 2026-11-10 | The save gate is tested in three required parts: identical writes after loading the same save, identical behaviour over the same game, and each DLL loading the other's saves. | in force |
| D25 | 2026-11-10 | Three determinism fixes that change 32-bit behaviour go into both the increment 0 DLL and the x64 branch: a comparison operator fix, unit-ID order for squad moves, and sorted game-info types. | in force |
| D26 | 2026-11-10 | Nothing from this project is installed into the owner's real game install until the project is complete; all testing stays on test instances and headless hosts. | in force |
| D27 | 2026-11-10 | The patch ships with an installer in the style of Vox Populi's, and everything it ships and every install step is confirmed with the owner before the installer epic begins. | in force; amended by D281 |
| D28 | 2026-11-10 | The installer is an add-on to a matching Vox Populi install, links to the Visual C++ x64 runtime instead of bundling it, and keeps the 32-bit DLL as a fallback with an off switch. | in force |
| D29 | 2026-11-10 | A dialog in a test instance is dismissed only through a harness command that checks that saves have settled and records the dialog, the button and the outcome. | in force |
| D30 | 2026-11-10 | Revises D21 item 5: the proxy mirror becomes an epoch mirror that answers a read locally only while no impure crossing has happened or is in flight. | in force |
| D31 | 2026-11-10 | Corrects D2's rationale: increment 2 does not avoid Granny model work, so a model-file spike moves forward and a landmarks package is added with two candidate designs. | narrowed by D33 |
| D32 | 2026-11-10 | The 32-bit ship branch also carries the sorted writes of D18, so both DLLs write the three hash-ordered containers identically and the save gate needs no exception. | in force |
| D33 | 2026-11-10 | Increment 2 involves no model work: the server draws terrain, water, rivers, borders and overlays, while landmark models, roads, bridges and farm fields stay in the EXE until increment 3. | in force |
| D34 | 2026-11-10 | Source code of open-source Granny model readers may be cloned, built from source and run, with no prebuilt binaries and never a Granny runtime library. | in force |
| D35 | 2026-11-10 | Both DLL ship branches turn off synchronous writes and use persistent journaling on the statistics database connection, with no change to game state or save bytes. | in force |
| D36 | 2026-11-10 | Increment 2 building blocks that do not depend on open proxy work start now, and the MC schedules independent foundational work for later epics whenever resources are free. | in force; narrowed by D38 |
| D37 | 2026-11-10 | &lt;internal operations decision&gt; | narrowed by D133 |
| D38 | 2026-11-10 | &lt;internal operations decision&gt; | in force |
| D39 | 2026-11-10 | &lt;internal operations decision&gt; | amended by D61, D68 |
| D40 | 2026-11-10 | &lt;internal operations decision&gt; | in force |
| D41 | 2026-11-11 | &lt;internal operations decision&gt; | in force; amended by D64 |
| D42 | 2026-11-11 | &lt;internal operations decision&gt; | narrowed by D69 |
| D43 | 2026-11-11 | The tracker page is the owner's first stop and source of truth, is rebuilt on every issue change, and the MC may dispatch a one-off to improve its clarity unasked. | in force |
| D44 | 2026-11-11 | Useful fixes found in upstream's merged or open pull requests are taken into the project's DLL line on the MC's judgement, with low-severity ones picked up by the next generation of agents. | in force |
| D45 | 2026-11-11 | Every lasting instruction from the owner is saved to the MC's memory files and its instruction file so it survives compaction and hand-over. | in force |
| D46 | 2026-11-11 | The fork's master branch is the project's shipping DLL line, and upstream offers are limited to the counterspy crash fix, with growth commits offered later after a rebase and re-measurement. | in force |
| D47 | 2026-11-11 | Every message to the owner ends with a short summary of 25 to 50 words and then either states that no action is needed or names the decision required. | in force; amended by D52, D77 |
| D48 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D49 | 2026-11-11 | Test runs that do not need the DLL's AI logs run with AI logging off in both arms, and only the counting step of the proposed trade-path cache is approved. | in force |
| D50 | 2026-11-11 | &lt;internal operations decision&gt; | narrowed by D75 |
| D51 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D52 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D53 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D54 | 2026-11-11 | A session that has fulfilled its purpose says so and that it is safe to archive once the MC has its report, instead of the usual no-action line. | in force; amended by D90 |
| D55 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D56 | 2026-11-11 | An experiment on the test DLL branch gives the shell a white fog-mask texture so models keep their textures with the terrain systems off, and shipping it is left to the owner. | done |
| D57 | 2026-11-11 | The harness may load one allowlisted, hash-pinned frame-capture DLL into a test game at launch, on test instances only and off by default. | in force |
| D58 | 2026-11-11 | Epic 3200, the server drawing the terrain surface, takes colour from a tap of the EXE's baked tile caches if two premises hold and otherwise uses a plain shader. | done |
| D59 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D60 | 2026-11-11 | The harness's dismiss command may get a coordinator override of its settled-save wait for a crashed game whose crash box is open, as stop already has. | in force |
| D61 | 2026-11-11 | &lt;internal operations decision&gt; | replaced by D68 |
| D62 | 2026-11-11 | &lt;internal operations decision&gt; | done |
| D63 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D64 | 2026-11-11 | &lt;internal operations decision&gt; | in force |
| D65 | 2026-11-11 | The EXE's Steam start-up check is never patched or answered by anything of ours, including on test machines, where the owner installs Steam and signs in. | in force |
| D66 | 2026-11-11 | The rendering goal is a faithful pixel-for-pixel recreation of the stock picture, with look-alike substitutes treated as steps or fallbacks and not end states. | in force |
| D67 | 2026-11-11 | Epic 3200 is accepted without its colour criterion, which has no clean reference, and that criterion moves to issue 3019 and stays binding for increment 2. | in force |
| D68 | 2026-11-12 | &lt;internal operations decision&gt; | in force; amended by D140 |
| D69 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D70 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D71 | 2026-11-12 | &lt;internal operations decision&gt; | in force; amended by D140 |
| D72 | 2026-11-12 | &lt;internal operations decision&gt; | replaced by D75 |
| D73 | 2026-11-12 | A pre-tool hook for agent calls is added to the project's settings file, together with the merge of the one-off that provides its script. | done |
| D74 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D75 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D76 | 2026-11-12 | &lt;internal operations decision&gt; | in force; amended by D204 |
| D77 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D78 | 2026-11-12 | Increment 1's memory acceptance compares EXE private bytes and largest free block per turn against stock over a long stretch of the huge save, using the proxy route with the 0102 fix. | in force; amended 2026-11-13 |
| D79 | 2026-11-12 | Epic 3200 is merged with the colour criterion carried in ticket 3019 and the geometry criterion carried in ticket 3020, neither waived. | done |
| D80 | 2026-11-12 | The server re-implements as much of the world renderer as possible, including bake, lockstep and depth; only the DLL and the world renderer run there, and everything else stays in the EXE for legal interoperability. | in force |
| D81 | 2026-11-12 | Increment 2 moves the renderer into `server/` with an event feed, lockstep first and depth as a rider, and ports the bake in two stages, testing at product level against stock plus a list of intended differences. | in force |
| D82 | 2026-11-12 | Bake stage 1, the GPU colour stage fed by layer records read from the EXE, becomes epic block 3400 beside 3300, starting with ticket 3023 and a usable-records checkpoint. | done |
| D83 | 2026-11-12 | Nothing taken from the EXE or the game's files is committed or shipped, and re-implemented EXE algorithms are written from the knowledge base's description of behaviour with no decompiler output committed. | in force |
| D84 | 2026-11-12 | &lt;internal operations decision&gt; | in force |
| D85 | 2026-11-12 | The ported renderer reuses as much of the original engine's functionality and data as possible, within the legal working rules of D83. | in force |
| D86 | 2026-11-12 | Gray tiles after a pan or camera jump are a bug to fix; if the fix needs much memory, the owner chooses between keeping all tiles ready and keeping a few rings around the view. | in force; settled by D88 |
| D87 | 2026-11-13 | The proxy fixes of ticket 9036 are merged without a pin change; the pin moves once, with the fix for the remaining growth in ticket 9041, and short proxy-route runs are allowed meanwhile. | done |
| D88 | 2026-11-13 | Every land tile's baked terrain stays resident, about 413 MB on the huge map, and a user option sets the baked-tile camera radius, with -1 meaning the whole map as the default. | in force |
| D89 | 2026-11-13 | The 64-bit patch has one global options file in plain INI form that a text editor can change, with the bake radius as its first option. | in force |
| D90 | 2026-11-13 | &lt;internal operations decision&gt; | in force |
| D91 | 2026-11-13 | Tickets 0116 and 0104 trace to checkpoint Lua in the 32-bit test host; both DLL fixes go into the next pin batch with 0084, 0096 and 0103 even though some outcomes change. | done |
| D92 | 2026-11-13 | Epic 3400 is merged with exact layer lists, roads re-baked in under 0.6 s, no gray tiles after bake-ahead, and colour reported near the EXE's own ceiling. | done |
| D93 | 2026-11-13 | The EXE's Overlay system stays on, because with it off the highlight style map stays empty and the EXE faults; this was applied by the epic 3300 team. | in force |
| D94 | 2026-11-13 | Colour comparisons name their asset set, including texture-pack presence, install and machine, and compare only like with like. | in force |
| D95 | 2026-11-13 | Anything submitted upstream carries no internal bug or decision ids, project names or cherry-pick provenance. | in force; amended by D281 |
| D96 | 2026-11-13 | DLL pin batch 2 is pinned on both the 32-bit-line and 64-bit-line branches, covering tickets 0084, 0096, 0103 and 0116/0104, with 17 of 17 test games identical between 64-bit and 32-bit. | done |
| D97 | 2026-11-13 | &lt;internal operations decision&gt; | in force; amended by D103, D113, D152, D182, D244 |
| D98 | 2026-11-13 | &lt;internal operations decision&gt; | in force |
| D99 | 2026-11-13 | Epic 3300 is merged: the renderer runs in the server with a ring and fence, lockstep and depth, and the proxy shell in product form, with follow-ups 0137 and 0134 for two accepted gaps. | done |
| D100 | 2026-11-13 | &lt;internal operations decision&gt; | done |
| D101 | 2026-11-13 | &lt;internal operations decision&gt; | in force; suspended until 2026-11-26 by D251 |
| D102 | 2026-11-13 | The 1-10 priority scale from the queue-reexamination note is the project's priority scale. | in force |
| D103 | 2026-11-13 | &lt;internal operations decision&gt; | replaced by D113 |
| D104 | 2026-11-13 | Epic 3500 builds a new Vox Populi release with one command, and the DLL-side 64-bit changes go upstream eventually with 32-bit use unharmed; six decisions of the draft remain the owner's. | in force |
| D105 | 2026-11-13 | &lt;internal operations decision&gt; | in force |
| D106 | 2026-11-13 | &lt;internal operations decision&gt; | in force; places amended by D226 |
| D107 | 2026-11-13 | The 64-bit DLL gets a third build configuration without whole-program optimisation for functional work, only one whole-program link runs at a time on the workstation, and ThinLTO is investigated for a go or no-go. | in force |
| D108 | 2026-11-13 | &lt;internal operations decision&gt; | in force |
| D109 | 2026-11-13 | &lt;internal operations decision&gt; | in force |
| D110 | 2026-11-13 | &lt;internal operations decision&gt; | narrowed by D133 |
| D111 | 2026-11-13 | &lt;internal operations decision&gt; | narrowed by D133 |
| D112 | 2026-11-14 | Pictures and comparisons shown for a release's acceptance must look as the player sees them in the full shipped system; anything else is labelled diagnostic. | in force |
| D113 | 2026-11-14 | &lt;internal operations decision&gt; | replaced by D152 |
| D114 | 2026-11-14 | Popups and windows the EXE draws over the map are closed before any picture or comparison of world-renderer work, while the always-on interface stays. | in force |
| D115 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D116 | 2026-11-14 | For fallback crash 3516, the config switches of D33 move out of config.ini and the proxy writes them at run time on the 64-bit path only. | replaced by D119 |
| D117 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D118 | 2026-11-14 | There is no degraded fallback: when the 64-bit path cannot run, the player gets an error popup and a diagnostic report instead of a broken game. | in force; amended by D240 |
| D119 | 2026-11-14 | Replaces D116: the D33 switches stay in config.ini in the reduced form, and any 64-bit failure writes a local report, shows a popup, ends the game and restores config.ini. | in force; amended by D240 |
| D120 | 2026-11-14 | Beta error reports are a local file the tester sends in, with no automatic upload and no network code in this release. | in force; amended by D122 |
| D121 | 2026-11-14 | ThinLTO is a no-go: the 64-bit DLL's measured and shipped builds stay on full whole-program optimisation, while the fast configuration and one link at a time cover link memory. | in force |
| D122 | 2026-11-14 | Beta testers hand in error reports as an issue on the patch's code-hosting repository, with the report file attached. | in force |
| D123 | 2026-11-14 | The beta does not ship with a gap where a start without our proxy shows a broken world; the gap must be fixed first. | replaced by D125 |
| D124 | 2026-11-14 | For ticket 3516, design V (the EXE always boots stock and the proxy clears the view bits on the 64-bit path) is tried first, and its memory cost is brought to the owner. | cancelled by D125 |
| D125 | 2026-11-14 | Design V is not taken: memory stays saved, failures stay loud, and a start without the proxy shows a red map that testers report by hand. Replaces D123. | in force |
| D126 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D127 | 2026-11-14 | Release builds whose whole-game 64-bit versus 32-bit comparison fails are never handed to testers and are labelled as failed candidates for investigation only. | in force |
| D128 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D129 | 2026-11-14 | The canary build of upstream's latest code is the MC's weekly routine, run by hand, with no scheduled task installed. | suspended by D222 |
| D130 | 2026-11-14 | The upstream drafts and the 64-bit CI check request wait until after the beta, and nothing is sent upstream before then. | in force; narrowed by D257 |
| D131 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D132 | 2026-11-14 | The Architect's hand-over rules R1 to R6 are adopted: UI state never reaches game rules, no EXE call by address or code byte (then), UI Lua runs unmodified, and hand-over faults are logged loudly. | in force; amended by D136 |
| D133 | 2026-11-14 | &lt;internal operations decision&gt; | in force; amended by D141 |
| D134 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D135 | 2026-11-14 | &lt;internal operations decision&gt; | replaced by D226 |
| D136 | 2026-11-14 | The shipped DLL and proxy may call the EXE's texture loader by address and write the white mask into the EXE's draw-object data, and the white fog mask becomes the product default. | in force |
| D137 | 2026-11-14 | Harnesses and stubs come first where they pay: renderer work iterates on recorded inputs without a client, acceptance stays live, and nothing captured is committed. | in force |
| D138 | 2026-11-14 | Pictures from the offline scene harness go, annotated and marked diagnostic, into the tracker's visual checks store, and undescribed ones fail. | in force |
| D139 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D140 | 2026-11-14 | &lt;internal operations decision&gt; | in force; amended by D153, D182, D201, D251 |
| D141 | 2026-11-14 | &lt;internal operations decision&gt; | in force |
| D142 | 2026-11-15 | &lt;internal operations decision&gt; | in force |
| D143 | 2026-11-15 | Increment 3 is not parked: foundational low-hanging work starts when places are free, while increment 2 keeps priority where they compete. | in force |
| D144 | 2026-11-15 | Increment 3 starts with the server drawing units while the EXE's unit simulation runs; true absence of the EXE's drawing is later and optional, and may use EXE calls and data writes as in D136. | in force |
| D145 | 2026-11-15 | Key metrics such as load, turn times and EXE memory are recorded per run in a central index and compared across increments and optimisations, under epic 0330. | in force; narrowed by D151 |
| D146 | 2026-11-15 | Epic 0334 builds the 64-bit 43-civilisation variant through the whole product and finds where the player limits live and whether they can be raised past 43. | in force |
| D147 | 2026-11-15 | The server-drawn fog is accepted as the product's fog look, the beta turns the EXE's combat, particle and projectile systems back on, and the long-term aim is to turn the EXE's unit system off to save memory. | in force |
| D148 | 2026-11-15 | Every game client the project starts skips the intro video and has tutorials and first-start popups off, using the game's own settings in the instance's copied user folder. | in force |
| D149 | 2026-11-15 | The first playable build is to be published as a repository pre-release after the owner confirms reading its instructions. | replaced by D150 |
| D150 | 2026-11-15 | Official GitHub releases are reserved for versions the owner has played at least once and approved; the first playable build is not published until then. | in force |
| D151 | 2026-11-15 | The metrics ledger becomes a release acceptance record of clearly labelled runs, each with the client version, memory growth over 10 turns and one screenshot per turn; narrows D145. | in force; amended by D157 |
| D152 | 2026-11-15 | &lt;internal operations decision&gt; | replaced by D182, then D244 |
| D153 | 2026-11-15 | &lt;internal operations decision&gt; | replaced by D182 |
| D154 | 2026-11-15 | After the first playable build, the owner approves a release only after playing the build and seeing its metrics-ledger acceptance runs; the first build is an alpha exception. | in force; amended by D221 |
| D155 | 2026-11-15 | No test game client may sit on a crash or assert dialog; a dialog watcher starts with every launch and notifies the owning session so it closes the box and fixes or reports the bug. | in force |
| D156 | 2026-11-16 | A human-input test harness (epic 0392) lets testers act only by mouse and keyboard, judge by picture and read-only Lua, and record replayable traces, with release journeys first and explorers second. | in force; amended by D159 |
| D157 | 2026-11-16 | The acceptance set's late-game stress row is a worst-case map that crashes the stock game, replacing the 43-civilisation Earth TSL save. | in force; amended by D160 and D161 |
| D158 | 2026-11-16 | Test game clients may run at 1920x1080 instead of 2560x1440 where that saves time, as long as comparisons use references of the same size. | in force |
| D159 | 2026-11-16 | Human-input coverage is measured against totals read from the game, a model-free sweeper and the release journeys run on every release candidate, and increment acceptance has an explicit gate. | in force; amended by D221 |
| D160 | 2026-11-16 | A Standard-speed, Deity-difficulty game on the same 180x112 Tectonic map replaces the Marathon variant as the stress row if it crashes stock; amends D157. | in force |
| D161 | 2026-11-16 | The stress map qualifies if it crashes stock at any turn up to 400, and a one-off carries the Marathon stress game toward turn 2000 with a save every 50 turns. | in force; amended by D180 |
| D162 | 2026-11-16 | No build goes to the owner to play until it has passed both human-input release journeys (basic and large) on that exact build. | in force; amended by D221 |
| D163 | 2026-11-16 | Epic 0437 studies turn times and memory across four setups, splitting each turn between the EXE's subsystems and the server, and may reserve the laptop as its quiet test space. | in force |
| D164 | 2026-11-16 | Game identity must be exact only like with like, headless against headless and windowed against windowed; a headless-versus-windowed divergence is not a defect. | in force |
| D165 | 2026-11-16 | Epic 0437 also finds the root cause of the EXE's large share of turn time and implements a fix, using only approved means such as import slots, vtable pointers, data writes and hooks in other modules. | in force |
| D166 | 2026-11-16 | Epic 2600 builds one self-contained installer per release, in the style of Vox Populi's, that checks the VP version, bundles the 43-civ variant, places all files and switches the install to 64-bit mode. | in force |
| D167 | 2026-11-16 | &lt;internal operations decision&gt; | done |
| D168 | 2026-11-16 | Upstream's maintainers agreed to take this repository as a pull request into a new 64bit folder, so DLL changes behind compile-time defines are allowed, but nothing is sent upstream without the owner's word. | in force |
| D169 | 2026-11-16 | Increment 4, Extras, adds flag-gated extras in the options file, with bugfixes on by default and visual additions off behind `EnableVisualAdditions`, as visuals only that never change rules or saves. | in force; amended by D190 |
| D170 | 2026-11-16 | Live terrain changes, land and water or any tile and mountains, are drawn at once without a reload as an always-on enhancement in epic 0465 (Extras C). | in force; D172's change to it withdrawn by D173 |
| D171 | 2026-11-16 | The installer's final shipping list and install process are approved as written, so the confirmation required by D27 and D166 is given. | in force; amended by D225 |
| D172 | 2026-11-16 | Live terrain changes sit behind their own LiveTerrain flag in the visual additions category, off by default; changes D170. | replaced by D173 |
| D173 | 2026-11-17 | Live terrain changes are an always-on bugfix with no flag at all, because stock's need to reload is the defect; D172 is withdrawn and D170 restored. | in force |
| D174 | 2026-11-17 | Upstream integration uses a private fork and an allow-list export script that selects but never edits files, publishing product and scrubbed notes in one pull request of two linked commits while ops, tracker, evidence and history stay private. | in force |
| D175 | 2026-11-17 | &lt;internal operations decision&gt; | in force |
| D176 | 2026-11-17 | The bomber freeze is fixed by making bombs register a hit on the submarine, with a fallback of skipping the strike animation if that fails within 48 hours; no Extras flag. | in force |
| D177 | 2026-11-17 | &lt;internal operations decision&gt; | done |
| D178 | 2026-11-17 | The turn-time study stops its baseline gathering on the laptop and puts testbeds on fixes and ameliorations first, using short development series of two or three trials of 10 to 20 turns per arm. | in force |
| D179 | 2026-11-17 | The full port's speedup source documents stay out of the repository; the project's files describe their entries in our own words by entry ID. | in force |
| D180 | 2026-11-17 | The Marathon stress game's turn-50 saves are no longer loaded in the stock client, because its limit was found at the T1100 save; the headless run continues. | in force |
| D181 | 2026-11-17 | The path finder's stamp-wrap fix (A10) and the wonder-specialization fix (A9) go into the DLL on both pins as core fixes in one pin batch, with upstream reports drafted for the owner's approval. | in force |
| D182 | 2026-11-17 | &lt;internal operations decision&gt; | amended by D201, D244 and D251 |
| D183 | 2026-11-17 | Increment 2 is released on its own as a candidate that passes the acceptance phase and the journeys before reaching the owner, without folding in increment 3's checks. | replaced by D195 |
| D184 | 2026-11-17 | Release builds of the 64-bit DLL use profile-guided optimisation once bug 0508's fix is in the pins, as a named release configuration with a locked profile and a retrain rule. | in force |
| D185 | 2026-11-18 | Every crash observed with the 64-bit patch gets its root cause determined and recorded, and hard fixes go to the Architect for fixes, mitigations or ameliorations. | in force |
| D186 | 2026-11-18 | The farm-field layout moves to the server as its own work, with an interim guard on the EXE's farm builder until it lands, and acceptance is by mechanism rather than crash count. | in force; amended by D231 |
| D187 | 2026-11-18 | Community bug reports the owner has not seen become low-priority tickets with about one hour of reproduction effort, and are closed as could-not-reproduce if they do not reproduce. | in force |
| D188 | 2026-11-18 | &lt;internal operations decision&gt; | replaced by D201 |
| D189 | 2026-11-18 | Increment 2 ships as the first release candidate as taken, without the fix for the rare farm-field crash on huge loads, which becomes a documented known issue. | done |
| D190 | 2026-11-18 | Epic 0504 closes after its four open tickets, second-tier speedups move to a new low-priority epic, and increment 4 work may start whenever places are free. | in force |
| D191 | 2026-11-18 | The AI update tick option defaults to on once a pinned-build check confirms an identical game, while the two drain options stay off by default pending comparisons. | in force; amended by D197 |
| D192 | 2026-11-18 | Live terrain is built by re-baking the changed region on the server behind a research gate, not by calling the EXE's whole-map rebuild. | in force |
| D193 | 2026-11-18 | Hidden embarks become a product feature behind a runtime switch on both DLL pins, with the open gaps closed and the default intended to be on. | in force |
| D194 | 2026-11-18 | For live terrain, a changed region drawn with plausible piece variants is accepted, since exact random-number replay of the stock choice is not required. | in force |
| D195 | 2026-11-18 | Closed beta 0.1b is targeted for a Friday release as one installer covering increments 1 and 2, minimap-free increment 3, performance options and live terrain where feasible. | cancelled by D200; revived by D203; amended by D260 |
| D196 | 2026-11-18 | &lt;internal operations decision&gt; | done |
| D197 | 2026-11-18 | The 0.1b release configuration switches on the visual-drain skip and unordered AI drain options and the AI update tick, written by the installer rather than compiled in. | lapsed with the beta cancelled by D200 |
| D198 | 2026-11-18 | The live-terrain ring, a look-alike blend over neighbouring tiles, is accepted for 0.1b, with offline pictures and a live video as evidence. | lapsed with the beta cancelled by D200 |
| D199 | 2026-11-19 | If the 43-civ journeys are not ready in time, the 43-civ variant ships on the standard variant's pass plus its own validated take, as a one-time exception. | lapsed with the beta cancelled by D200 |
| D200 | 2026-11-19 | After the stock game's Steam update, beta 0.1b is cancelled, the new EXE is analysed first, and no migration starts without the owner's approval. | in force; amended by D202 |
| D201 | 2026-11-19 | &lt;internal operations decision&gt; | amended by D251 |
| D202 | 2026-11-19 | The migration to the new Steam build starts as epic 0590, supporting both builds chosen at boot, with the per-build address table generated by scripts and stored locally only. | in force; narrowed by D218 |
| D203 | 2026-11-19 | After the migration, the goal is a pipeline of beta releases every day or two, ordered increment 3, then increment 4 extras, then performance work. | in force; amended by D216 |
| D204 | 2026-11-19 | &lt;internal operations decision&gt; | in force |
| D205 | 2026-11-19 | Live runs on test copies of the new build are authorized to confirm every patch mechanism and in-memory code writes work without any added anti-tamper. | done |
| D206 | 2026-11-19 | The MC has standing approval to delete remote work branches carrying a local path, once their content is safely on main or a team branch as a clean commit. | in force |
| D207 | 2026-11-19 | Seven answers for the Extras A and B questions are accepted: road style candidates, no bridge extra, own-aircraft patrols, defaults after picture approval, sharper-terrain tests, guide review, ships moored off by default. | in force |
| D208 | 2026-11-19 | Two idea epics are filed as backlog tickets: massive custom maps far above stock size and about one hundred civilisations, both resting on EXE code writes needing separate approval. | in force |
| D209 | 2026-11-20 | The road and railroad generator follows a design rule: straight runs are never cut off abruptly, and any break goes into short curved pieces. | in force |
| D210 | 2026-11-20 | Combat in increment 3 follows route S: the server owns each fight's clock and draws effects, and hands sound triggers to the EXE's audio. | in force; amended by D211 |
| D211 | 2026-11-20 | The first beta may ship with effects stage A, where the EXE's particle system plays effects at the server's command, with the server-drawn stage B following later. | in force |
| D212 | 2026-11-20 | The revised contribution guide text is committed, and the tidy road layout variant (e) is built but stays off by default while shipping as an opt-in in betas. | in force; amended by D215 |
| D213 | 2026-11-20 | A renderer that dies during play follows the same route as a start-up failure: a report, one popup, and the game ends rather than continuing with a grey world. | in force |
| D214 | 2026-11-20 | The new Steam build may be marked product-ready after one run without developer switches and one run on an installer-laid layout pass, with one guide sentence reworded. | in force |
| D215 | 2026-11-20 | One options key selects the road layout among stock, the tidy layout and a new all-connections layout, with the default staying stock. | in force; amended by D249 |
| D216 | 2026-11-20 | Changes D203's order: everything the first beta needs, including live land-and-water terrain changes, takes priority over other work, with a Monday release as the aim. | in force |
| D217 | 2026-11-20 | Every brief tells implementers to stop their own background monitors before finishing and how to detect and clean up orphaned processes, using a shared tool. | in force |
| D218 | 2026-11-20 | A one-off session opens a draft pull request upstream updating only the EXE address header, build stamps and DRM note, a narrow exception to the local-only address rule. | done |
| D219 | 2026-11-20 | &lt;internal operations decision&gt; | in force |
| D220 | 2026-11-20 | Release validation gets three time budgets: one hour for hotfixes, up to four hours for intermediate releases, and up to a day for full runs on the owner's word. | in force; amended by D221 |
| D221 | 2026-11-20 | The validation tiers (hotfix, intermediate, full) are built as epic 0630 at priority 8, and four earlier decisions (D162, D159, D151/D154, D127) are changed to match. | in force |
| D222 | 2026-11-20 | The weekly canary build of upstream master from D129 is suspended and no validation tier includes it; only the owner can switch it back on. | in force |
| D223 | 2026-11-21 | A crash cause with a known fix or mitigation is fixed before any build reaches human players, even if the release date slips; a known-issues line is no substitute. | in force |
| D224 | 2026-11-21 | With the patch switched off, the proxy still installs its guard of the EXE's UI message queue, and the key `[Proxy] QueueGuardWhenOff` can leave it out. | in force; amended by D225 |
| D225 | 2026-11-21 | Guards also run in off mode under one key `[Proxy] GuardsWhenOff`, Vox Populi's own Lua crash fixes may ship if the override is clean, and the 32-bit DLL stays the player's own. | in force |
| D226 | 2026-11-21 | &lt;internal operations decision&gt; | in force |
| D227 | 2026-11-21 | &lt;internal operations decision&gt; | in force |
| D228 | 2026-11-21 | Writes outside the EXE's data section are allowed for extra enhancements and performance gains behind the single option `[Extras] FullExeOptimizations`, and the patch must work without them. | in force |
| D229 | 2026-11-21 | The measurement of the single frame between two AI updates is pulled ahead as its own ticket at priority 6, after the first beta's needs. | done |
| D230 | 2026-11-21 | The server draws the farm buildings, and this work is a need of the first beta, with the farm-field crash fix `ServerFarmFields = 1` in its option set. | amended by D231 |
| D231 | 2026-11-21 | The first beta gets farm buildings by letting the EXE create each one at its plot's centre while the server draws it from the model tap; this replaces the server-placed route. | in force |
| D232 | 2026-11-21 | One time only, the first beta's candidate goes to the owner after the one-hour hotfix suite passes alone, so the owner can test by hand while the full plan continues. | done |
| D233 | 2026-11-21 | The project keeps one runbook of exact commands, and whoever works out a missing or broken command must add it to the runbook before handing back. | in force |
| D234 | 2026-11-21 | One time, an unvalidated preview installer of main is built for the owner's own install, and a visible session lets the owner direct installer customizations in person. | done |
| D235 | 2026-11-22 | The larger UI draw buffers are always on and no longer a player option, and the air-bases fix ships on by default once a live run shows it works. | in force |
| D236 | 2026-11-22 | A second beta candidate is cut the same day from main as a hotfix-tier take over the first candidate and goes to the owner after the hotfix suite passes. | done |
| D237 | 2026-11-22 | An epic for four classes of renderer problems found in playtesting runs at priority 9 under a visible lead, with each fix accepted by the owner's hands-on check on a test client. | in force |
| D238 | 2026-11-22 | &lt;internal operations decision&gt; | in force |
| D239 | 2026-11-22 | A core design goal: every patch build for one Vox Populi DLL version loads saves made by any other build for that version, and its saves load in all of them. | in force |
| D240 | 2026-11-22 | Changes D118: after a failure of the 64-bit path the patch stays on by default, and the popup offers removing the 64-bit patch as an opt-in. | in force |
| D241 | 2026-11-22 | A frame-pacing parity epic at priority 8 measures first, then removes blockers, then sets a frame budget, so camera movement matches stock smoothness at similar memory use. | in force |
| D242 | 2026-11-22 | Replaces the bounded wait: the interface always waits as long as needed so it matches the drawn picture exactly, and only a dead or hung server ends the wait. | in force |
| D243 | 2026-11-23 | The perimeter garrison look becomes a visual addition of its own, `[Extras] PerimeterGarrisons`, off by default; the other garrison looks are not offered to players. | in force |
| D244 | 2026-11-23 | &lt;internal operations decision&gt; | in force |
| D245 | 2026-11-23 | Beta 3 goes to the owner once playable after the hotfix tier and both release journeys, and beta 4 must include the frame-pacing fixes plus everything else ready. | in force; amended by D247 |
| D246 | 2026-11-23 | The AI drain options act only while the human player has both Quick Combat and Quick Movement on, read at the start of each AI turn; otherwise stock behaviour runs. | in force |
| D247 | 2026-11-23 | The remaining beta 3 intermediate-tier tests are cancelled so the test places go to beta 4 development; beta 3 stands as handed over. | in force |
| D248 | 2026-11-23 | Beta 4's candidate is cut once the full set of stutter fixes is merged, targeting midday on 2026-11-25, with no partial release earlier. | done |
| D249 | 2026-11-23 | The road layout option counts as a visual addition for every value, so it needs EnableVisualAdditions, and the road options are grouped together in the options file and installer. | in force |
| D250 | 2026-11-24 | `[Extras] ShipsMoored` takes three values: off by default, bow-to-land small ships, or ships end to end along the shore; the other two looks are not offered. | in force |
| D251 | 2026-11-24 | &lt;internal operations decision&gt; | in force |
| D252 | 2026-11-24 | Every open ticket gets a priority, a scope and live links to its blockers, so the queue offers ready work straight from the tracker without a publish step. | in force |
| D253 | 2026-11-24 | The fog-of-war overlay projection step R3 of ticket 0529 is dropped, because every frame it would change is hidden under the load screen. | done |
| D254 | 2026-11-24 | One time, for beta 4, the MC itself runs the finished installer on the owner's Steam install after the hotfix tier passes, with backups and the owner's saves untouched. | done |
| D255 | 2026-11-25 | Any feature that costs extra resources or alters base game behaviour or graphics must be configurable in the options file and installer, and 8 GB systems are a low-priority target. | in force |
| D256 | 2026-11-25 | The smooth wheel and trackpad zoom fix ships on by default, and the installer's checkbox for it starts ticked. | in force |
| D257 | 2026-11-25 | Beta 5, the public source on the fork's x64 branch and the upstream pull request are planned to go out together on the evening of 2026-11-27, or the next morning at the latest. | in force |
| D258 | 2026-11-25 | The export plan's five points are approved: clean build on the laptop, internal ids allowed in the notes folder, attribution kept, old x64 tip kept as history, and beta 5 as a pre-release after the owner plays it. | in force; amended by D263, D277 |
| D259 | 2026-11-25 | Beta 5, the public source and the upstream pull request are built on Vox Populi 5.5, with DLL pins moved to new branches and overlapping fixes resolved in upstream's favour. | in force |
| D260 | 2026-11-25 | The README's copyright holder is `VP64 contributors`, and the error popup's bug-report link and the update link point at upstream; this replaces D195's reports to the fork. | in force; amended by D261 |
| D261 | 2026-11-25 | For beta 5 the installer's source-code notice names the fork's public x64 branch until upstream merges the pull request, the one exception to D260's rule on product lines. | in force |
| D262 | 2026-11-25 | In every exported file the owner's first name becomes "the owner", by one scripted and reviewed edit; files that are not exported keep it. | done |
| D263 | 2026-11-25 | The export's clean build runs in a fresh folder; the export carries what the public source needs to build, including the 4390913 EXE address tables and our search patterns as interoperability data (a named exception to D83), while the newer EXE build's addresses stay private (D202). | in force |
| D264 | 2026-11-25 | Mounted units gliding and units cutting stock's curved path through a bend are beta 5 gates, and the landship treads in a turn are wanted too; beta 5 may slip by up to a day for them. | done |
| D265 | 2026-11-25 | The official VP64 logo is committed in docs/assets/, the one folder where pictures are committed; the rule against pictures and binaries in git stands for everything else. | in force |
| D266 | 2026-11-25 | Late-game terrain models flashing on a small pan is a beta 5 gate; its tests use a copy of the owner's save on a test install, never the Steam install. | done |
| D267 | 2026-11-25 | &lt;internal operations decision&gt; | replaced by D279 |
| D268 | 2026-11-25 | For beta 5 the landship tread effect is removed by a data write (a pass-through of the EXE's event listener, no code write), behind an options key that is on by default; the server drawing the effect itself follows later and replaces it. | in force |
| D269 | 2026-11-25 | Camera flashes are fixed as one class: what the world shows is kept, keyed by the world and sized for the whole map in the server, and removed only on evidence, never by a per-frame limit. | in force |
| D270 | 2026-11-26 | The Quicker Combat Animations extra offers one variant (the line-up skipped, the first strike at normal speed, the rest of the fight at three times speed) as one key, off by default. | in force |
| D271 | 2026-11-26 | &lt;internal operations decision&gt; | done |
| D272 | 2026-11-26 | &lt;internal operations decision&gt; | done |
| D273 | 2026-11-26 | &lt;internal operations decision&gt; | done |
| D274 | 2026-11-26 | &lt;internal operations decision&gt; | in force |
| D275 | 2026-11-26 | &lt;internal operations decision&gt; | in force |
| D276 | 2026-11-26 | The bridges following the tidied road layout is a beta 5 gate, and the beta 5 cut may wait several hours for it. | done |
| D277 | 2026-11-26 | The public source goes to the fork for the owner's review before the owner has played beta 5, with the fork's old x64 tip kept as history. | amended by D278 |
| D278 | 2026-11-26 | The public x64 branch goes to the fork on 2026-11-26 from the current source, after the owner's first name is removed from every exported file. | in force |
| D279 | 2026-11-26 | &lt;internal operations decision&gt; | in force |
| D280 | 2026-11-26 | Once the x64 branch is public, the source goes upstream with each installer release: a new pull request if none is open, otherwise a push to the open one's branch; until the first pull request is merged, every release adds to the initial one. | in force; amended by D281 |
| D281 | 2026-11-26 | The owner's approval of a release is also the approval to push that release's source upstream, provided the export checks pass; the text of a new pull request is still approved on its own. | in force |
| D282 | 2026-11-26 | This digest shows each decision about the project's internal operations as an internal operations decision, and notes about how the work is run (machines, tracker, queue and similar) stay out of the published folder. | in force |
