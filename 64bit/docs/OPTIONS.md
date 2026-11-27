# Options of the Vox Populi 64-bit patch

The patch has one options file, `VP64Options.ini`. It is a plain text file: open it with Notepad or any other text
editor, change a value, save it, and start the game.

## Where the file is

```
Documents\My Games\Sid Meier's Civilization 5\VP64Options.ini
```

That is the same folder as the game's own `config.ini`, `UserSettings.ini` and your saves. The patch creates the file
the first time it runs, with every option listed at its default value. Installing or updating the patch writes into it
only the release's own settings and what you choose on the installer's options page (below); everything else you wrote
stays.

If you want the file somewhere else, set the environment variable `VP64_OPTIONS` to the file's full path (for example
`D:\Games\civ5\VP64Options.ini`). The patch then reads that file and creates it there if it is missing.

## How to change an option

A new file looks like this (shortened):

```ini
[Renderer]
; How much of the map's terrain the patch keeps baked and ready to draw ...
; BakedTileCameraRadius = -1
```

Lines that start with `;` are comments, so the option above is switched off and the patch uses its default. To change
it, delete the `;` at the start of the option's line and edit the value:

```ini
[Renderer]
BakedTileCameraRadius = 5
```

Close the game before you edit the file. The patch reads it when the game starts, so a change takes effect at the next
start.

To go back to the defaults, close the game and delete the file; the next start writes a fresh one. While the patch is
switched on (`vp64helper on`, which the installer runs), the fresh file is not all defaults: it gets the release's own
settings (the patch's `VP64\release-options.txt`, the values the installer wrote) and `ServerRenderer = 1`, so the game
looks and plays as after the install. If the patch cannot find those settings, it does not start a half-configured
game: it shows its error message and report instead (see `ServerRenderer` below).

A file from an older release keeps working after an update: an option it does not set takes its default, and the
patch's log (`vpproxy.log`) names, in one line that starts with `options: note:`, every setting of the release the file
does not have. Delete the file as above to get them.

## The installer's options page

When you install the patch, the installer shows an options page between the game folder and its checks: every
option of this file, grouped by its `[section]`, as a tree (a box under another is greyed while that one is off, and keeps its
own tick), with the option's description below. Each option has a short title, shown by the installer beside its key in
this file. The page starts from the values your `VP64Options.ini` already sets, else from the release's own settings
and the defaults. The installer writes into the file the release's settings, every option you set away from its
default, and every option your file already set; the rest stays commented at its default. `[Renderer]
ServerRenderer` is shown but not changeable there: the installer sets it (`vp64helper on`). The page's list is made
from the patch's own table of options when the installer is built, so it always matches this file. Removing the patch
puts your file back as it was before the install. The text option `[Shell] WindowTitlePrefix` is one box there, "Show
[VP64] in the window title": unticked, the installer writes `WindowTitlePrefix = ""`; ticked, the prefix you have (your
own text, if your file sets one, else the default) stays.

## The options

### [Renderer] BakedTileCameraRadius

How much of the map's terrain the patch keeps baked and ready to draw. Baked terrain is the finished ground texture of
a tile. A tile whose terrain is not baked yet shows gray until it is.

| Value | Meaning |
|---|---|
| `-1` | Prevent gray tiles by caching the whole map, at the cost of resource usage (the default). After the game has baked the map once in the background, no tile is gray after you pan, jump the camera or come back to a place. Uses the most video memory: about 413 MB on a huge map, less on smaller maps. |
| `0` to `128` | Only the tiles within that many rings (hexes) of what is on the screen. Uses less video memory; tiles further away are baked when the camera comes near them, so a fast pan or a jump across the map can show gray tiles for a moment. `0` keeps only the tiles on the screen. |

If your graphics card is short of video memory, try `5`:

```ini
[Renderer]
BakedTileCameraRadius = 5
```

Default line, as the patch writes it: `BakedTileCameraRadius = -1`.

### [Renderer] ServerRenderer

Whether the patch's 64-bit server draws the map's terrain instead of the game. Experimental: leave it at `0` unless you
are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws the map itself, as without this option. |
| `1` | On. The server draws the terrain, and the patch's compositor puts it into the game's picture under the units, cities and the interface. |

**You normally do not set this option by hand.** The patch's switch, `vp64helper.exe` (beside the patch's DLL), sets it
together with the game's own `config.ini`:

- `vp64helper on` switches the patch's memory-saving configuration on: it turns some of the game's own drawing systems
  off in `config.ini` (the server draws those parts instead) and sets `ServerRenderer = 1` here. Both belong together:
  with those systems off and `ServerRenderer = 0`, nobody would draw the terrain.
- `vp64helper off` puts both back as they were before `on`: your own values in `config.ini`, and this file byte for
  byte (or, if `on` created the file, the file is removed again). If you changed this file after `on`, only the
  `ServerRenderer` line goes back and your other changes stay; if you changed `ServerRenderer` itself after `on`, `off`
  leaves your value and says so.
- `vp64helper status` shows the state of both files.
- `ServerRenderer = 0` written by hand while the patch is switched on: the patch does not start the game that way (it
  would show a degraded world: no fog of war, no forests, none of the server's drawing). It shows its error message,
  writes a report and ends the game. The patch stays on: "Close" keeps it so, and the next start fails the same way
  until `ServerRenderer = 1` is back (`vp64helper on` sets it). "Remove 64bit patch" in the message switches the patch
  off instead. To play without the server's terrain, use `vp64helper off`.

The patch keeps its record of the file as it was in `vp64-options-side.ini` beside it while the switch is on; do not
edit or delete that file.

With `1`, the patch's folder (the one that holds `VP64\`) must also hold `shell_composite.dll` and the file
`shell_composite.sha256` that came with it. The patch checks both against its own files before it uses the compositor.
If either is missing or does not match (for example after a partial update), the patch's log (`vpproxy.log`) says why,
in a line that starts with `shell:`. While the patch is switched on (`vp64helper on`), the patch then does not start
the game (it would show a degraded world): it shows its error message and report and asks you to run its installer
again. The patch stays on unless you choose "Remove 64bit patch" in the message. With the patch switched off, the game draws the map itself.

Default line, as the patch writes it: `ServerRenderer = 0`.

### [Renderer] WhiteFogMask

Whether the patch gives the game's buildings, wonders, improvements, resources and trees their textures when the
game's own fog-of-war drawing (the `FluidFOW` system) is switched off. On by default; set it to `0` only to compare
with the picture without it.

| Value | Meaning |
|---|---|
| `1` | On (the default). When the game starts, the patch loads one small white texture that ships with the game (`White.dds`, the map editor's "no fog" texture) with the game's own texture loader and uses it as the fog-of-war mask of those models. |
| `0` | Off. Nothing changes: with `FluidFOW` off, those models draw dark red and trees do not draw at all. |

It acts only when the game's `FluidFOW` system is off in `config.ini`; with `FluidFOW` on, the game's fog owns the mask
and the patch leaves it alone. It works only with the game build it was written for (Civilization V DX11, the last
Steam build): the patch first checks the game's code and does nothing if it differs. With it on, models on plots you
have seen but do not see now are drawn without the game's fog tint. Every step is one line in the patch's log
(`vpproxy.log`) that starts with `shell: white mask:`.

Default line, as the patch writes it: `WhiteFogMask = 1`.

### [Renderer] ServerUnits

Whether the patch's 64-bit server draws the units (soldiers, ships, aircraft, workers and the other figures on the map)
instead of the game. Experimental: leave it at `0` unless you are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws the units itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws every unit where the game puts it, and the game draws none of them. While a fight is shown (Quick Combat off) and `ServerCombat = 1` (the default), the server also plays the fight: the fighters attack, are hit and fall, with their sounds and the fight's effects, and the game waits for the fight to end. With `ServerCombat = 0` the game times each fight and plays its sounds and effects itself (see `ServerCombat`). |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game draws the units, and the patch's log
(`vpproxy.log`) says why in one line that starts with `shell: [Renderer] ServerUnits = 1`.

Default line, as the patch writes it: `ServerUnits = 0`.

### [Renderer] ServerCombat

Whether the patch's 64-bit server also plays the fights while it draws the units: it times each fight, plays the
fighters' sounds and shows the fight's effects, and the game waits for it. A troubleshooting switch: if fights look,
sound or wait wrong, set it to `0` and say so in your bug report.

| Value | Meaning |
|---|---|
| `1` | On (the default). With `ServerUnits = 1`, the server and the patch's parts in the game agree once per game start whether all of them can work on this game. If they can, the server plays the fights. If one of them cannot, none of them is used: the game plays every fight itself (its own timing, sounds and effects), and the patch shows one error report. |
| `0` | Off. The game times the fights and plays their sounds and effects itself, as without this option; the units are still drawn by the server. No error report: the patch's log (`vpproxy.log`) says so in one line that starts with `shell: [Renderer] ServerCombat = 0`. |

It does nothing while `ServerUnits` is `0`.

Default line, as the patch writes it: `ServerCombat = 1`.

### [Renderer] ServerArc

Whether the patch's 64-bit server draws the red arc that appears when you aim a ranged attack (an archer, a city or a
siege unit on its target), instead of the game. Experimental: leave it at `0` unless you are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws the arc itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws the arc and the game's own arc is switched off. |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game draws the arc, and the patch's log
(`vpproxy.log`) says why in one line that starts with `shell: [Renderer] ServerArc = 1`.

Default line, as the patch writes it: `ServerArc = 0`.

### [Renderer] ServerPath

Whether the patch's 64-bit server draws the markers of a unit's planned move (the turn discs on the path's plots, the
chevrons between them and the ring on the destination, shown while you choose where a unit goes), instead of the game.
Experimental: leave it at `0` unless you are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws the markers itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws the markers and the game's own are switched off. |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game draws the markers, and the patch's log
(`vpproxy.log`) says why in one line that starts with `shell: [Renderer] ServerPath = 1`.

Default line, as the patch writes it: `ServerPath = 0`.

### [Renderer] ServerFarmFields

Whether the patch's 64-bit server lays out and draws the farm fields (the patches of crops around a farm) and the
tracks between them, instead of the game. With `1` the game's own farm-field layout, which can crash on large late games, never runs. The first beta sets it to `1`.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game lays out the farm fields itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws the fields and tracks, the same ones on every load of a save, and the game's own farm-field layout is no longer run. Trees under a field are cut as in the game. The game still makes each farm's buildings (barns and silos) itself; they stand at the centre of the farm's tile, among the server's fields, where the unpatched game puts them on the edge of a field (ticket 0655, 2026-11-21; before it no farm building was drawn with this option on). |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game lays out the fields, and the patch's log
(`vpproxy.log`) says why in one line that starts with `shell: [Renderer] ServerFarmFields = 1`.

Default line, as the patch writes it: `ServerFarmFields = 0`.
### [Renderer] ServerCities

Whether the patch's 64-bit server draws the cities (their buildings, walls, world wonders, the buildings shown in a
city and ruins, and the road and railroad bridges across rivers) instead of the game. Experimental: leave it at `0` unless you are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws the cities itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws every city model it has, and the game draws none of those. A model the server does not have (from a mod, say) is still drawn by the game. |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game draws the cities, and the patch's log
(`vpproxy.log`) says why in one line that starts with `shell: [Renderer] ServerCities = 1`. If the server cannot draw
the cities once the game is running, the game stops with an error message and a report, rather than going on with a
broken map.

Default line, as the patch writes it: `ServerCities = 0`.

### [Renderer] ServerLandmarks

Whether the patch's 64-bit server draws the improvements (farms, mines, plantations, forts and the other tile
improvements), the resources, the natural wonders and the pontoon bridges instead of the game. Experimental: leave it at
`0` unless you are testing the renderer.

| Value | Meaning |
|---|---|
| `0` | Off (the default). The game draws them itself, as without this option. |
| `1` | On, together with `ServerRenderer = 1`. The server draws every such model it has, and the game draws none of those. A model the server does not have (from a mod, say) is still drawn by the game. |

It works only together with `ServerRenderer = 1`. With `ServerRenderer = 0`, or when the patch cannot use its
compositor (see `ServerRenderer`), this option does nothing: the game draws them, and the patch's log (`vpproxy.log`)
says why in one line that starts with `shell: [Renderer] ServerLandmarks = 1`. If the server cannot draw them once the
game is running, the game stops with an error message and a report, rather than going on with a broken map.

Default line, as the patch writes it: `ServerLandmarks = 0`.

### [Renderer] HideTreadEffect

Whether the game's own scrolling tread effect of the landship (the First World War tank) is hidden while the patch's
server draws the units. The game draws that effect on its own hidden copy of the unit, not on the tank the server
draws, so while the tank turns, the moving treads come apart from it and trail behind or beside it for about a second.

| Value | Meaning |
|---|---|
| `1` | Hidden (the default). The game's tread effect is not drawn. The tank keeps the tracks moulded into its body, but they do not scroll while it drives. Nothing else changes: the helicopter's rotor blades, the jets' engine fire, the thrown spears and every other effect are drawn as before. |
| `0` | Shown: as the game draws it; in a turn the treads can lag behind or beside the tank. |

It does nothing while `ServerUnits` is `0` (the game then draws the tank and its treads together, as without the patch).
The patch's log (`vpproxy.log`) says in one line starting with `farm_guard: treads (ticket 0784)` whether the effect is
hidden, and, should this game build not match what the patch expects, one line starting with
`farm_guard: LOUD: the tread filter (ticket 0784) is OFF`: the treads are then drawn as with `0`.

Default line, as the patch writes it: `HideTreadEffect = 1`.

### [Renderer] ModelStore

Whether the patch's 64-bit server remembers every city and improvement model it has drawn (the "model store", ticket
0789), so that none of them flashes away for a single frame while the map moves or a turn is processed. Without it a
model the game hands over late, or not at all for one frame, is missing from that frame's picture.

| Value | Meaning |
|---|---|
| `1` | On (the default). The server keeps every model it has seen until the game shows that it is gone, and draws it in a frame in which the game's own hand-over lacks it. It costs the server's memory only, none of the game's: about 1.5 MB on a large late-game map, at most about 75 MB for each of its two picture passes at its built-in limit. A model the game really removes is shown at most one frame late. |
| `0` | Off. The server remembers only the models of the last few seconds of a camera move (the patch's behaviour before this option). |

It does something only together with `ServerRenderer = 1` and `ServerCities = 1` or `ServerLandmarks = 1`. The
server's log says which way it is in one line that starts with `models: the model store` (on) or `models: the keep`
(off), and counts what the store drew in a line `models store:` every second. A test setting in the
server's `server.cfg`, `render_models_store=0` or `=1`, overrides this option.

Default line, as the patch writes it: `ModelStore = 1`.

### [Performance] AiUpdateTickMs

How long the game waits at most, during the AI players' turns, before it asks for the next AI player's moves. The game
itself asks at most every 100 ms (its "update tick"); between two AI players it often waits part of such a tick for
nothing.

| Value | Meaning |
|---|---|
| `0` | On the next frame, and only while the AI players move (the default, highest performance). The AI turns go faster. |
| `1` to `100` | At most this many milliseconds, and only while the AI players move. |
| `-1` | As the game does it. The patch changes nothing. |

The default is `0` since patch builds of 2026-11-19 (it was `-1`). An options file that has its own
`AiUpdateTickMs = ...` line keeps that value; a file that shows the key only as a comment (`; AiUpdateTickMs = -1`,
as older builds wrote it) takes the new default `0`. To keep the game's own pacing, write `AiUpdateTickMs = -1`
without the `;`.

Your own turn is never changed: the game still asks every 100 ms while it is your turn, so your units move on screen
exactly as before. The game's results do not change either, only how fast the AI turns go by. It works only with
the game build it was written for (Civilization V DX11, the last Steam build). If the patch's checks of the game fail
when the game starts, it writes an error report, shows its error window and keeps the game's own pacing; if a check
fails later in the game, it writes the report and a line in the patch's log (`vpproxy.log`) and keeps the game's own
pacing, without a window. Every step is one line in `vpproxy.log` that starts with `pacing:`.

Default line, as the patch writes it: `AiUpdateTickMs = 0`.

### [Performance] AiSkipVisualDrain

Experimental. Whether the next AI player may start its moves before the game has finished showing the previous AI
player's moves. During the AI turns the game normally waits, after each AI player, until every move, attack and other
event of that player has been handed to the map; only then does it ask for the next player's moves.

| Value | Meaning |
|---|---|
| `0` | As the game does it (the default). The patch changes nothing. |
| `1` | On, only while the AI players move: the next AI player starts at once. The AI turns go by faster; the AI's moves are shown further behind the game, and some may still be shown when your turn starts. |
| `2` | Experimental prototype. On as `1`, except after an AI player that left many moves to show (more than 100 of the game's queued map events): there the game waits, as it does without the patch, until they have been shown before the next AI player starts. The map stays close to the game during the AI turns, without the long catch-up at their end. Best together with `AiDrainUnordered = 1`, which shows each such player's moves in one frame; without it, the patch writes one line in `vpproxy.log` that value 2 gains little. |

**Only with Quick Combat and Quick Movement on** (ticket 0718). With `1` or `2` the option acts only while both Quick
Combat and Quick Movement are ticked in the game's options. With either of them off the AI's moves and fights are
animated, and the next AI player starting at once would let several AI players' animations play at the same time; so
then the game waits after each AI player exactly as it does without the patch. The patch checks the two settings
before each AI player's moves start, so ticking or unticking them during a game switches the option from the next AI
player on; nothing that is already being shown is cut short. Each switch is one line in `vpproxy.log` starting with
`pacing: D246:`.

Your own turn is never changed. The game's results do not change, only how fast the AI turns go by and when their
moves are shown. It works only with the game build it was written for (Civilization V DX11, the last Steam build). If
the patch's checks of the game fail when the game starts, it writes an error report, shows its error window and keeps
the game's own pacing; if a check fails later in the game, it writes the report and a line in the patch's log
(`vpproxy.log`) and keeps the game's own pacing, without a window. Every step is one line in `vpproxy.log` that starts
with `pacing:`. It can be combined with `AiUpdateTickMs`.

For testers: value 2's limit is fixed in the patch (`proxy/src/proxy_pacing.h`), not an option: the floor
`kHoldFloorRecords` = 100 (after an AI player with at most this many undispatched events of the game's visual queue the
next player starts at once, as with `1`). It comes from ticket 0568's model (variant F1,
`research/08-performance/d165-pacing-fix-design.md` section 11). Ticket 0571 built 0568's first form (the next player
held for at most 8 frames) and found that the game shows nothing while it holds; value 2 therefore lets the game's own
wait act instead (`research/08-performance/turn-time-study.md` "Option 2 value 2: drain after each AI player").

Default line, as the patch writes it: `AiSkipVisualDrain = 0`.

### [Performance] AiDrainUnordered

Experimental, and only together with `AiSkipVisualDrain = 1` or `2`. With `AiSkipVisualDrain` on, the AI players' moves
that are still to be shown are handed to the map at the end of the AI turns (with `2` also after each AI player that
left many of them). The game hands them over in its own careful
order: one step of each unit per frame, so a turn with many units that embark or move far takes a few seconds of
frames at its end. This option hands that whole remainder over in one frame instead, in the order the moves were made.

| Value | Meaning |
|---|---|
| `0` | As the game does it (the default). The patch changes nothing. |
| `1` | On, only at the end of the AI turns (with `AiSkipVisualDrain = 2` also after each AI player the game waits for), and only while Quick Combat and Quick Movement are on and nothing on the map is still being animated: the AI's last moves appear together and the AI turns end sooner. |

Like `AiSkipVisualDrain`, it follows Quick Combat and Quick Movement during the game (ticket 0718): the patch checks
them when the AI turns' last moves start to be shown, and with either off the game shows them in its own order.

Your own turn is never changed. The game's results do not change, only how fast the end of the AI turns goes by. With
`AiSkipVisualDrain = 0` this option does nothing (one warning line in `vpproxy.log`). It works only with the game build
it was written for (Civilization V DX11, the last Steam build). If the patch's checks of the game fail when the game
starts, it writes an error report, shows its error window and keeps the game's own order; if a check fails later in
the game, it writes the report and a line in the patch's log (`vpproxy.log`) and keeps the game's own order, without a
window. Every step is one line in `vpproxy.log` that starts with `pacing:`.

Default line, as the patch writes it: `AiDrainUnordered = 0`.

### [Performance] GameEventsOneCrossing

Whether a Lua game event that the game raises (the `GameEvents` the game's rules and the mods listen to) crosses from
the patch's 64-bit server to the game once, instead of once for every argument and twice more. Experimental: a speed-up
of AI turns that leaves the game exactly as it plays without it.

| Value | Meaning |
|---|---|
| `0` | Off (the default). Every game event crosses as before. |
| `1` | On. The server keeps the event's arguments and sends them with the event; the game builds them and runs the event's listeners as before. |

Default line, as the patch writes it: `GameEventsOneCrossing = 0`.

### [Performance] HiddenEmbarks

During the AI turns, AI units board and leave boats (embark and disembark) many times. For each one the game prepares
a new model on the map and shows the change step by step, one frame at a time, even where no human player can see the
unit. With this option the patch's game DLL does not send the embark or disembark of a unit that no human player can
see. When that unit comes into view, the DLL sends the unit as it is then.

| Value | Meaning |
|---|---|
| `0` | As the game does it (the default in this file; a release of the patch may write `1`). The patch changes nothing. |
| `1` | On: the AI turns are shorter (about 2-3 seconds per turn on a standard map in the middle of the game). Out of sight nothing changes; a unit that comes into view shows its right model at its right place. |

Your own units, and every unit any human player can see, are shown as before. In a hotseat game a unit is held back
only when no human player can see it. The game's results do not change, only what is shown. Multiplayer has not been
tested with this option.

The option needs the patch's own game DLL. If that DLL does not have the feature, the patch writes an error report and
a line in the server's log, and the game shows every embark as before.

Default line, as the patch writes it: `HiddenEmbarks = 0`.

### [Performance] RenderFrameJobs

How many helper threads the patch's 64-bit server uses to prepare each picture of the map. While the server draws a
picture, a helper thread prepares the units for it (their animation and placement) at the same time, so the picture
is ready sooner. Needs `[Renderer] ServerRenderer = 1`; otherwise the server draws nothing and the option does nothing.

| Value | Meaning |
|---|---|
| `-1` | Automatic (the default): 2 helper threads on a processor with more than 2 threads (logical cores), else none. |
| `0` | None: the server prepares each picture on its drawing thread alone, as before the helper threads existed. Uses the least processor time. |
| `1` to `8` | That many helper threads. More than 2 has not been measured to help. |

Each helper thread waits while it has no work; it uses processor time only while a picture is being prepared. The
server's log (`render.log` in the run folder) names the number in effect in its line `frame jobs: N workers`.

Default line, as the patch writes it: `RenderFrameJobs = -1`.

### [Proxy] GuardsWhenOff

When the patch is on, it guards the game against some of the game's own crashes. This option decides whether the
patch keeps those guards while the patch is switched off: by the off switch, or when the patch falls back to Vox
Populi's own 32-bit DLL because its 64-bit part could not start.

The guards this option covers:

- **The UI message queue guard.** The game passes many small messages from the map to its interface through a fixed
  4 MB queue. During a long load (a large late-game save) the game can write past the end of that queue. That
  corrupts memory and can crash the game or make it hang, and it happens without the patch too. The guard drops a
  message that would not fit instead.
- **The farm builder guard.** The game lays out the fields around each farm itself, and on large late games this
  layout can corrupt its own memory and crash, or hang on very large maps. The guard checks the layout after each farm
  and stops feeding it when it went wrong; near the limit of very large maps it skips the layout. A farm it skipped
  shows its buildings without the field pattern around them.

These two are all it covers. The patch's growth of the game's interface drawing buffers (format rules below)
runs only while the patch is on: with the patch switched off the patch is not on the game's interface path, so
that growth cannot run.

| Value | Meaning |
|---|---|
| `1` | On (the default). The guards run while the patch is switched off as well. A dropped message can leave a farm field or a road curve missing until that plot is drawn again. |
| `0` | Off. While the patch is switched off it adds nothing, and the game behaves exactly as without the patch. Use this only to find out whether a problem comes from a guard. |

The option only matters while the patch is switched off; with the patch on, the guards always run. The patch's log
(`vpproxy.log`) says at every start whether each guard is active and, if it is not, why. Two examples: a game
version the patch does not know, or a test DLL that brings its own guard.

Default line, as the patch writes it: `GuardsWhenOff = 1`.

### [Proxy] KeyboardHookThread

The game installs a keyboard hook for the whole Windows session, so that Alt+Enter does not switch its screen mode.
Windows runs that hook on the game's main thread, for every key pressed in every program. While the game's main thread
is busy (starting the game, loading a map, loading or leaving the game's interface), every key you press anywhere waits
for the game, so typing in other programs lags or loses keys. And when the game does not answer for 5 seconds, Windows
puts a "Not Responding" ghost window in its place, which can take the keyboard focus.

With this option the patch runs the game's hook on a thread of its own, which always answers at once. The game's rule
stays: Alt+Enter is held back while the game's window is in front. One change from the game: with another program in
front, Alt+Enter passes to that program (the game held it back in every program). The patch also tells Windows not to
show a ghost window for the game.

| Value | Meaning |
|---|---|
| `1` | On (the default). The hook runs on the patch's own thread; no ghost window. |
| `0` | Off: as the game does it. The hook runs on the game's main thread, and Windows shows a ghost window for a busy game. For diagnosis only. |

The option only matters while the patch is on; with the patch switched off the game is unchanged. The patch's log
(`vpproxy.log`) says at every start, in lines that start with `kbhook:`, where the hook runs.

Default line, as the patch writes it: `KeyboardHookThread = 1`.

### [Proxy] LeaderScenes

For the 43-civ variant of the patch. The game loads the 3D scene of an AI leader (the leader in their room, shown on the
diplomacy screen) only for the civilizations in slots 0 to 21, because it was made for at most 22 civilizations. The
leaders of the civilizations in slots 22 to 42 talk without their scene: the screen shows no 3D leader, and later
leader screens may show the game's debug text "THERE IS NO SCENE LOADED FOR THIS LEADER!!" at the top left.

With this option the patch asks the game to load those scenes too. Each scene costs the 32-bit game about 32 MB of its
4 GB of address space, for the rest of the game session. With 21 more scenes that is up to about 670 MB, which makes an
out-of-memory crash late in a large game more likely. That is why the option is off by default.

| Value | Meaning |
|---|---|
| `0` | Off (the default): as the game does it. Leaders in slots 22 to 42 have no 3D scene. |
| `1` | On demand: a leader's scene is loaded the first time their diplomacy screen opens. Only the leaders you meet cost address space. |
| `2` | All at load: every AI leader's scene in slots 22 to 42 is loaded when the game loads. Costs the most address space. |

With the standard (22-civ) variant every slot from 22 on is a city state, and the option does nothing. It works only
with the game build the patch knows; otherwise the patch loads nothing and says why in `vpproxy.log` (lines that start
with `leaderscene:`).

Default line, as the patch writes it: `LeaderScenes = 0`.

### [Logs] KeepRuns

How many of the patch's diagnostic logs stay on your disk. They live in the game's user folder (`Documents\My
Games\Sid Meier's Civilization 5\Logs`), in two places, and each keeps this many:

- **Run folders**, `Logs\VP64\run\<date>-<time>-...`: one for each start of the patch's server (a game start, and again
  after each change of mods in the same session). Each holds the logs of the proxy, the server and its renderer: about
  33 MB an hour of play. When a new one is made, the oldest go.
- **The compositor's older logs**, `Logs\shell_composite.<date>-<time>.log`: at every start of the game the previous
  `shell_composite.log` is kept under its date (about 25 MB an hour of play). The oldest go at the next start.

| Value | Meaning |
|---|---|
| `1` to `100` | Keep that many of each (the default is 10). The newest is always kept. |

The server also works on two copies of the game's database in its run folder (about 7 MB each). They are deleted when
the game ends normally, and kept after a crash or an error message, for the error report's root cause. Nothing else
is ever deleted: not your saves, not this file, not the error reports (`VP64\reports`), not the game's own logs.
`vpproxy.log` names at every start how many run folders are kept and why (`boot: run folders kept: ...`), and
`shell_composite.log` what it removed (`log: older logs: ...`).

Always on, without an option: the frame-pacing lines of the compositor, the server's render-pass statistics and the
proxy's call counters. They are the patch's base cost (decision D255): the first two under 0.01 % of a frame's
processor time, the counters under about 0.02 % of a turn, and together about 5.3 MB an hour of the logs above. The error report and the patch's developers need them to find
the cause of a stutter or a crash; switching them off would save no measurable time.

Default line, as the patch writes it: `KeepRuns = 10`.

### [Shell] WindowTitlePrefix

Text put in front of the game window's title while the patch runs, so that the taskbar, Alt+Tab and screen
recordings show that this game runs with the 64-bit patch: `[VP64] Sid Meier's Civilization V (DX11)`.

| Value | Meaning |
|---|---|
| `"[VP64] "` | The default. The title reads `[VP64] Sid Meier's Civilization V (DX11)`. |
| `""` | The game's own title, unchanged. |
| other text in double quotes | That text in front of the game's title: at most 64 characters (bytes of UTF-8), no control characters. |

Keep the double quotes: everything between them is used exactly as written, the space at the end included. Without
quotes the value is trimmed like every other value, so `WindowTitlePrefix = [VP64]` gives `[VP64]Sid Meier's ...`.

The patch puts the text in front once the game's window exists, and again if the game ever sets its own title back;
it never puts it in front twice. With the patch switched off (the off switch, or a fall back to Vox Populi's own
32-bit DLL) the title is the game's own; a fall back adds its notice at the end instead (` - VP64 64-bit patch OFF:
...`). The installer's options page shows this option as one box, "Show [VP64] in the window title": unticked, it writes
`WindowTitlePrefix = ""`; ticked, it keeps your own text, or the default when your file sets none. Any other text is
set in the file itself.

Default line, as the patch writes it: `WindowTitlePrefix = "[VP64] "`.

## Extras

Extras are optional changes to what the patch draws. They all live in one section, `[Extras]`, written last in the
file. Each extra belongs to one category, and each category has one switch:

| Category | Switch | Default | What it holds |
|---|---|---|---|
| Bugfixes | `EnableBugfixes` | `1` (on) | Fixes for things the game draws wrongly. Their names start with `Fix`. |
| Visual additions | `EnableVisualAdditions` | `0` (off) | New visual polish that the game does not have. |

An extra is used only when **both** its category's switch and its own line are `1`. A switch that is `0` switches off
every extra in its category, whatever their own lines say. Some extras need another one (a bridge fix needs the road
layout it builds on); such an extra is off while the one it needs is off, and its entry below says which one it needs.

- Fixes are on by default: an approved fix has its own line at `1`, so it is used unless you switch it or all fixes off.
- Visual additions are off by default. The patch's installer asks you whether you want them on its options page; if
  you tick them, it writes `EnableVisualAdditions = 1` into this file (the command `vp64helper visual-additions on`,
  beside the patch's DLL, does the same; `vp64helper visual-additions off` writes `0`, `vp64helper visual-additions
  status` shows the value). An extra's own line is written by the installer only when you change it on that page (or your file already sets it).
- To switch one extra off and keep the rest, set its own line to `0`. To switch a whole category off, set its switch to
  `0`.
- A new extra comes with its own line at `0` until it has been accepted for everyone. An extra whose name starts with
  `Experiment` is never switched on by default under that name.
- Extras change only what is drawn: never the game's rules, its saves, or what is sent to other players.

A third category, `EnableGameplayChanges`, is reserved for extras that would change rules or saved state. It does not
exist: the patch has no such switch and no such extra.

Each start lists every extra in the patch's logs (`vpproxy.log`, and the server's log): first both switches, then one
line per extra saying whether it is in effect and, if not, why (`options: extras: ...`).

### [Extras] EnableBugfixes

The switch of every bugfix extra.

| Value | Meaning |
|---|---|
| `1` | On (the default): every fix in `[Extras]` whose own line is `1` is used. |
| `0` | Off: every fix is off, whatever its own line says; the game draws as it always did. |

Default line, as the patch writes it: `EnableBugfixes = 1`.

### [Extras] EnableVisualAdditions

The switch of every visual addition.

| Value | Meaning |
|---|---|
| `0` | Off (the default): no visual addition is used, whatever its own line says. |
| `1` | On: every visual addition in `[Extras]` whose own line is `1` is used. The installer asks you about this. |

Default line, as the patch writes it: `EnableVisualAdditions = 0`.

### [Extras] VisibleAreaOutline

A visual addition: a thin line on the ground around the area your units and cities can see right now (the edge
between the plots in sight and the plots out of sight). Needs the fog of war, which the patch draws by default.

| Value | Meaning |
|---|---|
| `0` | Off (the default). |
| `1` | On, when `EnableVisualAdditions = 1` as well. |

Default line, as the patch writes it: `VisibleAreaOutline = 0`.

### [Extras] FixUnitPlacement

A fix for unit figures that sink into rock: on a mountain or a hill, the game sometimes puts part of a unit's figures
inside the ground (a horse's hooves, the lower part of a catapult's crew). With this fix the patch's server keeps them
out of the rock: a land unit at rest moves its group of figures to the flattest ground near its tile's centre (at most a
small step), and each figure stands on the highest ground under its feet. Needs `ServerUnits = 1` (the server draws the
units); while that is `0` this line does nothing, and the server's log says so in one line.

A unit in a fight keeps the game's own placement for the whole fight; when the fight is over, its figures can step up
onto the higher ground in one frame. Display only: the game's rules and saves are not touched.

| Value | Meaning |
|---|---|
| `0` | Off (the default for now). |
| `1` | On, when `EnableBugfixes = 1` as well (its default). |

Default line, as the patch writes it: `FixUnitPlacement = 0`.

### [Extras] GarrisonVisible

**A test value, not offered to players** (the maintainer's decision of 2026-11-23: none of its looks is better than the
game's own picture; `PerimeterGarrisons` below is the garrison option for players). The patch does not write this line
into a new options file and the installer's options page does not show it; the patch still reads it, for tests. While
`PerimeterGarrisons` is in effect this line draws nothing, and the server's log says so in one line.

A visual addition: a city's garrison is drawn at the city. The game shows no figure of the military unit stationed in a
city (only its flag beside the city's banner); with this addition the patch's server draws that unit's figures at the
city: at the south side of the city (or the next side of the city that is on land), facing out. Only military land units are drawn this way: a ship stationed in a city
is `ShipsMoored`'s, and workers, settlers, great people and aircraft standing in a city are drawn by the game as before.
A foreign city's garrison is drawn only where you see that city now, as its flag. While the unit is in a fight it is left
as the game shows it.

Needs `ServerUnits = 1` (the server draws the units); while that is `0` this line does nothing, and the server's log says
so in one line (`units extra GarrisonVisible: off: ServerUnits = 0`). The added figures cast a shadow like the other units the server draws.

| Value | Meaning |
|---|---|
| `0` | Off (the default): no garrison figure, as in the game. |
| `1` | On, when `EnableVisualAdditions = 1` as well. |

Default: `GarrisonVisible = 0` (not written into the file; add the line to set it).

### [Extras] ShipsMoored

A visual addition: ships in a city are drawn moored on the city's water side. The game draws no figure of a ship that
is in a city (only its flag beside the city's banner); with this addition the patch's server draws each such ship at
the edge of the city's tile where the water begins, several ships in a row along the shore. The value chooses how they
lie (the maintainer's choice of 2026-11-24, D250): `1` is look (d), the bow to the city and drawn a little smaller, several
ships side by side; `2` is look (b), alongside the shore, several ships end to end (where a row of them does not fit, each
ship lies on its own at the nearest water). A ship on a fort, citadel or feitoria used as a canal is drawn the same way
(the game draws it on land inside the walls). A foreign city's ships are drawn only where you see that city now. While a
ship is in a fight it is left as the game shows it; while it sails in or out it is drawn on its way and moors when it
arrives. A ship with no water near its tile (none within about one and a half tiles) is left as the game shows it.

Needs `ServerUnits = 1` (the server draws the units); while that is `0` this line does nothing, and the server's log
says so in one line (`units extra ShipsMoored: off: ServerUnits = 0`). When it is in effect the log names the look once
(`units extra ShipsMoored: look (d) BowToLandSmall (ShipsMoored = 1)`). The game draws no shadow under a moored ship,
and the ship's flag stays at its tile.

| Value | Meaning |
|---|---|
| `0` | Off (the default, D207): no ship figure in a city, as in the game. |
| `1` | Look (d): bow to the city, drawn at 0.8 of the game's size, several ships side by side; when `EnableVisualAdditions = 1` as well. |
| `2` | Look (b): alongside the shore, at the game's size, several ships end to end; when `EnableVisualAdditions = 1` as well. |

Default line, as the patch writes it: `ShipsMoored = 0`.

### [Extras] FixAirBases

A fix for aircraft at their bases. After a saved game is loaded, the game draws your aircraft in their city or on their
carrier; after an aircraft rebases during play, after its carrier moves, or after a fight on its tile, the game stops
drawing it (it hides every unit of a tile except the first whenever the tile's units change, and an aircraft is never
the first). With this fix the patch's server draws your and your team's aircraft in every case as the game draws them
after a load, at the same place. Display only: the game's rules and saves are not touched.

Needs `ServerUnits = 1` (the server draws the units); while that is `0` this line does nothing, and the server's log
says so in one line (`units extra FixAirBases: off: ServerUnits = 0`).

| Value | Meaning |
|---|---|
| `0` | Off: aircraft drawn as the game draws them. |
| `1` | On (the default), when `EnableBugfixes = 1` as well (its default). |

Default line, as the patch writes it: `FixAirBases = 1`.

### [Extras] FixUnknownWonderBase

A fix for natural wonders that the game does not know, such as one a mod adds. The game draws the ground of a natural
wonder from its own list of wonders; a wonder that is not on the list gets the ground of Mount Fuji, whatever the tile
is. With this fix the patch draws such a wonder's tile from its own ground (flat land, hills or a mountain, as the tile
is), when a game is loaded and when a mod places the wonder during play, so that the picture is the same before and
after a reload. The heights the game uses for placing units and for clicking on the map follow the picture. The wonder's
own model is not changed. Display only: the game's rules and saves are not touched. Vox Populi itself adds no such
wonder, so without such a mod this line changes nothing. A wonder on a coast or ocean tile, and one whose type the
server cannot name, keep the game's ground.

Needs `ServerRenderer = 1` (the server draws the terrain); while that is `0` this line does nothing. The server's log
has one line per load for it (`live terrain: load seq N: load-time pass ...`), with the time it took.

| Value | Meaning |
|---|---|
| `0` | Off: as the game draws it, on the ground of Mount Fuji. |
| `1` | On (the default), when `EnableBugfixes = 1` as well (its default). |

Default line, as the patch writes it: `FixUnknownWonderBase = 1`.

### [Extras] FixMinimapUnexplored

A fix for the game's minimap. When a mod or a script sets tiles back to unexplored during play (the main map covers them
in cloud again), the game's minimap keeps showing them, with their edges, and keeps the wider view they made, until the
next load. With this fix the patch marks those tiles unexplored in the minimap's own data within a few frames, so the
minimap shows what the main map shows and zooms back to the tiles you know. Tiles that only go from visible to seen are
not affected (the minimap draws both alike). Display only: the game's rules and saves are not touched. Vox Populi itself
hides tiles again only in its debug mode, so without such a mod or script this line changes nothing.

It works on the game's own minimap, with or without the patch's server renderer. If the patch does not recognise the
game's version, or the minimap's data are not as expected, the fix switches itself off with one line in `vpproxy.log`
(`minimap fog: OFF, ...`) and the minimap behaves as in the stock game.

| Value | Meaning |
|---|---|
| `0` | Off: as the game does it; hidden tiles stay on the minimap until the next load. |
| `1` | On (the default), when `EnableBugfixes = 1` as well (its default). |

Default line, as the patch writes it: `FixMinimapUnexplored = 1`.

### [Extras] FixSmoothWheel

A fix for the mouse wheel on a trackpad or a smooth (high-resolution) wheel. The game takes every wheel message as one
full step, whatever its size: on a trackpad or a smooth wheel Windows sends a notch as several small messages, about one
per frame while the movement lasts, so one light flick zooms the map three steps or more at 30 frames per second (more at
higher frame rates), while a fast spin of several notches that arrives as one message counts as a single step. With this
fix the patch adds the small movements up and lets one step through for each whole notch (120 units, the size of one
click of a notched wheel); a part of a notch is dropped when the wheel turns the other way or after 0.4 s without wheel
input, or when the game's window loses the focus. A message that carries several notches is handed to the game as that
many steps, one after the other. A notched mouse wheel behaves exactly as before. The same applies to every list and
panel that scrolls with the wheel.

It works on the wheel messages before the game sees them (an entry of the game's own import table; no code of the game
is changed). If the patch does not recognise the game's version, the fix switches itself off with one line in
`vpproxy.log` (`wheel: off: ...`) and the wheel behaves as in the stock game. With the patch switched off it is not used.

| Value | Meaning |
|---|---|
| `0` | Off: as the game does it; every wheel message is one step. |
| `1` | On (the default), when `EnableBugfixes = 1` as well (its default). |

Default line, as the patch writes it: `FixSmoothWheel = 1`.

### [Extras] AirInterceptPatrol

A visual addition: your and your team's aircraft on intercept duty are drawn flying a slow holding ring round their base
(city or carrier), with an arc for taking off when the order is given and one for landing when it ends; the game draws
them standing at their base. Another team's aircraft are never moved (the game shows no enemy orders). The ring's radius
is half the aircraft's intercept range from the game's unit table (`Units.AirInterceptRange`); a promotion that changes
the range is not counted, so with a mod that adds one the ring shows the base range only, and the server's log names the
mod's promotions in one line (`units extra AirInterceptPatrol: N promotion(s) change the intercept range ...`; Vox
Populi has none). The aircraft fly at the height the game uses for an air strike, in their standing pose (no flying
animation yet). Their unit flag, selection ring and shadow stay at the base, where the game has them. Display only: the
game's rules and saves are not touched.

Needs `ServerUnits = 1` (the server draws the units); while that is `0` this line does nothing, and the server's log
says so in one line (`units extra AirInterceptPatrol: off: ServerUnits = 0`).

Needs a game DLL that sends the patch's unit messages (built with `VP64_RENDER_EVENTS`): which aircraft are on intercept
reaches the server only through them. The DLL of this version does not send them yet (they come with the next DLL
update), so for now the line changes nothing, and the server's log says so once per session (`units extra
AirInterceptPatrol: no unit activity message (feed kind 1000) since the load: the DLL sends no render messages ...`).

| Value | Meaning |
|---|---|
| `0` | Off (the default). |
| `1` | On, when `EnableVisualAdditions = 1` as well. |

Default line, as the patch writes it: `AirInterceptPatrol = 0`.

### [Extras] PerimeterGarrisons

A visual addition, "Perimeter Garrisons": a city's garrison is drawn round the city. The game shows no figure of the
military unit stationed in a city (only its flag beside the city's banner); with this addition the patch's server draws
that unit's figures spaced evenly in a ring along the edges of the city's tile, facing outward, on every edge whose
neighbouring tile is land (no figure on an edge that faces water). The unit keeps its own number of figures: they are
spread out, none is added. A city on a one-tile island (no land edge) shows its garrison at the city's centre.

Only military land units are drawn this way: a ship stationed in a city is `ShipsMoored`'s, and workers, settlers, great
people and aircraft standing in a city are drawn by the game as before. A foreign city's garrison is drawn only where you
see that city now, as its flag. While the unit is in a fight it is left as the game shows it. The figures cast a shadow
like the other units the server draws. Display only: the game's rules and saves are not touched.

Needs `ServerUnits = 1` (the server draws the units); while that is `0` this line does nothing, and the server's log says
so in one line (`units extra PerimeterGarrisons: off: ServerUnits = 0`).

| Value | Meaning |
|---|---|
| `0` | Off (the default): no garrison figure, as in the game. |
| `1` | On, when `EnableVisualAdditions = 1` as well. The installer offers it on its options page, under the visual additions. |

Default line, as the patch writes it: `PerimeterGarrisons = 0`.

### [Extras] RoadLayout

A visual addition: used only when `EnableVisualAdditions = 1` as well; while that is `0`, the roads are drawn as with
`0`, whatever this line says. How the roads and railroads are laid out. The game draws a road as a bundle of curves between neighbouring road plots,
laid out anew each session: where several road plots touch, they form rings and loops of road around nothing, and the
curves wander. With `1` or `2` the patch's server lays the roads out itself, and the same map always gives the same
layout; trees are cleared along the patch's roads instead of the game's. It needs `ServerRenderer = 1` (the server draws
the roads); while that is `0` this line does nothing.

| Value | Meaning |
|---|---|
| `0` | As the game lays them out (the default for now). |
| `1` | Tidy: straight from plot centre to plot centre, short curves where a road turns, branches merging into the road they join, cities as hubs, and no ring of road unless it goes around something that has no road (a lake, a mountain, a farm). |
| `2` | All connections: every two neighbouring plots that both have a road or railroad are joined, as in Civilization III, so a missing link means there is no road there; laid out as `1` (straight runs, short curves, cities as hubs). `RoadJunction` and `RoadJitter` shape it. |

Bridges follow the patch's roads with `1` and `2`. The game builds each road or railroad bridge itself, along the curve
it laid out for that crossing; the patch's server turns and moves that bridge onto the point where the patch's road
crosses the same river edge, along the road, at the game's own height, and leaves out a bridge where the patch's layout
draws no road across that edge. This needs `ServerCities = 1` (the server draws the bridges); with `ServerCities = 0`
the game draws its bridges where it put them. A crossing where the game drew no bridge in this session (it lays its
roads out anew each session) has none with the patch's layout either. With `0` the bridges join the game's roads as in
the game. (Ticket 0808, 2026-11-26; until then, ticket 0806's note said bridges kept the game's placement with `1` and `2`.)

The layout stays as the game's by default for now (the maintainer's decision of 2026-11-20); switch it on with
`EnableVisualAdditions = 1` and `RoadLayout = 1` or `2`. Until 2026-11-23 value `1` counted as a fix (under
`EnableBugfixes`); the maintainer made the whole option a visual addition, beside `RoadJunction` and `RoadJitter`. Until 2026-11-22 this option was called `FixRoadLayout` and had only `0` and `1`; it
was never in a release, so the old name is not read (a file that has it gets the "unknown option" warning).

Default line, as the patch writes it: `RoadLayout = 0`.

### [Extras] RoadJunction

A visual addition that shapes `RoadLayout = 2` (all connections): what the centre of a plot looks like where four to six
roads meet. It does nothing unless `RoadLayout = 2` is in effect (which needs `EnableVisualAdditions = 1`). Cities are
always hubs, whatever this says.

| Value | Meaning |
|---|---|
| `0` | Through (the default): one road passes through the centre and the others merge into it, as with `RoadLayout = 1`. |
| `1` | Cross: the roads cross in the centre; each two opposite roads are one straight line, a road without an opposite one ends in the centre. |
| `2` | Ring: a small roundabout in the centre, every road ending on it. |
| `3` | Rosette: each two neighbouring roads are joined by a short curve that bends round the centre; nothing passes through the centre itself. |

The default is provisional: the maintainer picks the treatment from the preview sheets (cross, ring, rosette), and the
default changes then.

Default line, as the patch writes it: `RoadJunction = 0`.

### [Extras] RoadJitter

A visual addition that shapes `RoadLayout = 2` (all connections): how much the roads wobble from plot to plot, so that
they run tile to tile but not ruler-straight, as in Civilization III. Each plot's crossing point is moved by a fixed small
amount that depends only on the plot, so the same map always looks the same. It does nothing unless `RoadLayout = 2` is
in effect (which needs `EnableVisualAdditions = 1`).

| Value | Meaning |
|---|---|
| `0` | None (the default): straight from plot centre to plot centre. |
| `1` to `7` | Each plot's crossing point moved by up to that many of the map's height samples (a plot is 64 wide): `1` a little, `7` the most. |

The default is provisional: the maintainer picks the amount from the preview sheets, and the default changes then.

Default line, as the patch writes it: `RoadJitter = 0`.

## Format rules

The file follows the usual INI rules:

- `[Section]` lines group the options. Section and option names are not case-sensitive.
- One option per line: `Name = value`. Spaces around the name and the value do not matter.
- A line that starts with `;` or `#` is a comment. A `;` or `#` after a space ends the value, so
  `BakedTileCameraRadius = 5 ; less memory` works.
- Values are whole numbers, except `[Shell] WindowTitlePrefix`, which is text. Quotes around a value are allowed
  (`"5"`); around a text value they keep it exactly as written, spaces, `;` and `#` included.
- Save the file as plain text (UTF-8 or ANSI, Notepad's default). A file saved as "Unicode" (UTF-16) by older Notepad
  versions is read too. The file must be smaller than 64 KB.

The patch never stops because of a mistake in this file. Whatever it cannot use is written to its log, and the
default is used instead:

- an option name it does not know is ignored;
- a value that is not a whole number, or is outside the allowed range, gives the default;
- a text value without its closing quote, with text after the closing quote, longer than 64 bytes, not valid UTF-8 or
  with a control character gives the default;
- an option written twice: the last line counts;
- an option under the wrong `[Section]`, or above the first section, is still read (so every option name is unique
  across the whole file);
- any other line that is not a comment, a `[Section]` or `Name = value` is ignored.

The game's interface drawing buffers are always enlarged while the patch is on (four times the game's 1 MB and
4,096 commands, so that about 450 unit flags in view no longer crash the game; the patch's own checks of the
game build still decide whether it can be done, and `vpproxy.log` says so in lines that start with `uidraw:`).
This was the option `[Renderer] UiDrawListGrowth` until 2026-11-22; a line for it is now an unknown name, logged
and ignored.

The log is `vpproxy.log` in the newest folder under `Logs\VP64\run\` in the game's user folder (`Documents\My Games\Sid
Meier's Civilization 5`; before issue 0148 it was `VP64\run\` next to the patch's DLL). Each start lists every option
with its value and whether it came from the file or is the default.

## For testers

Settings that exist only for testing the patch (the proxy's `vpproxy.ini`, the server's `server.cfg`, marker files)
are not in this file and are not described here; see `proxy/README.md` and `server/README.md`.
