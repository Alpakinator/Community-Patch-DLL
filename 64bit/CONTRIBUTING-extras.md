# Contributing an extra

This guide is for anyone who wants to add an *extra* to the 64-bit patch: an optional change to what the patch draws,
switched by one line in the patch's options file. It says what counts as an extra, the rules every extra follows and
the command that checks each rule, and how approval works. It also walks through a complete small example, from the
options entry to the pull request.

Paths below are relative to the folder that holds this guide.

## 1. What an extra is

The 64-bit patch moves the game's world rendering into a 64-bit process, the *server*. With every extra off, the server
aims to draw the same picture as the unpatched game, pixel for pixel. An extra is a deliberate, optional difference
from that picture:

- it changes **only what is drawn**: never the game's rules, its state, its saves, or anything sent between
  multiplayer players;
- it is switched by **one line** in `VP64Options.ini`, in the section `[Extras]`;
- with its line off, the picture is exactly what it was before the extra existed.

Every extra belongs to one **category**, and each category has a switch. An extra is in effect only when its category
switch **and** its own line are both on. A category switch that is off wins over every line in that category.

| Category | Switch | Default | What it holds |
|---|---|---|---|
| Bugfixes | `EnableBugfixes` | `1` (on) | Fixes for things the game draws wrongly: roads with loops, buildings standing on a road. Names start with `Fix`. |
| Visual additions | `EnableVisualAdditions` | `0` (off) | New visual polish that the game does not have. The installer asks the player whether they want these. |

A third category, `EnableGameplayChanges`, is **reserved**. It would hold changes to rules or saved state, or anything
else that is not display-only. It does not exist: the patch has no such switch, and no such extra is accepted until
its design (a marker in saves, a rule for multiplayer players) has been worked out and agreed. The name is reserved so
that the word "gameplay" keeps that meaning.

Experiments are extras whose names start with `Experiment`. They belong to the category of what they show, are always
off by default, and are never approved under that name: an experiment that is accepted is renamed when it becomes a
feature.

### 1.1 What is not an extra

- **A bug in the patch itself**, for example the server drawing something differently from the unpatched game. That is
  an ordinary fix with no option: the server's goal is the unpatched picture.
- **A crash or a freeze**, also one that the unpatched game has. Fix it without a flag.
- **Anything that changes the game**: rules, balance, AI, what the player can know, saves, multiplayer messages. That
  belongs in the game DLL or a mod, not in the patch's extras.
- **A change to the game's user interface** (screens, popups, the minimap). The patch leaves the interface to the
  game.
- **A performance option** that does not change the picture. Such options live in the `[Renderer]` or
  `[Performance]` sections and follow the general procedure for an option (`options/README.md`).

## 2. The rules

Every extra follows these twelve rules. They apply to the project's own extras exactly as to anyone else's.

| # | Rule | How it is checked |
|---|---|---|
| E1 | **Display only.** An extra never changes the game's rules, its state, its saves or what is sent between multiplayer players. | The same save played with the flag on and off gives identical game statistics (2.1). |
| E2 | **Off is unchanged.** With the flag off, the picture is what it was before the extra existed. | The offline scene harness draws its scenes with the flag off and compares them with the server's own pictures recorded before the change (2.2). |
| E3 | **One flag, one table entry, one document entry.** Each extra has one row in the options table and one entry in `docs/OPTIONS.md` with its default. | The options unit test (2.3). |
| E4 | **Default off until approved.** A new extra is merged with its default at 0. Only the maintainer changes a default (section 6). | The options unit test fails a non-zero default that is not on the approved list (2.3); review. |
| E5 | **Independent.** An extra works with any combination of the other flags, or names in its table row the extra it needs; while the needed extra is off, it is off too, and the log says why. | The extra's unit test, alone and with every other extra on; the options test for the `needs` chain (2.4). |
| E6 | **Failure falls back to the unchanged picture.** An extra that cannot work (a missing file, an input that never arrives, an unknown unit) switches itself off for the session with one log line, and the picture is drawn without it. It never stops the game and never draws half of itself. | A unit test that takes the extra's input away (2.4). |
| E7 | **Nothing hidden is shown.** An extra shows only what the game's own interface already lets that player know. | Review; where in doubt, a picture of what the unpatched interface shows. |
| E8 | **Its cost is stated.** Frame time, seconds per turn and memory with the flag on and off. An extra that costs more than 1 % of a turn says why it is worth it. | Measured runs per arm (2.5). |
| E9 | **Pictures come with it.** On and off pictures of fixed scenes from the offline scene harness, kept as its regression test. | The harness (2.2). |
| E10 | **No game files.** Game art is read from the player's own installation at run time. Nothing taken from the game's files or its executable is committed. New art made by a contributor is the contributor's own work, published under this repository's licence (GPL-3.0-or-later), with its origin named. | Review; the content check (2.6). |
| E11 | **Messages go one way.** An extra may receive messages from the game DLL (section 4); it never calls into the DLL and never reads a DLL object from the render thread. | The renderer's code is compiled without the DLL's headers, so a call does not build; review (2.6). |
| E12 | **One extra per pull request,** with its flag, its document entry, its pictures and its E1 result. | Review, with the pull request template (section 7). |

Two consequences for testing. Comparisons with the unpatched game set **both** category switches to 0, because bugfixes
are on by default and so the default picture differs from the unpatched one on purpose. A player's normal game runs
with the defaults.

### 2.1 E1: the game plays the same

The check uses the DLL's statistics log (`stats.db`, written by the game DLL when its `SQLITE_LOGGING` option is on) and
`tools/d10/statscmp.py`, which compares every table and column of two logs except process measurements.

1. Copy a save. Play it for at least 10 turns with the patch, the extra's flag at 1 and its category switch at 1.
   Keep that game's `stats.db`.
2. Load the same save again and play the same number of turns with the flag at 0. Keep that `stats.db`.
3. Compare, counting only the turns after the save's turn `T`:

   ```
   python -I tools/d10/statscmp.py ON/stats.db OFF/stats.db --from-turn T --expect-turns T T+10
   ```

   Exit 0 and `IDENTICAL` is the pass. Compare like with like: both games windowed, or both without a window; a game
   played without a window and one played with a window may differ for reasons that have nothing to do with the extra.

An extra that lives only in the server's renderer (section 3) cannot reach the game state, because the renderer runs
on its own thread, reads only the event feed, and is compiled without the DLL's headers. The statistics comparison
is still required: it catches a change made elsewhere by mistake.

An extra that adds a message send to the DLL needs one more check: the DLL built **with** and **without** the define
`VP64_RENDER_EVENTS` plays the same game (section 4.4).

### 2.2 E2 and E9: the offline scene harness

The offline scene harness replays a *scene bundle*, a recording of everything the server's renderer received over a
few frames of a real game, and draws it again without the game. The player is `sceneshot`
(`server/tools/sceneshot/README.md`); the bundle format and the recorder are described in the harness's own notes.

**Bundles hold data read from your copy of the game** (terrain textures, the overlay the game draws, its shaders' output).
Never attach a bundle, or a picture made from one, to a pull request or an issue. Record your own bundles from your own
installation, and describe your pictures in words, or keep them for the review in a place the maintainer can see
privately.

Build the player:

```
python -I server/tools/sceneshot/build_sceneshot.py --out <work>/ss
```

Record a bundle (once per scene): run the patch with these lines in the server's `server.cfg` and play to the scene you
want; the recorder writes the bundle into the empty folder:

```
render_test_record=<absolute path of an empty folder>
render_test_record_after=900
render_test_record_frames=8
render_test_record_dump=all
```

Then, for each bundle and each named camera of it:

- **E2, off is unchanged:** with the extra off, the player's pictures must equal the server's own pictures stored in the
  bundle (depth byte-equal; colour at most 1 pixel off by at most 1 in one channel, the harness's standing tolerance):

  ```
  <work>/ss/bin/sceneshot.exe --bundle <bundle> --dumped --compare --tolerance 1:1 --option <YourFlag>=0 --out <work>/off
  ```

- **E9, the pictures:** the same camera with the flag on and with it off:

  ```
  <work>/ss/bin/sceneshot.exe --bundle <bundle> --camera <name> --option EnableVisualAdditions=1 --option <YourFlag>=1 --out <work>/on
  <work>/ss/bin/sceneshot.exe --bundle <bundle> --camera <name> --option EnableVisualAdditions=1 --option <YourFlag>=0 --out <work>/off
  ```

  (For a bugfix, set `EnableBugfixes` instead.) The two pictures are the extra's evidence. Your test keeps the scene
  list and the cameras, so that a later change to the renderer can draw them again and compare.

`--option KEY=VALUE` sets one `[Extras]` line of `VP64Options.ini` for the player, as if the file held it; repeat it for
several lines. Every other option keeps its default. A key outside `[Extras]`, or a value the options file would not
take, is refused.

### 2.3 E3 and E4: the options test

```
python ipc/build.py --target test_options
build/x86/Release/options/test_options.exe docs/OPTIONS.md build/x86/Release/options/scratch
build/x64/Release/options/test_options.exe docs/OPTIONS.md build/x64/Release/options/scratch
```

Both must end with every check passed. The test fails, among other things, when a key has no entry in
`docs/OPTIONS.md`, when an `[Extras]` key breaks the naming rules (5.1), when a `needs` chain has a cycle or leaves
`[Extras]`, and when a feature's default is not 0 and the key is not on the approved list (`kApprovedDefaults` in
`options/tests/test_options.cpp`, which only the maintainer extends).

### 2.4 E5 and E6: the extra's own unit test

Each extra has a unit test under `server/tests/extras/`. It must cover, at least:

- the extra **alone** (only its category switch and its own flag on) and with **every other extra on**;
- the extra **off** by each of its causes: its own flag 0, its category switch 0, the extra it needs off; in each case
  it draws nothing and says why in one line;
- its input **taken away** (E6): the input missing at the start, and lost in the middle of a session. The extra must
  switch itself off with one log line, keep the frame going and draw nothing of itself;
- **negative cases** for every check: a test that would also pass if the extra did nothing proves nothing, so each
  check has a counterpart that must fail or must differ.

Native code also runs its tests once under AddressSanitizer (each test runner's `--asan` option).

### 2.5 E8: the cost

State, with the flag on and off, on the same machine and the same scene or save:

- **frame time** of the server's draw: the player's timing run over your bundles,
  `sceneshot.exe ... --no-pictures` repeated at least 5 times per arm, median and range;
- **seconds per turn** of a late game (an extra that reacts to game events can cost time per turn as well as per
  frame), at least 3 runs per arm, median and range;
- **memory**: the server's peak working set, on and off.

Give the numbers, the machine, the number of runs and why they moved. An extra whose cost is within the noise says so
and shows the noise. An extra that costs more than 1 % of a turn explains why it is worth it.

### 2.6 E10 and E11: what review checks, and what the build checks

- **E10:** the pull request adds no file taken from the game (textures, models, sounds, shader binaries, text) and no
  picture of the game. New art you made is named in the pull request, with where it came from, and is published under
  GPL-3.0-or-later. Before you open the pull request, look at the list of files your branch changes
  (`git diff --stat <the branch you started from>`): no picture, save, log, dump or capture file, and no path into
  your own user folder, in a file or in a file name. The maintainer runs the repository's content check on every
  pull request before it is taken in; it fails on pictures, saves, logs, dumps, private paths and the maintainer's own
  list of private names. The check is not part of this folder, because it reads the file list of the maintainer's
  repository, laid out differently from this one, and that private list.
- **E11:** the server's renderer (`server/src/render/`, `server/src/extras/` and the modules under `render/`) is
  compiled without the DLL's include path, so code there cannot call the DLL. An extra reads the game only through the
  event feed (`server/src/feed/srv_feed.h`) and the tables the feed copies; review checks that it adds no other way in.

## 3. How an extra fits into the server

```
 game DLL (game thread)                         server's renderer (render thread)
 ---------------------                          ---------------------------------
 stock notifications to the game's EXE  --+
                                          +-->  event feed  --drain-->  scene: fog, units, trees, ..., extras
 render messages (VP64_RENDER_EVENT)    --+     (one ordered queue,       each frame: draw
                                                plot and unit tables)
```

- The **event feed** (`server/src/feed/srv_feed.h`) is the renderer's only input from the game. It carries every
  notification the DLL sends to the game's executable (a plot changed, a unit moved, the fog changed), the DLL's own
  render messages (section 4), and copies of the plot, unit and player tables. Each event has a kind, a plot and four
  values; the kinds and their values are listed in that header.
- The **scene** (`server/src/render/srv_render_scene.h`) takes each drained batch of events and draws each frame. The
  extras module (`server/src/extras/`) is one of its parts: the scene asks it to read each batch and to draw at its
  place in the frame.
- The **options** (`options/`) are read once at the server's start. An extra asks for its effective state (5.2) once
  and keeps the answer; it does not read the file again during a game.
- The **offline scene harness** feeds the same scene from a recording, which is why an extra that reads only the feed
  can be developed and tested without starting the game.

## 4. Messages from the DLL: adding a message kind and a send site

Most extras can work from what the feed already carries. When an extra needs a state that the game never tells its
executable (a unit's air patrol, a unit loaded onto a carrier), the DLL can send a **render message**.

### 4.1 The rules for a send site

- It sits **after** the state change it reports.
- It passes **values**, never pointers.
- Its arguments are computed from members and plain getters only, with **no side effect** (no call that can fire a
  game event, change a cache or touch a random number generator).
- It **never waits** and never reads an answer.
- It is compiled only with the define `VP64_RENDER_EVENTS`. In a build without it, the source the compiler sees is the
  same as before apart from line numbers, so the 32-bit DLL is unchanged.

### 4.2 Adding a kind

1. **The DLL side.** In `CvGameCoreDLL_Expansion2/VP64RenderEvents.h`, add a define at the end of the list of kinds,
   with the next free number in 1000-1999 and a comment giving the meaning of the plot and of each of the four values.
   A number is never reused, and a kind's values never change meaning; a kind that needs other values is a new kind.
2. **The server side.** In `server/src/feed/srv_feed.h`, add the same number to `enum Kind`, after the last extras kind,
   with the same comment. In `server/src/feed/srv_render_events.cpp`, add the `static_assert` that ties the two
   numbers together, so that a server built against a DLL source with other numbers does not compile.
3. **The version.** `VP64_RENDER_EVENTS_VERSION` (DLL) and `kVersionExpected` (server) change only when an existing kind
   changes meaning or is removed. Adding a kind at the end does not change it: an older server ignores a kind it does
   not know, and counts it.
4. **The unit test** of the render messages (`server/tests/feed/run_render_events_test.py`) covers the new kind: it
   arrives in the feed with its values, in order.

### 4.3 Adding a send site

In the DLL source, include the header once near the top of the file, inside the define:

```cpp
#if defined(VP64_RENDER_EVENTS)
#include "VP64RenderEvents.h"
#endif
```

and put each send in its own block, right after the change it reports. This is the site in `CvUnit::SetActivityType`,
after the new value is stored (the function reaches it only when the value changes):

```cpp
	m_eActivityType = eNewValue;
#if defined(VP64_RENDER_EVENTS)
	VP64_RENDER_EVENT(VP64_RENDER_EVENT_UNIT_ACTIVITY, (pPlot != NULL) ? pPlot->GetPlotIndex() : -1,
		getOwner(), GetID(), eNewValue, eOldActivity);
#endif
```

Think about the **starting state** too. A message reports changes; a renderer that starts with a loaded game also needs
the state as it is at the load. The existing kinds send it from `CvUnit::setupGraphical`, which runs for every unit
when the game sets it up for display, with the old value equal to the new one so that a consumer can tell the two
apart. A new kind that describes a lasting state does the same, or says why it does not need to.

### 4.4 Checking that the DLL plays the same

1. Build the DLL with the define and without it (`--no-render-events` on the DLL build).
2. Play the same game without a window with each: a new game of at least 100 turns on a small map, and a save loaded,
   saved at once and played for 20 more turns.
3. Compare: `tools/d10/statscmp.py` on the two `stats.db` files, and `tools/d10/savecmp.py` on the saves written at the
   load and at the end. Both must say `IDENTICAL`. (Two new games never give byte-equal saves, even with one DLL: the
   game's identifiers are not drawn from its seeded random numbers. So the save check uses a loaded save.)
4. For the 32-bit build, compare the preprocessed source of every changed file at the commit before yours and at yours,
   with the 32-bit build's flags: the text must be equal except for line numbers.

## 5. Adding a flag

### 5.1 The table row

1. Ask for an option id in your issue (section 7). Ids are given out by the maintainer so that two pull requests never
   take the same one.
2. Add the id before `kCount` in `options/include/vp64_options.h`, and its row in `kDefs` in
   `options/src/vp64_options.cpp`:
   - section `Extras`;
   - the key: a bugfix starts with `Fix`; a visual addition starts with neither `Fix` nor `Enable`; an experiment
     starts with `Experiment`. Every key is unique across the whole file, without regard to case, because the loader
     honours a key in any section;
   - type, **default 0**, and a range that holds 0 (0 always means off, the unchanged picture). Most extras are 0-1; an
     extra may take a number where a number is the honest form of the choice (for example a duration in seconds, with
     0 meaning the game's own length);
   - help lines for the default file, each under 100 characters;
   - the category: `kCatBugfix` or `kCatVisual`;
   - `needsKey`: the key of the extra it needs, or `NULL`.
3. Add its entry in `docs/OPTIONS.md` under `## Extras`, in the form of the others: what the player sees, the value
   table, what it needs, and the line `Default line, as the patch writes it: \`YourFlag = 0\``.

### 5.2 Reading it

The extra's code asks for the **effective state**, never the raw value:

```cpp
const Vp64Opt::ExtraState s = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kYourFlag);
if(!s.on)
	return;   // s.why says which line switched it off
```

`Extra` combines the category switch, the flag and the needed extra. The server's start log already lists every extra
with its state and the reason (`options: extras: ...`); your code adds a log line only for what the options cannot
know, such as an input that is missing at run time (E6).

## 6. Approval

- **A new extra is merged with its default at 0** and stays off for every player until it is approved. Players who want
  to try it set its line to 1 by hand (and, for a visual addition, `EnableVisualAdditions = 1`).
- **Only the maintainer approves an extra,** from its pictures, its numbers, and where needed a game played with it.
  Approval changes the default to 1 in a separate, small pull request that also adds the key to `kApprovedDefaults`.
- **What approval means for players:** the options file is written with every line commented out, so an untouched line
  follows the current default. A player who switched the category on gets an approved visual addition at the next
  update; a player who set the line by hand keeps their own value.
- **The installer** writes only `EnableVisualAdditions = 1`, when the player says yes to visual additions. It never
  writes an extra's own line.
- An extra can be **declined**, or **retired** later (for example when the game's own drawing it builds on is replaced).
  A retired key is left as an empty row and its id is never given again.

## 7. Proposing an extra, and the pull request

1. **Open an issue first** with the form "Propose an extra". Say what the player would see, which category it belongs
   to, what the extra reads (an existing feed kind, a table, or a new render message), and why it is display-only.
   Ask for an option id there. Agreeing on these before you write code avoids a pull request that cannot be taken.
2. **One extra per pull request** (E12): the table row, the `docs/OPTIONS.md` entry, the code, the unit test, the scene
   list for the harness, and the evidence for E1, E2, E8 and E9 in the description, following the pull request template.
   A new render message goes in the same pull request as its first extra, with its DLL change.
3. **Licence:** everything you contribute is published under GPL-3.0-or-later. Code taken from another project names
   that project, its path and its version, and only if its licence is compatible.

## 8. A worked example: `VisibleAreaOutline`

This section follows one small extra through every step. It is a real extra in the patch, merged with default 0 and
not approved; it exists to be read.

**What the player sees.** A thin, light line on the ground along the edge of the area the player can see right now:
between the plots in sight and the plots that are revealed but out of sight. The unpatched game already shows the
difference as a darker shade on the plots out of sight; the line makes the edge easier to read.

**Why it is display-only (E1, E7).** It reads only the active player's own visibility, which the game already shows.
It changes nothing in the game.

### 8.1 The options entry

The row in `options/src/vp64_options.cpp` (id 22, `kVisibleAreaOutline` in `options/include/vp64_options.h`):

```cpp
{"Extras", "VisibleAreaOutline", kTypeInt, 0, 0, 1,
	"A thin line on the ground around the area your units and cities can see now.\n"
	"  0 = off (default). 1 = on (also needs EnableVisualAdditions = 1).",
	kCatVisual, NULL},
```

and its entry in `docs/OPTIONS.md`:

```
### [Extras] VisibleAreaOutline

A visual addition: a thin line on the ground around the area your units and cities can see right now (the edge
between the plots in sight and the plots out of sight). Needs the fog of war, which the patch draws by default.

| Value | Meaning |
|---|---|
| `0` | Off (the default). |
| `1` | On, when `EnableVisualAdditions = 1` as well. |

Default line, as the patch writes it: `VisibleAreaOutline = 0`.
```

### 8.2 Its input: an existing feed kind

No new render message is needed. The fog of war changes with the feed's kind 3 (`kFowChanged`: the DLL's notification
that a plot's visibility changed for the active player), and the scene's fog module already keeps every plot's state
(`FowScene::states()`: visible, seen, unexplored) and a generation number that rises with every change. The extra reads
those two values; it reads no table of its own, so the harness's recordings need no new member.

### 8.3 The code

All of it is in `server/src/extras/`:

- `srv_extras.h/.cpp`: the extras module of the scene. At the start it asks `Vp64Opt::Extra` for each extra it holds.
  The server's start log already lists every extra's state and the reason, so the module adds a line only for what
  the options cannot know (the fog switched off, an input missing at run time). Each frame it calls each running extra.
- `srv_extra_visible_outline.h/.cpp`: the extra itself.
  - **Building the edges:** when the fog's generation changes, it walks every plot that is visible and, for each of its
    six neighbours that is revealed but out of sight, takes the hex edge they share (an unexplored neighbour lies
    under the fog's clouds and gets no edge). An edge's two ends are the centres of three
    neighbouring plots averaged, so no hex size is assumed. Each edge becomes a flat ribbon on the ground, with its
    heights read from the server's height field.
  - **Drawing:** after the terrain, the water and the ranged-attack arc, before the trees and the units (so that units
    stand on the line), with the scene's depth, alpha-blended, once per copy of the map where the map wraps.
  - **Falling back (E6):** without the fog (switched off, or failed), or without a height field, it draws nothing and
    says so once in the log. If its own drawing fails, it says so once and switches itself off for the session.
- The scene makes the module once, at its start, only when an extra is in effect, and calls it at two places: right
  after the fog has read each feed batch, and at its place in the frame (`extrasDraw`). A program that does not link
  `server/src/extras/` gets no module and draws as before.

### 8.4 The tests

`server/tests/extras/` (run by `run_extras_test.py`, also with `--asan`):

- the edge builder on small hand-made maps: one visible plot (six edges), a visible area with a hole, a map that wraps
  (edges across the seam), no visible plot (no edge), every plot visible (no edge); and negatives: a seen plot next to an
  unexplored one gives no edge, a visible plot next to an unexplored one gives no edge, a table of the wrong size is
  refused;
- the effective state: off by its flag, off by `EnableVisualAdditions = 0`, on with both, unchanged by every other
  extra on;
- the fallback: no fog at the start; the fog lost during a session; a draw that fails;
- the harness (`test_visible_outline_scenes.py`): the scenes below, drawn with the extra off (E2: equal to the recorded
  pictures) and on (E9: in effect, and different from the recording).

### 8.5 The pictures

From the harness, at the recorded frames of two scenes, with the extra on and off: a huge map late in a game (380
edges; about 14,600 of 3.6 million pixels change in each of its 8 frames), and a small map early in a game (no plot is
revealed but out of sight yet, so nothing is outlined and on equals off). In the pull request they are described in words, and kept privately for review.

### 8.6 The pull request

```
Add the visual addition VisibleAreaOutline

What it shows: a thin line on the ground along the edge of the area the player can see now.
Category: visual addition. Default 0 (not approved). Needs: none (the fog of war, which is on by default).
Reads: the fog's per-plot state, kept by the scene from feed kind 3 (kFowChanged). No new render message.

E1: statscmp on vs off from one save: not run for this example yet (it needs a game; see 2.1).
E2: sceneshot --dumped --compare --tolerance 1:1 with VisibleAreaOutline=0: every frame of four bundles byte-equal (8 of 8 each).
E3/E4: test_options 673 passed (x86 and x64), default 0.
E5/E6: run_extras_test.py 83 passed, also under ASan.
E7: shows only the active player's own visibility, already shown by the game as the darker shade.
E8: the harness's time per frame on the huge scene, 5 runs per arm alternating, 35 frames each: off median 52.4 ms
(39.7-67.7), on 50.9 ms (40.3-696.0, one outlier); no difference beyond the noise of a busy machine. Edge rebuild on a
fog change: 0.21-0.38 ms (5 runs, 380 edges). Memory: one 256 KB vertex buffer. Seconds per turn: no work is added to
the game's thread.
E9: on and off pictures of two scenes, kept for the review.
E10: no game files, no pictures, saves or logs in the change (git diff --stat read).
E11: reads the feed only; no DLL header.
E12: this extra only.
```
