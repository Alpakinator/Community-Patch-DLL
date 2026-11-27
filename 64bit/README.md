# VP64

VP64 is a runtime patch for Sid Meier's Civilization V with Vox Populi that runs the DLL and the world renderer components (re-implemented due to being memory-inefficient and buggy) in a new 64-bit server. To connect it to the original exe, it proxies user inputs and lua state between them, and draws the world renderer picture directly into the exe's frame through a shared GPU texture. It is designed to reap as many benefits from a 64bit runtime as possible while staying strictly within the bounds of a runtime patch that *NEVER* modifies the exe at rest.

## Mission Statement

1. First and foremost, VP64 is a **BUGFIXING** initiative with the ultimate goal of *ZERO* crashes and bugs. By running memory-intensive components in 64bit space and increased patching opportunities in the EXE, the following bugfixes have already been made possible:

* Crashes from 32-bit address space exhaustion in large games
* 12+ misc. CTDs and hangs, such as a double-free bug in the EXE's farm-field layout procedure
* Terrain not switching during play (water, mountains, natural wonders)
* Grey tiles on camera movement
* Stale road paths not showing new roads when completed
* Bomber strikes on units that don't shoot back getting stuck
* Sounds and animations related to fights looping or playing when they shouldn't
* Aircraft vanishing after a rebase or a carrier move
* Unit figures clipping into hills and mountains

2. The second goal of VP64 is **Performance Improvements** - by caching aggressively and fixing engine bugs and inefficiencies, turn times and frame-rates can be substantially improved. In this fairly unoptimized beta turn times are already 35-55% better depending on map size and number of players!

3. A distant third goal is to provide gameplay enhancement opportunities using the new patch and world renderer code, such as cleaning up the tangled roads, introducing a "Quicker Combat Animations" setting, or adding visual clarity to intercepting planes with a patrol animation.

## What it is not

#### 1. A full 64 bit port or rewrite
Only the DLL, portions of the world renderer, and IPC/proxying machinery run in the 64-bit server.

The original exe is still **essential** as it handles all the menus, saves/loads, user input, audio, 2D UI, Lua engine, building the game mod DB, netcode, save files, asset loading, the dx11 rendering engine and device, and more. See diagram below for details.

This means that some of the benefits of the full port, such as quick menus, load times, mod menu "configuring game db" screens in all of the areas above are out of reach. This patch is scoped only to the world renderer and dll so performance benefits and stability increases are only felt in-game.

#### 2. A full graphics engine rewrite

The scope of what was ported to the world renderer was to move the components that consume a lot of memory in the 32 bit memory space which faces exhaustion, particularly memory that scaled with game length (which turned out to be most of it).

However, to save on implementation effort and reduce the scope of the patch the original exe still does all of the following: Direct3D 11 device and contexts, compiling shaders, camera, frame buffers, mouse to plot lookup, and all menus, windows, and everything HUD related such as flags, banners, yield icons, etc. 

## What it is

A runtime patch that when enabled proxies DLL game logic and the world renderer running in a 64-bit server, disabling the corresponding 32-bit exe systems. 

![Ownership diagram: the 32-bit shell on the left with its rendering engine broken down, the 64-bit server on the right with its world renderer broken down, and the hand-overs between them](research/07-architecture-decisions/ownership-end-product.svg)

The picture shows the intended end product; not every part of it is built today. The table of every graphics piece
with its owner, and what is decided and what is still a design, are in
[`research/07-architecture-decisions/ownership-end-product.md`](research/07-architecture-decisions/ownership-end-product.md).

## The three programs

| Program | Bits | What it does |
|---|---|---|
| The game's EXE (the shell) | 32 | Unchanged on disk. Window, interface, Lua, sound, network, saves, assets. It loads the game DLL as it always has. |
| The proxy (`proxy/`, with the shell side in `shell/`) | 32 | Sits in the game DLL's place inside the EXE, forwards the EXE's calls into the DLL to the server and back (`ipc/`), and draws the server's world picture into the EXE's frame. |
| The server (`server/`, with the world renderer in `render/`) | 64 | Loads the DLL built for x64, holds the game state, runs the AI and draws the world (terrain, water, rivers, roads, units, cities). |

Two properties hold throughout:

- **Saves stay compatible.** The x64 DLL writes saves in the 32-bit DLL's save format, so a save made with VP64 loads
  in the same Vox Populi version without it, and the other way round.
- **No degraded game.** If the 64-bit side cannot start or stops, the patch does not fall back to a half-drawn world:
  it shows an error message, writes a report, and offers to switch itself off ("Remove 64bit patch").

## What is where

```
server/      the 64-bit server: hosts the DLL, game state, AI, the world renderer's host side
render/      the world renderer (terrain, water, rivers, units, cities) and its offline scene tests
proxy/       the 32-bit proxy that takes the game DLL's place in the EXE, and the patch's helper program
shell/       the shell side: the compositor inside the EXE and its test programs
ipc/         the transport between the proxy and the server (the protocol: ipc/PROTOCOL.md)
options/     the patch's options file (VP64Options.ini) and its reader
models/      the reader of the game's .gr2 model files, without the Granny runtime
exebuild/    the EXE builds the patch knows, and the checks that tie each EXE location it uses to that build
deps/        the pinned third-party sources and the DLL source the build expects
tools/       build, release and test tools (the x64 DLL build in tools/x64-dll/)
docs/        docs/OPTIONS.md: every option the player can set
research/    the notes: what the EXE and the DLL do, the designs, the measurements and the negative results
BUILDING.md  the one-command build
```

The DLL's own 64-bit changes are not in this folder: they are in the DLL source next to it, behind the defines
`VP_OUT_OF_PROCESS` and `VP64_*`, so the 32-bit build of the DLL is unchanged.

## Where to start

**Playing it.** Every option the player can set, and how the patch is switched on and off, is in
[`docs/OPTIONS.md`](docs/OPTIONS.md).

**Building it.** [`BUILDING.md`](BUILDING.md): the toolchain, the folder layout and the one command that builds the
whole release set (the x64 DLL and its libraries, the server, the proxy, the helper and the compositor). Each part
also has its own build script, described in its folder's README.

**Reading the code.** Start from the README of the part you care about: [`server/`](server/README.md),
[`render/`](render/README.md), [`proxy/`](proxy/README.md), [`shell/`](shell/README.md), [`ipc/`](ipc/README.md),
[`options/`](options/README.md), [`models/`](models/README.md). The x64 DLL build is in
[`tools/x64-dll/README.md`](tools/x64-dll/README.md).

**Contributing.** Start with [`CONTRIBUTING.md`](CONTRIBUTING.md); the section "Contributing" below
lists every guide and template.

### If you are an agent

Most of this patch was built by AI coding agents working from shared notes, and the notes are written for you to
continue that way.

1. **Read [`research/README.md`](research/README.md) first**, including its section "For readers of the exported
   notes": it explains the confidence marks (`[C]` confirmed by a decompile or dump, `[L]` verified live, `[I]`
   inferred), the ticket numbers (four digits) and decision numbers (`D` + number) the notes cite, and which linked
   files are not part of this folder. Every decision is resolved in one line by
   [`research/07-architecture-decisions/decision-digest.md`](research/07-architecture-decisions/decision-digest.md).
2. **Search the notes before you investigate.** Search `research/` for the EXE address, DLL function, file name or
   symptom you are about to look into; the answer, or a negative result that saves you the time, is often there.
   EXE addresses are listed in [`research/02-exe-internals/addresses.md`](research/02-exe-internals/addresses.md)
   (virtual addresses at ImageBase `0x400000` of the DX11 EXE unless a note says otherwise).
3. **Use the runbook for commands.**
   [`research/06-tooling-and-workflows/runbook.md`](research/06-tooling-and-workflows/runbook.md) has the exact build,
   test, harness and capture commands; its section 0 lists the traps that cost the most time.
4. **Know who owns what.** Before changing anything that draws,
   [`research/07-architecture-decisions/ownership-end-product.md`](research/07-architecture-decisions/ownership-end-product.md)
   says whether the EXE or the server owns that piece.
5. **Work offline where you can.** The renderer has offline scene tests and tools that draw recorded scenes without
   a running game ([`render/README.md`](render/README.md), [`server/README.md`](server/README.md)); a live game is
   needed only for the final check.
6. **Keep the rules below** (in full, with how each is checked, in [`CONTRIBUTING.md`](CONTRIBUTING.md)), and
   write what you learn back into the notes on that topic, correcting old statements in place rather than adding
   contradictions. `CONTRIBUTING.md` section 7 says what an agent-written pull request must contain.

## Rules the code keeps

- **The EXE is never modified on disk.** The patch changes the EXE's behaviour only in memory, at run time, and only
  on EXE builds it recognises (`exebuild/`); on an unknown build it touches nothing.
- **Nothing from the EXE or the game's files is in this folder.** Behaviour re-implemented from the EXE is written
  from the notes' descriptions of it, and no decompiler output is committed. Shaders and assets are read from the
  player's own install at run time.
- **The 32-bit DLL stays unchanged.** Every 64-bit-only change to the DLL is behind the compiler's own macros or a
  `VP_OUT_OF_PROCESS` / `VP64_*` define.
- **Same game, same saves.** The server runs the same game rules as the 32-bit DLL, and its saves load in either.
- **Extras are optional.** Changes beyond fixing what the stock game does wrong live in the `[Extras]` options:
  bug fixes on by default, visual additions off by default. How to propose one:
  [`CONTRIBUTING-extras.md`](CONTRIBUTING-extras.md).

## Contributing

Contributions are welcome, from people and from coding agents working for them. Every guide for contributors:

| Guide | Read it when |
|---|---|
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Before any change. How a change is proposed and merged; the rules every change keeps (G1 to G11) and how each is checked; what to know and what to run for each part of the code; the tests for the picture and for the same game; how to update the notes; what a pull request must say. |
| [`CONTRIBUTING-extras.md`](CONTRIBUTING-extras.md) | Your change adds an *extra*: an optional difference from the stock picture, either a fix of something the stock game draws wrongly or a visual addition, switched by its own key in `[Extras]`. Its rules (E1 to E12), the categories and defaults, and a worked example from the option to the pull request. |
| [`.github/ISSUE_TEMPLATE/propose-an-extra.yml`](.github/ISSUE_TEMPLATE/propose-an-extra.yml) | The questions to answer when you propose an extra in an issue, before building it. |
| [`.github/PULL_REQUEST_TEMPLATE/extra.md`](.github/PULL_REQUEST_TEMPLATE/extra.md) | The description a pull request for an extra fills in: the flag, the document entry, the pictures and the checks. |
| [`options/README.md`](options/README.md), section "Adding an option" | Your change adds a key to `VP64Options.ini`. |
| [`research/README.md`](research/README.md), section "How to use it" | You learned something the next contributor needs: where it goes in the notes and how it is written. |

The two templates live inside this folder, where the repository's issue and pull request pages do not offer them
automatically: open the file and copy its questions into your issue or pull request. Bug reports go to the tracker
named under "Bug reports" below.

## The notes

The notes in [`research/`](research/README.md) record what the EXE and the DLL do, how the patch is designed, what
was measured, and what was tried and did not work. Start at [`research/README.md`](research/README.md); the decision
numbers the notes cite are listed in
[`research/07-architecture-decisions/decision-digest.md`](research/07-architecture-decisions/decision-digest.md).

## Bug reports

Please report problems with the 64-bit patch on the Community-Patch-DLL issue tracker:
<https://github.com/LoneGazebo/Community-Patch-DLL/issues>.

## Licence

Copyright (C) 2026 VP64 contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The full text is in
[`LICENSE`](LICENSE).

Third-party components keep their own licences: the archives the x64 build compiles are pinned in
[`deps/third-party.lock`](deps/third-party.lock) (Lua 5.1.4: MIT; SQLite 3.7.17: public domain; zlib 1.2.3: zlib
licence), and MinHook in [`shell/third_party/minhook`](shell/third_party/minhook/LICENSE.txt) keeps its BSD licence.
Code taken from another GPL-compatible project names its origin (repository, path, commit) in a comment at the top of
the file or block. Civilization V itself, its EXE and its assets are not part of this folder and are not covered:
the patch reads the game's shaders and assets from the player's own install at run time.
