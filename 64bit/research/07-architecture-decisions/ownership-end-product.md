# Who owns what in the finished patch

Drawn by the Architect on 2026-11-14 on the owner's order ([reworded for privacy]: draw the diagram of the intended end product, without current milestones or completion results). It replaces the ownership diagram of the 2026-11-10 architecture
review (`research/sources/shell-architecture-review.html`, section 1), which showed the state planned for
increment 2. It shows the intended end product, not what is built today.

![Ownership diagram: the 32-bit shell on the left with its rendering engine broken down, the 64-bit server on the right with its world renderer broken down, and the hand-overs between them](ownership-end-product.svg)

The original 32-bit EXE stays as the shell and keeps everything that is not the game rules or the picture of the
world. The 64-bit server holds the only copy of the game and draws the whole world. The EXE keeps its Direct3D
device, swap chain, camera, picking and UI pass. Every world system of its rendering engine is either switched off
or started without reaching the screen, and the compositor draws the server's picture of the same frame in their
place.

## Graphics: where each piece lives and how the server gets it

| Piece | Owner | How the server gets it |
|---|---|---|
| Direct3D 11 device, contexts, swap chain, back buffer, depth buffer, Present | EXE | It does not. The server has its own device and shares only the frame ring's textures with the EXE |
| Camera and view matrices | EXE | The compositor publishes them every frame with a frame number, as soon as the EXE has set its views |
| Hex picking | EXE | Not needed. The EXE picks on a flat plane and does not use the terrain for it |
| Compiled shaders (1,417) | the EXE file | The server reads the shader section of the EXE file on disk at run time. Nothing is copied into the patch (D83) |
| Textures, model files, terrain tables | the game install | The server reads the packs and loose files itself with its own readers. It does not take textures from the EXE's memory |
| River mesh, coast-wave chunks, sea settings | EXE, built at load | The compositor copies them once per load through shared memory. The EXE never draws them |
| Terrain height field | EXE, built at load | Copied once per load the same way. The EXE's field stays the reference for everything the EXE anchors in the world, so the server places its models by the same heights |
| Highlights, selection, ranges, borders | EXE, drawn into its own texture | The compositor copies the EXE's overlay texture every frame and the server lays it over its terrain and water (epic 0167) |
| Ranged-attack arrows, path display, view mode | EXE UI events | The proxy listens to the UI's own Lua events and passes them to the renderer. They never reach the game rules |
| Ocean surface grid, currents map, water clock | server | Built by the server. The EXE rebuilds its ocean grid every frame, so there is nothing to copy |
| Baked terrain tile colours | server | Baked by the server from the heights, the terrain data and the install's textures, with the EXE's own shaders |
| Everything that changes during play: plots, roads, borders, fog, units, cities | server | From the DLL's own change notices inside the server |
| Minimap picture | server | The other way: the compositor copies the server's minimap into the texture the EXE's minimap panel already shows |
| Unit positions while units move | server | The other way: the proxy raises the UI's own position events from the server's table, so flags follow the units the server draws |

## What in the picture is decided and what is not

- **Decided:** only the DLL and the world renderer run in the server and everything else stays in the EXE (D80);
  the renderer has its own thread and never reads the DLL directly (D81); every land tile is kept baked by default
  (D88); Overlay and StrategicView stay started in the EXE (D93, bug 3010); the EXE's switches stay in `config.ini`
  and a failure is loud (D125).
- **From the plan, not confirmed by the owner as a priority:** units, cities and combat effects drawn by the server. The
  handoff (section 3.1) calls this increment 3 and says it starts only on the owner's word.
- **The Architect's designs of 2026-11-14, not decided or tested:** the last three hand-overs in the middle column
  (UI overlays to the server, the minimap picture and the unit positions back to the EXE), and with them the
  Minimap system as "started, never drawn". They are in `architect-2026-11-14-hand-over-designs.md`.
- **Open:** D81 has the server build terrain heights itself in a later bake stage. Under the designs above the
  EXE's height field stays the reference, so any height the server changes during play has to be written into the
  EXE's field as well (rule R4 of the design file).

## Sources and upkeep

The numbers in the picture: 9 deferred contexts (`research/05-graphics-composition/insertion-point-and-frame-structure.md`
9.1), 1,417 shaders (`asset-pipeline.md` 2.1), 999 engine-to-DLL virtuals, 296 DLL-to-engine methods used and 3,463
Lua registrations (`research/03-dll-engine-boundary/proxy-design.md` 2), the 3.4 ms wait
(`renderer-in-server-design.md` 3). The systems and their switches: `research/02-exe-internals/disable-systems-and-masks.md`,
`world-systems-and-stubs.md`, `research/05-graphics-composition/water-system.md` 9.

The SVG file is the source; edit it by hand. The repository's front `README.md` shows the same file. The text of
the SVG was checked for overflow by measuring every label against its box in a browser (2026-11-14, none over);
the published page version was not looked at by its author.
