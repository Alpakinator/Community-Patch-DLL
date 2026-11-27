// vp64_options.cpp - the options file of the 64-bit patch (one-off 9042, decisions D89 and D88). Hand-written.
// Built into the x86 proxy (proxy/build_proxy.py), the x64 server (server/build_server.py) and the unit tests of both
// trees (options/CMakeLists.txt, run by `python ipc/build.py --test`). No STL, no C++11 library header, no exceptions.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp64_options.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Vp64Opt
{
namespace
{
// ================================================================================================ the table
// Help lines are written into a new file as comments; keep them short (under about 100 characters) and plain ASCII.
// Every row with a key fills every field up to its title (one-off 9081: the installer's options page shows it; the
// owner's wording, one place); valueCategory (ticket 4940) only on a row with a category per value (none since one-off
// 9081: RoadLayout, the only one, became a plain visual addition, the owner 2026-11-23). A
// reserved row (an empty key) leaves the trailing fields out on purpose (zero: kCatNone, no needsKey, no title, no
// valueCategory); clang's -Wextra (clang-cl /W4) would call that a missing initializer.

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
const Def kDefs[kCount] = {
	{"Renderer", "BakedTileCameraRadius", kTypeInt, -1, -1, 128,
	 "How much of the map's terrain the patch keeps baked and ready to draw, so that no tile\n"
	 "shows gray after you pan or jump the camera.\n"
	 "  -1 = prevent gray tiles by caching the whole map at the cost of resource usage (default).\n"
	 "      Uses the most video memory: about 400 MB on a huge map.\n"
	 "  0 to 128 = only the tiles within that many rings (hexes) of the screen. Uses less video\n"
	 "      memory; tiles further away are baked when the camera comes near them.\n"
	 "      5 is a good value when video memory is short.",
	 kCatNone, NULL, "How much of the map's terrain stays baked"},
	{"Renderer", "ServerRenderer", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the map's terrain (experimental).\n"
	 "  0 = off (default): the game draws the map itself.\n"
	 "  1 = on: the server draws the terrain and the patch's compositor puts it into the\n"
	 "      game's picture. Needs shell_composite.dll and shell_composite.sha256 beside the\n"
	 "      patch's VP64 folder; if they are missing or do not match, the game draws the map.",
	 kCatNone, NULL, "Server draws the map's terrain"},
	{"Renderer", "WhiteFogMask", kTypeInt, 1, 0, 1,
	 "Whether the patch gives the game's buildings, wonders, improvements, resources and trees\n"
	 "their textures when the game's own fog-of-war drawing is switched off.\n"
	 "  1 = on (default): the patch loads one small white texture with the game's own loader and\n"
	 "      uses it as the fog mask of those models. Works only with this exact game build;\n"
	 "      otherwise the patch does nothing and says why in vpproxy.log.\n"
	 "  0 = off: nothing changes; with the game's fog-of-war drawing off, those models draw dark red.",
	 kCatNone, NULL, "Textures for models when fog drawing is off"},
	// id 3: was [Renderer] UiDrawListGrowth (ticket 0487, default 1). The owner, in person, one-off 9081, 2026-11-22:
	// no longer an option; the proxy always grows the EXE's UI draw buffers (proxy_uidraw.cpp keeps its own build and
	// byte checks).
	// Reserved for ever (ids never renumber); a player's "UiDrawListGrowth = ..." line is an unknown key now.
	{"Renderer", "", kTypeInt, 0, 0, 0, ""},
	{"Renderer", "ServerUnits", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the units too (experimental; needs ServerRenderer = 1).\n"
	 "  0 = off (default): the game draws the units itself.\n"
	 "  1 = on: the server draws the units. While a fight is shown, the fighters stand still.\n"
	 "      Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server draws the units"},
	{"Renderer", "ServerArc", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the red arc of an aimed ranged attack\n"
	 "(experimental; needs ServerRenderer = 1).\n"
	 "  0 = off (default): the game draws the arc itself.\n"
	 "  1 = on: the server draws the arc and the game's own is switched off.\n"
	 "      Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server draws the ranged-attack arc"},
	{"Renderer", "ServerPath", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the markers of a unit's planned move (the turn\n"
	 "discs, the chevrons between them and the ring on the destination; experimental; needs\n"
	 "ServerRenderer = 1).\n"
	 "  0 = off (default): the game draws the markers itself.\n"
	 "  1 = on: the server draws them and the game's own are switched off.\n"
	 "      Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server draws the planned-move markers"},
	{"Renderer", "ServerFarmFields", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server lays out and draws the farm fields and the tracks\n"
	 "between them (experimental; needs ServerRenderer = 1).\n"
	 "  0 = off (default): the game lays out the farm fields itself.\n"
	 "  1 = on: the server draws them, the same fields on every load of a save, and the game's\n"
	 "      own farm-field layout is no longer run. Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server lays out and draws the farm fields"},
	// ticket 0566 (D191): default 0 (was -1, stock pacing, until 2026-11-19)
	{"Performance", "AiUpdateTickMs", kTypeInt, 0, -1, 100,
	 "How often the game asks for the next AI player's moves during the AI turns.\n"
	 "  0 = on the next frame, only while the AI players move (default, highest performance):\n"
	 "      the AI turns go faster, the game's results do not change.\n"
	 "  1 to 100 = at most this many milliseconds, only while the AI players move.\n"
	 "  -1 = as the game does it: every 100 ms at most. The patch changes nothing.\n"
	 "      Your own turn is not changed. Works only with this exact game build;\n"
	 "      otherwise the patch writes an error report and the game keeps its own pacing.",
	 kCatNone, NULL, "How often the AI's next moves are asked for"},
	{"Performance", "AiSkipVisualDrain", kTypeInt, 0, 0, 2,
	 "Whether the next AI player may start before the previous one's moves have been shown\n"
	 "(experimental).\n"
	 "  0 = off (default): as the game does it. The patch changes nothing.\n"
	 "  1 = on: the AI turns go faster; the AI's moves are shown further behind the game.\n"
	 "  2 = on, but after an AI player with many moves to show the next one waits until\n"
	 "      they are shown (prototype; best with AiDrainUnordered = 1).\n"
	 "      Your own turn is not changed. Works only with this exact game build;\n"
	 "      otherwise the patch writes an error report and the game keeps its own pacing.",
	 kCatNone, NULL, "AI players' turns end before last moves shown"},
	{"Performance", "AiDrainUnordered", kTypeInt, 0, 0, 1,
	 "Whether the AI's moves that are still to be shown at the end of the AI turns are shown\n"
	 "all at once instead of one step per frame (experimental; needs AiSkipVisualDrain = 1 or 2).\n"
	 "  0 = off (default): as the game does it. The patch changes nothing.\n"
	 "  1 = on: the end of the AI turns is shorter; the AI's last moves appear together.\n"
	 "      Only with Quick Combat and Quick Movement on. Works only with this exact game\n"
	 "      build; otherwise the patch writes an error report and the game keeps its own pacing.",
	 kCatNone, NULL, "Show the AI's last moves all at once"},
	{"Performance", "GameEventsOneCrossing", kTypeInt, 0, 0, 1,
	 "Whether a Lua game event the game raises crosses from the 64-bit server to the game once\n"
	 "instead of once per argument (experimental speed-up; the game plays the same).\n"
	 "  0 = off (default): every event crosses as before.\n"
	 "  1 = on: the server keeps the event's arguments and sends them with the event.",
	 kCatNone, NULL, "Lua events cross to the game once (speedup)"},
	{"Renderer", "ServerCities", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the cities: their buildings, walls, wonders and\n"
	 "ruins (experimental; needs ServerRenderer = 1).\n"
	 "  0 = off (default): the game draws the cities itself.\n"
	 "  1 = on: the server draws every city model it has; a model it lacks (a mod's) is still\n"
	 "      drawn by the game. Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server draws the cities"},
	{"Renderer", "ServerLandmarks", kTypeInt, 0, 0, 1,
	 "Whether the patch's 64-bit server draws the improvements, resources, natural wonders\n"
	 "and pontoon bridges (experimental; needs ServerRenderer = 1).\n"
	 "  0 = off (default): the game draws them itself.\n"
	 "  1 = on: the server draws every such model it has; a model it lacks (a mod's) is still\n"
	 "      drawn by the game. Does nothing while ServerRenderer is 0.",
	 kCatNone, NULL, "Server draws improvements and resources"},
	// id 14 (ticket 0613, reserved for 0465's [Extras] flag under K4; MC #13471): a bugfix, default 1 (D169), the owner's
	// word at the merge. An [Extras] row here, among the ids below the switches: written in the [Extras] block all the same
	{"Extras", "FixUnknownWonderBase", kTypeInt, 1, 0, 1,
	 "Draws a natural wonder that the game does not know (one added by a mod) on its tile's own\n"
	 "ground: flat land, hills or a mountain, as the tile is. Needs ServerRenderer = 1.\n"
	 "  1 = on (the default; also needs EnableBugfixes = 1).\n"
	 "  0 = off: as the game draws it, on the ground of Mount Fuji.",
	 kCatBugfix, NULL,
	 "Unknown natural wonders on their own ground"},
	{"Performance", "HiddenEmbarks", kTypeInt, 0, 0, 1,
	 "Whether the game skips showing the AI's units boarding and leaving boats where no human\n"
	 "player can see them, and shows each such unit as it is when it comes into view.\n"
	 "  0 = off (default): as the game does it. The patch changes nothing.\n"
	 "  1 = on: the AI turns are shorter; out of sight nothing changes, and a unit that comes\n"
	 "      into view shows its right model. Needs the patch's own game DLL; otherwise the\n"
	 "      patch writes an error report and the game shows every embark as before.",
	 kCatNone, NULL, "Skip AI embarks no one can see"},
	// ---- [Extras] (ticket 4931; docs/extras/flags-and-messages-design.md 1.2). Written last in the default file, as one
	// block (DefaultFileText): the switches, then the bugfixes, then the visual additions. A feature row comes with its
	// feature (E12): it fills one of the reserved rows below with its key, help, category and needsKey, default 0 until
	// the maintainer accepts it (E4; test_options.cpp kApprovedDefaults).
	{"Extras", "EnableBugfixes", kTypeInt, 1, 0, 1,
	 "Fixes for things the game draws wrongly (roads, bridges, buildings on roads, ...).\n"
	 "  1 = on (default): every fix below that is switched on is used.\n"
	 "  0 = off: every fix below is off, whatever its own line says; the game draws as it always did.",
	 kCatSwitch, NULL,
	 "Bugfixes"},
	{"Extras", "EnableVisualAdditions", kTypeInt, 0, 0, 1,
	 "New visual polish that the game does not have.\n"
	 "  0 = off (default): no addition is used, whatever its own line says.\n"
	 "  1 = on: every addition below that is switched on is used. The installer asks you about this.",
	 kCatSwitch, NULL,
	 "Visual additions"},
	// id 18 (ticket 4934, MC #7273, D212; ticket 4940, D215, MC #8261 and #10673): RoadLayout (was FixRoadLayout 0-1, never
	// shipped); default 0 until the maintainer changes it (E4, D212). A visual addition with every value (the owner, 2026-11-23,
	// in person to one-off 9081: the road options sit with the visual additions; was a category per value, 1 a bugfix and
	// 2 a visual addition, MC #8261)
	{"Extras", "RoadLayout", kTypeInt, 0, 0, 2,
	 "How roads and railroads are laid out (also needs EnableVisualAdditions = 1); without that\n"
	 "switch, as with 0. Needs ServerRenderer = 1: the patch's server draws the roads.\n"
	 "  0 = as the game lays them out (default for now).\n"
	 "  1 = tidy: straight from plot centre to plot centre with short curves, branches merging\n"
	 "      into the through road, and no rings of road around nothing.\n"
	 "  2 = all connections: every two neighbouring plots with a road are joined, as in\n"
	 "      Civilization III, laid out as 1. RoadJunction and RoadJitter shape it.",
	 kCatVisual, NULL,
	 "Road and railroad layout"},
	// ids 19-21: reserved (MC #7273, K4) for FixRoadBridges, FixRoadClearance and ExperimentSharperTerrain; each
	// feature's own ticket fills its row. An empty key is never matched, written or documented, and is no extra.
	{"Extras", "", kTypeInt, 0, 0, 0, ""},
	{"Extras", "", kTypeInt, 0, 0, 0, ""},
	{"Extras", "", kTypeInt, 0, 0, 0, ""},
	// id 22 (ticket 4938, MC #7345): the contribution guide's worked example, a visual addition; default 0 (E4)
	{"Extras", "VisibleAreaOutline", kTypeInt, 0, 0, 1,
	 "A thin line on the ground around the area your units and cities can see now.\n"
	 "  0 = off (default). 1 = on (also needs EnableVisualAdditions = 1).",
	 kCatVisual, NULL,
	 "Outline of the area you see"},
	// ids 23-27 (epic 0455, MC #7491; 26 FixAirBases, a bugfix, by MC #7901; 27 AirInterceptPatrol by MC #8114): the
	// unit extras; each needs ServerUnits = 1 (its consumer says so, one line);
	// default 0 until the maintainer accepts (E4; ShipsMoored off by default by D207)
	{"Extras", "FixUnitPlacement", kTypeInt, 0, 0, 1,
	 "Keeps unit figures out of mountain and hill rock: a land unit at rest stands on the flattest\n"
	 "ground near its tile's centre, each figure on the ground under its feet. Needs ServerUnits = 1.\n"
	 "  0 = off (default for now). 1 = on (also needs EnableBugfixes = 1).",
	 kCatBugfix, NULL,
	 "Units kept out of mountain and hill rock"},
	// id 24: a test value since D243 (ticket 4983, MC #11877: looks a-c are not offered to players; PerimeterGarrisons,
	// id 32, is): read and logged, not in the default file or on the installer's page (Def::testOnly)
	{"Extras", "GarrisonVisible", kTypeInt, 0, 0, 1,
	 "Shows a city's garrison: the military unit stationed in a city is drawn at the city.\n"
	 "Needs ServerUnits = 1.\n"
	 "  0 = off (default). 1 = on (also needs EnableVisualAdditions = 1).",
	 kCatVisual, NULL,
	 "City garrison shown at the city", NULL, 1},
	// D250 (the owner, 2026-11-24): 1 = look (d), 2 = look (b); looks (a) and (c) are not offered (ticket 4984)
	{"Extras", "ShipsMoored", kTypeInt, 0, 0, 2,
	 "Shows ships in a city, moored on the city's water side (also needs EnableVisualAdditions = 1).\n"
	 "Needs ServerUnits = 1.\n"
	 "  0 = off (default).\n"
	 "  1 = bow to the city, drawn a little smaller; several ships side by side.\n"
	 "  2 = alongside the shore; several ships end to end.",
	 kCatVisual, NULL,
	 "Ships moored in cities"},
	{"Extras", "FixAirBases", kTypeInt, 1, 0, 1,
	 "Draws your aircraft at their city or carrier also after a rebase, a carrier's move or a fight,\n"
	 "as the game draws them after a load. Needs ServerUnits = 1.\n"
	 "  0 = off. 1 = on (the default; also needs EnableBugfixes = 1).",
	 kCatBugfix, NULL,
	 "Aircraft shown at their base after a move"},
	{"Extras", "AirInterceptPatrol", kTypeInt, 0, 0, 1,
	 "Shows your and your team's aircraft on intercept duty flying a holding loop in their range.\n"
	 "Needs ServerUnits = 1.\n"
	 "  0 = off (default). 1 = on (also needs EnableVisualAdditions = 1).",
	 kCatVisual, NULL,
	 "Intercepting aircraft fly a patrol loop"},
	// id 28 (ticket 4783, MC #8627): route S's switch; default 1 (the MC's: the beta's fights are the server's), a
	// troubleshooting switch a tester can be asked to set to 0
	{"Renderer", "ServerCombat", kTypeInt, 1, 0, 1,
	 "Whether the patch's 64-bit server also plays the fights: their timing, sounds and effects\n"
	 "(needs ServerUnits = 1).\n"
	 "  1 = on (default): the server plays the fights; if a part of it cannot work on this game,\n"
	 "      the game plays every fight itself and the patch shows one error report.\n"
	 "  0 = off: the game times the fights and plays their sounds and effects itself; the units\n"
	 "      are still drawn by the server. Switch this off if fights look or sound wrong.\n"
	 "      Does nothing while ServerUnits is 0.",
	 kCatNone, NULL, "Server plays the fights"},
	// id 29 (ticket 0635, MC #9004, #9011; D224): read only by the proxy's BootOff (the patch switched off); default 1
	{"Proxy", "GuardsWhenOff", kTypeInt, 1, 0, 1,
	 "Whether the patch keeps its guards against the game's own crashes while the patch is\n"
	 "switched off (the game then runs on Vox Populi's own 32-bit DLL). OPTIONS.md lists them.\n"
	 "  1 = on (default): the UI message queue guard and the farm builder guard run then too\n"
	 "      (a farm field or road curve may be missing until the plot is redrawn).\n"
	 "  0 = off: the patch adds nothing while it is switched off (for diagnosis only).",
	 kCatNone, NULL, "Crash guards while the patch is off"},
	// ids 30-31 (ticket 4940, MC #10673): the shape of RoadLayout = 2 (all connections); visual additions, default 0 until
	// the maintainer picks from the sheets (E4). No needsKey: the installer's page puts an extra under the extra it needs
	// only when that one is a checkbox, and RoadLayout has three values; the consumer reads them only while RoadLayout = 2
	// is in effect (srv_extra_roads.cpp, RoadLayoutChoiceFromOptions).
	{"Extras", "RoadJunction", kTypeInt, 0, 0, 3,
	 "The centre of a plot where four to six roads meet, when every connection is drawn.\n"
	 "Used only with RoadLayout = 2 (also needs EnableVisualAdditions = 1).\n"
	 "  0 = through (default): one road passes through, the others merge into it, as with\n"
	 "      RoadLayout = 1.\n"
	 "  1 = cross: the roads cross in the centre, each two opposite roads in one straight line.\n"
	 "  2 = ring: a small roundabout in the centre, every road ending on it.\n"
	 "  3 = rosette: each two neighbouring roads joined by a short curve round the centre.",
	 kCatVisual, NULL,
	 "Road crossings with all connections"},
	{"Extras", "RoadJitter", kTypeInt, 0, 0, 7,
	 "How much the roads wobble from plot to plot when every connection is drawn, so that they\n"
	 "run tile to tile but not ruler-straight.\n"
	 "Used only with RoadLayout = 2 (also needs EnableVisualAdditions = 1).\n"
	 "  0 = none (default): straight from plot centre to plot centre.\n"
	 "  1 to 7 = each plot's crossing point moved a fixed small amount: 1 a little, 7 the most.",
	 kCatVisual, NULL,
	 "Road wobble with all connections"},
	// id 32 (ticket 4983, D243, MC #11877): the owner's "Perimeter Garrisons", GarrisonVisible's look (d) as the players'
	// option; a visual addition, default 0 (D243)
	{"Extras", "PerimeterGarrisons", kTypeInt, 0, 0, 1,
	 "Shows a city's garrison: the figures of the military unit stationed in a city stand in a\n"
	 "ring along the city tile's edges that face land, facing outward. Needs ServerUnits = 1.\n"
	 "  0 = off (default). 1 = on (also needs EnableVisualAdditions = 1).",
	 kCatVisual, NULL,
	 "Perimeter Garrisons"},
	// id 33 (ticket 0727, 0552's route R1): a bugfix of the EXE's own minimap, default 1 (D169; the ticket's default)
	{"Extras", "FixMinimapUnexplored", kTypeInt, 1, 0, 1,
	 "Clears tiles from the minimap when the game hides them again (a mod or a script that sets\n"
	 "tiles back to unexplored); without it they stay on the minimap until the next load.\n"
	 "  1 = on (the default; also needs EnableBugfixes = 1).\n"
	 "  0 = off: as the game does it.",
	 kCatBugfix, NULL,
	 "Minimap forgets tiles hidden again"},
	// ids 34-36 (ticket 0745, D255: 0739's audit gaps G2-G4, research/07 options-file.md section 10): features that shipped
	// without a key. Each default is the behaviour of main before the key existed.
	{"Proxy", "KeyboardHookThread", kTypeInt, 1, 0, 1,
	 "Whether the patch moves the game's keyboard hook (the one that keeps Alt+Enter from\n"
	 "switching the game's screen mode) off the game's main thread, so that a busy game does\n"
	 "not make typing lag in other programs.\n"
	 "  1 = on (default): the hook runs on a thread of the patch's own; Alt+Enter is held back\n"
	 "      only while the game's window is in front, and Windows shows no \"Not Responding\"\n"
	 "      ghost window for a busy game.\n"
	 "  0 = off: as the game does it: the hook runs on the game's main thread (for diagnosis only).",
	 kCatNone, NULL, "Keyboard hook off the game's main thread"},
	{"Proxy", "LeaderScenes", kTypeInt, 0, 0, 2,
	 "Whether the 3D leader scenes of the civilizations in slots 22 to 42 are loaded (the\n"
	 "43-civ variant only; the game itself loads them only for the first 22). Each scene costs\n"
	 "the 32-bit game about 32 MB of its 4 GB of address space, so many of them make an\n"
	 "out-of-memory crash late in a large game more likely.\n"
	 "  0 = off (default): as the game does it: those leaders talk without their 3D scene.\n"
	 "  1 = on demand: a leader's scene is loaded when its diplomacy screen first opens.\n"
	 "  2 = all at load: every such AI leader's scene is loaded when the game loads.",
	 kCatNone, NULL, "3D leader scenes for civilizations 22-42"},
	{"Performance", "RenderFrameJobs", kTypeInt, -1, -1, 8,
	 "How many helper threads the patch's 64-bit server uses to prepare each picture of the map\n"
	 "(needs ServerRenderer = 1).\n"
	 "  -1 = automatic (default): 2 on a processor with more than 2 threads, else none.\n"
	 "  0 = none: the server prepares each picture on its drawing thread alone.\n"
	 "  1 to 8 = that many helper threads.",
	 kCatNone, NULL, "Helper threads for the server's pictures"},
	// id 37 (ticket 0755, D255; research/02 camera-and-picking.md 11.4): a bugfix of the EXE's wheel handling, default 1
	// as proposed in the ticket (D169: a bugfix is on by default); the owner confirms it at the merge
	{"Extras", "FixSmoothWheel", kTypeInt, 1, 0, 1,
	 "Makes a trackpad or a smooth (high-resolution) mouse wheel zoom the map and scroll lists\n"
	 "one step per notch, as a notched wheel does. The game takes every wheel message as one\n"
	 "full step whatever its size, so a light trackpad flick zooms three steps or more, and a\n"
	 "fast spin of several notches can count as one.\n"
	 "  1 = on (the default; also needs EnableBugfixes = 1): small movements are added up to\n"
	 "      whole notches, and each notch is one step.\n"
	 "  0 = off: as the game does it.",
	 kCatBugfix, NULL,
	 "Trackpad and smooth wheel zoom per notch"},
	// id 38 (ticket 0747, D255 G5/G5b; research/07 options-file.md 10.4): the disk the patch's logs keep; default 10, the
	// proxy's keep_runs before the key (proxy/src/proxy_rt.cpp EffectiveKeepRuns), now also the compositor's kept logs
	{"Logs", "KeepRuns", kTypeInt, 10, 1, 100,
	 "How many of the patch's diagnostic logs are kept in the game's Logs folder: the run\n"
	 "folders in Logs\\VP64\\run (one for each start of the patch's server, about 33 MB an hour\n"
	 "of play) and the compositor's older logs (shell_composite.<date>.log, about 25 MB an\n"
	 "hour). The oldest go first; saves, this file and error reports are never deleted.\n"
	 "  1 to 100 = keep that many of each (default 10); the newest is always kept.",
	 kCatNone, NULL, "How many diagnostic logs are kept"},
	// id 39 (ticket 0784, D268, D255): the landship's tread effect while the server draws the units; default 1 (D268)
	{"Renderer", "HideTreadEffect", kTypeInt, 1, 0, 1,
	 "Whether the game's own scrolling tread effect of the landship is hidden while the patch's\n"
	 "server draws the units (needs ServerUnits = 1). The game draws that effect at its own\n"
	 "hidden copy of the unit, so in a turn it comes apart from the tank the server draws.\n"
	 "  1 = hidden (default): no tread effect; the tank's own moulded tracks stay, but they do\n"
	 "      not scroll while it drives.\n"
	 "  0 = shown: as the game draws it; the treads can lag behind or beside the tank in a turn.\n"
	 "      Does nothing while ServerUnits is 0.",
	 kCatNone, NULL, "Hide the landship's detached tread effect"},
	// id 40 (ticket 0789, D269, D255; research/07 architect-2026-11-25-retained-world.md 6.1): the server's model store,
	// default 1; 0 = ticket 0709's keep (the server's model drawing as before 0789). The server reads it
	// (server/src/render/srv_render_host.cpp; server.cfg render_models_store, a test switch, wins)
	{"Renderer", "ModelStore", kTypeInt, 1, 0, 1,
	 "Whether the patch's 64-bit server remembers every city and improvement model it has drawn,\n"
	 "so that none of them flashes away for a frame while the map moves or a turn is processed\n"
	 "(needs ServerRenderer = 1 and ServerCities or ServerLandmarks = 1).\n"
	 "  1 = on (default): a model stays drawn until the game shows it is gone; some tens of\n"
	 "      megabytes of the server's memory on the largest maps.\n"
	 "  0 = off: only the models of the last few seconds are remembered (the patch before).",
	 kCatNone, NULL, "Server keeps every model it has drawn"},
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// Ticket 0680: the text options (vp64_options.h, TextId). Their sections come after the whole-number ones and before
// [Extras] in the default file.
const TextDef kTextDefs[kTextCount] = {
	{"Shell", "WindowTitlePrefix", "[VP64] ",
	 "Text put in front of the game window's title while the patch runs, so that the taskbar\n"
	 "and Alt+Tab show that the game runs with the patch. Keep the double quotes: they keep the\n"
	 "space at the end. With the patch switched off the title is the game's own.\n"
	 "  \"[VP64] \" = the default: the title reads \"[VP64] Sid Meier's Civilization V (DX11)\".\n"
	 "  \"\" = the game's own title, unchanged. At most 64 characters.",
	 // ticket 0745 (G1): the installer's options page shows this row as a checkbox with this title (ticked = the
	 // default text, unticked = ""); was "Window title prefix" until 2026-11-25
	 "Show [VP64] in the window title"},
};

const char kExtrasSection[] = "Extras";
const char kExtrasComment[] =
	"; Extras are optional changes to what the patch draws. Each one is used only when its category\r\n"
	"; switch below AND its own line are both 1. Fixes are on by default; visual additions are off\r\n"
	"; until you switch them on (the installer asks). Every extra is described in OPTIONS.md.\r\n";

const char kHeader[] =
	"; VP64Options.ini - options of the Vox Populi 64-bit patch.\r\n"
	";\r\n"
	"; Edit this file with any text editor (Notepad is fine) while the game is closed;\r\n"
	"; the patch reads it when the game starts.\r\n"
	";\r\n"
	"; Lines that start with ; are comments. Every option below is shown at its default value,\r\n"
	"; commented out. To change one, delete the ; at the start of its line and edit the value.\r\n"
	"; An option that is missing, or has a value the patch cannot use, takes its default;\r\n"
	"; the patch's log (vpproxy.log) names every line it did not understand.\r\n"
	"; Delete this file to get a fresh copy with the current defaults at the next start.\r\n"
	"; Every option is described in OPTIONS.md, shipped with the patch.\r\n";

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool EqualNoCase(const char* a, size_t alen, const char* b)
{
	const size_t blen = strlen(b);
	if (alen != blen) return false;
	for (size_t i = 0; i < alen; ++i)
		if (Lower(a[i]) != Lower(b[i])) return false;
	return true;
}

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

void Trim(const char* text, size_t& s, size_t& e)
{
	while (s < e && IsSpace(text[s])) ++s;
	while (e > s && IsSpace(text[e - 1])) --e;
}

bool SectionKnown(const char* name, size_t len)
{
	for (int i = 0; i < kCount; ++i)
		if (EqualNoCase(name, len, kDefs[i].section)) return true;
	for (int i = 0; i < kTextCount; ++i)
		if (EqualNoCase(name, len, kTextDefs[i].section)) return true;
	return false;
}

// Shown text is cut to this many bytes in warnings.
const int kShow = 80;
int ShowLen(size_t n) { return n > (size_t)kShow ? kShow : (int)n; }

struct Warner
{
	WarnFn fn;
	void* ctx;
	unsigned count;
	void operator()(const char* fmt, ...)
	{
		++count;
		if (!fn) return;
		char buf[640];
		va_list ap;
		va_start(ap, fmt);
		_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
		va_end(ap);
		fn(ctx, buf);
	}
};

// A whole number: optional sign, 1 to 9 digits, nothing else (so no overflow in an int).
bool ParseInt(const char* v, size_t n, int& out)
{
	size_t i = 0;
	bool neg = false;
	if (i < n && (v[i] == '+' || v[i] == '-'))
	{
		neg = v[i] == '-';
		++i;
	}
	if (i == n || n - i > 9) return false;
	int x = 0;
	for (; i < n; ++i)
	{
		if (v[i] < '0' || v[i] > '9') return false;
		x = x * 10 + (v[i] - '0');
	}
	out = neg ? -x : x;
	return true;
}

// Appends to a bounded buffer; len counts everything, written or not.
struct Appender
{
	char* buf;
	size_t cap, len;
	void Put(const char* s, size_t n)
	{
		for (size_t i = 0; i < n; ++i, ++len)
			if (buf && len + 1 < cap) buf[len] = s[i];
	}
	void Put(const char* s) { Put(s, strlen(s)); }
	void Finish()
	{
		if (buf && cap) buf[len < cap ? len : cap - 1] = 0;
	}
};
}  // namespace

// ================================================================================================ the options
const Def& Definition(Id id) { return kDefs[id]; }

unsigned char CategoryOf(const Def& d, int v)
{
	if (!d.valueCategory || v == 0 || v < d.minValue || v > d.maxValue) return d.category;
	return d.valueCategory[v - d.minValue];
}

Id FindKey(const char* key, size_t len)
{
	for (int i = 0; i < kCount; ++i)
		if (kDefs[i].key[0] && EqualNoCase(key, len, kDefs[i].key)) return (Id)i;   // a reserved id (empty key): never
	return kCount;
}

const TextDef& TextDefinition(TextId id) { return kTextDefs[id]; }

TextId FindTextKey(const char* key, size_t len)
{
	for (int i = 0; i < kTextCount; ++i)
		if (EqualNoCase(key, len, kTextDefs[i].key)) return (TextId)i;
	return kTextCount;
}

bool TextValueValid(const char* value, size_t len, const char** why)
{
	const char* dummy = NULL;
	const char** w = why ? why : &dummy;
	if (len > (size_t)kMaxTextBytes)
	{
		*w = "longer than 64 bytes";
		return false;
	}
	for (size_t i = 0; i < len;)
	{
		const unsigned char c = (unsigned char)value[i];
		if (c < 0x20 || c == 0x7F)
		{
			*w = "a control character";
			return false;
		}
		if (c < 0x80)
		{
			++i;
			continue;
		}
		// UTF-8: the lead byte's length, continuation bytes 10xxxxxx, no overlong form, no surrogate, at most U+10FFFF
		size_t n = 0;
		unsigned cp = 0;
		if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F;
		else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F;
		else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07;
		if (!n || i + n > len)
		{
			*w = "not valid UTF-8";
			return false;
		}
		for (size_t k = 1; k < n; ++k)
		{
			const unsigned char cc = (unsigned char)value[i + k];
			if ((cc & 0xC0) != 0x80)
			{
				*w = "not valid UTF-8";
				return false;
			}
			cp = (cp << 6) | (cc & 0x3F);
		}
		if ((n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
		{
			*w = "not valid UTF-8";
			return false;
		}
		if (cp >= 0x80 && cp < 0xA0)
		{
			*w = "a control character";
			return false;
		}
		i += n;
	}
	return true;
}

void Options::SetDefaults()
{
	for (int i = 0; i < kCount; ++i)
	{
		value[i] = kDefs[i].defaultValue;
		fromFile[i] = 0;
	}
	for (int i = 0; i < kTextCount; ++i)
	{
		strcpy_s(text[i], sizeof(text[i]), kTextDefs[i].defaultValue);
		textFromFile[i] = 0;
	}
}

// ================================================================================================ parsing
unsigned Parse(const char* text, size_t len, Options& out, WarnFn warn, void* ctx)
{
	out.SetDefaults();
	Warner w = {warn, ctx, 0};
	if (!text) len = 0;
	size_t i = 0;
	if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		i = 3;
	unsigned lineNo = 0;
	bool inSection = false;
	char section[64] = {0};
	size_t sectionLen = 0;
	unsigned lastLine[kCount];
	for (int k = 0; k < kCount; ++k) lastLine[k] = 0;
	unsigned lastTextLine[kTextCount];  // ticket 0680
	for (int k = 0; k < kTextCount; ++k) lastTextLine[k] = 0;
	while (i < len)
	{
		++lineNo;
		size_t s = i;
		while (i < len && text[i] != '\n' && text[i] != '\r') ++i;
		size_t e = i;
		if (i < len) i += (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') ? 2 : 1;
		Trim(text, s, e);
		if (s == e || text[s] == ';' || text[s] == '#') continue;
		if (text[s] == '[')
		{
			if (text[e - 1] != ']' || e - s < 2)
			{
				w("line %u: '%.*s': a section header must end with ']' (ignored)", lineNo, ShowLen(e - s), text + s);
				continue;
			}
			size_t ns = s + 1, ne = e - 1;
			Trim(text, ns, ne);
			sectionLen = ne - ns < sizeof(section) - 1 ? ne - ns : sizeof(section) - 1;
			memcpy(section, text + ns, sectionLen);
			section[sectionLen] = 0;
			inSection = true;
			if (!SectionKnown(text + ns, ne - ns))
				w("line %u: [%.*s]: no option belongs to this section; its keys are still read", lineNo,
				  ShowLen(ne - ns), text + ns);
			continue;
		}
		size_t eq = s;
		while (eq < e && text[eq] != '=') ++eq;
		if (eq == e)
		{
			w("line %u: '%.*s': not a comment, a [section] or key = value (ignored)", lineNo, ShowLen(e - s), text + s);
			continue;
		}
		size_t ks = s, ke = eq;
		Trim(text, ks, ke);
		size_t vs = eq + 1, ve = e;
		Trim(text, vs, ve);
		const size_t rawVs = vs, rawVe = ve;  // ticket 0680: a text option reads its quoted value from here
		// inline comment: ';' or '#' at the start of the value or after a space or tab
		for (size_t c = vs; c < ve; ++c)
			if ((text[c] == ';' || text[c] == '#') && (c == vs || IsSpace(text[c - 1])))
			{
				ve = c;
				Trim(text, vs, ve);
				break;
			}
		if (ve - vs >= 2 && text[vs] == '"' && text[ve - 1] == '"')
		{
			++vs;
			--ve;
			Trim(text, vs, ve);
		}
		if (ks == ke)
		{
			w("line %u: '%.*s': no key before '=' (ignored)", lineNo, ShowLen(e - s), text + s);
			continue;
		}
		const Id id = FindKey(text + ks, ke - ks);
		if (id == kCount)
		{
			const TextId tid = FindTextKey(text + ks, ke - ks);
			if (tid == kTextCount)
			{
				w("line %u: unknown option '%.*s' (ignored)", lineNo, ShowLen(ke - ks), text + ks);
				continue;
			}
			// Ticket 0680: a text option. Quoted: exactly what stands between the quotes; after the closing quote only
			// blanks or a comment. Unquoted: the value as every other option reads it (trimmed, inline comment cut).
			const TextDef& td = kTextDefs[tid];
			if (!inSection)
				w("line %u: %s comes before any section; it belongs in [%s] and is read anyway", lineNo, td.key, td.section);
			else if (!EqualNoCase(section, sectionLen, td.section))
				w("line %u: %s belongs in [%s], not [%s]; it is read anyway", lineNo, td.key, td.section, section);
			if (lastTextLine[tid])
				w("line %u: %s is set again (first on line %u); the last one counts", lineNo, td.key, lastTextLine[tid]);
			lastTextLine[tid] = lineNo;
			size_t ts = vs, te = ve;
			const char* bad = NULL;
			if (rawVs < rawVe && text[rawVs] == '"')
			{
				size_t q = rawVs + 1;
				while (q < rawVe && text[q] != '"') ++q;
				if (q == rawVe)
					bad = "has no closing double quote";
				else
				{
					size_t r = q + 1;
					while (r < rawVe && IsSpace(text[r])) ++r;
					if (r < rawVe && text[r] != ';' && text[r] != '#')
						bad = "has text after its closing double quote";
					ts = rawVs + 1;
					te = q;
				}
			}
			const char* why = NULL;
			if (!bad && !TextValueValid(text + ts, te - ts, &why)) bad = why;
			if (bad)
			{
				w("line %u: %s = %.*s: %s; the default \"%s\" is used", lineNo, td.key, ShowLen(rawVe - rawVs), text + rawVs,
				  bad, td.defaultValue);
				strcpy_s(out.text[tid], sizeof(out.text[tid]), td.defaultValue);
				out.textFromFile[tid] = 0;
			}
			else
			{
				memcpy(out.text[tid], text + ts, te - ts);
				out.text[tid][te - ts] = 0;
				out.textFromFile[tid] = 1;
			}
			continue;
		}
		const Def& d = kDefs[id];
		if (!inSection)
			w("line %u: %s comes before any section; it belongs in [%s] and is read anyway", lineNo, d.key, d.section);
		else if (!EqualNoCase(section, sectionLen, d.section))
			w("line %u: %s belongs in [%s], not [%s]; it is read anyway", lineNo, d.key, d.section, section);
		if (lastLine[id])
			w("line %u: %s is set again (first on line %u); the last one counts", lineNo, d.key, lastLine[id]);
		lastLine[id] = lineNo;
		int v = 0;
		if (vs == ve)
		{
			w("line %u: %s has no value; the default %d is used", lineNo, d.key, d.defaultValue);
			out.value[id] = d.defaultValue;
			out.fromFile[id] = 0;
		}
		else if (!ParseInt(text + vs, ve - vs, v) || v < d.minValue || v > d.maxValue)
		{
			w("line %u: %s = %.*s: not a whole number from %d to %d; the default %d is used", lineNo, d.key,
			  ShowLen(ve - vs), text + vs, d.minValue, d.maxValue, d.defaultValue);
			out.value[id] = d.defaultValue;
			out.fromFile[id] = 0;
		}
		else
		{
			out.value[id] = v;
			out.fromFile[id] = 1;
		}
	}
	return w.count;
}

namespace
{
// An option's help as comment lines.
void PutHelp(Appender& a, const char* help)
{
	for (const char* p = help; *p;)
	{
		const char* nl = strchr(p, '\n');
		const size_t n = nl ? (size_t)(nl - p) : strlen(p);
		a.Put("; ");
		a.Put(p, n);
		a.Put("\r\n");
		p += n + (nl ? 1 : 0);
	}
}

// One option's help as comments and its commented default line.
void PutOption(Appender& a, const Def& d)
{
	PutHelp(a, d.help);
	char line[160];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "; %s = %d\r\n", d.key, d.defaultValue);
	a.Put(line);
}
}  // namespace

size_t DefaultFileText(char* buf, size_t cap)
{
	Appender a = {buf, cap, 0};
	a.Put(kHeader);
	const char* lastSection = "";
	for (int i = 0; i < kCount; ++i)
	{
		const Def& d = kDefs[i];
		if (!d.key[0] || d.testOnly) continue;   // a reserved id (ticket 4875: id 11 on this branch); a test value (4983)
		if (strcmp(d.section, kExtrasSection) == 0) continue;   // [Extras] comes last, as one block (ticket 4931)
		if (strcmp(d.section, lastSection) != 0)
		{
			a.Put("\r\n[");
			a.Put(d.section);
			a.Put("]\r\n");
			lastSection = d.section;
		}
		else
			a.Put("\r\n");
		PutOption(a, d);
	}
	// Ticket 0680: the text options, after the whole-number sections, each value in double quotes.
	for (int i = 0; i < kTextCount; ++i)
	{
		const TextDef& d = kTextDefs[i];
		if (strcmp(d.section, lastSection) != 0)
		{
			a.Put("\r\n[");
			a.Put(d.section);
			a.Put("]\r\n");
			lastSection = d.section;
		}
		else
			a.Put("\r\n");
		PutHelp(a, d.help);
		char line[160];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "; %s = \"%s\"\r\n", d.key, d.defaultValue);
		a.Put(line);
	}
	// [Extras] once, at the end (design 1.5, Q5): the switches, then the bugfixes, then the visual additions, each by id,
	// except that the road options (keys starting with "Road") close their category together (the owner, 2026-11-23, one-off
	// 9081: RoadLayout beside RoadJunction and RoadJitter; the installer's page follows this order).
	a.Put("\r\n[");
	a.Put(kExtrasSection);
	a.Put("]\r\n");
	a.Put(kExtrasComment);
	const unsigned char order[] = {kCatSwitch, kCatBugfix, kCatVisual};
	for (size_t c = 0; c < sizeof(order); ++c)
		for (int roads = 0; roads < 2; ++roads)
			for (int i = 0; i < kCount; ++i)
			{
				const Def& d = kDefs[i];
				if (!d.key[0] || d.testOnly || d.category != order[c] || strcmp(d.section, kExtrasSection) != 0) continue;
				if ((strncmp(d.key, "Road", 4) == 0) != (roads == 1)) continue;
				a.Put("\r\n");
				PutOption(a, d);
			}
	a.Finish();
	return a.len;
}

// ================================================================================================ extras (ticket 4931)
namespace
{
Id SwitchOf(unsigned char category)
{
	return category == kCatBugfix ? kEnableBugfixes : category == kCatVisual ? kEnableVisualAdditions : kCount;
}

bool IsExtraRow(const Def& d)
{
	return d.key && d.key[0] && (d.category == kCatBugfix || d.category == kCatVisual);
}

ExtraState NotAnExtra()
{
	ExtraState s = {false, 0, kExtraNotAnExtra, kCount};
	return s;
}

// depth bounds the needs chain at run time (a unit test forbids cycles; this keeps a broken table from recursing).
ExtraState ExtraAt(const Options& o, Id id, int depth)
{
	if ((int)id < 0 || (int)id >= (int)kCount || !IsExtraRow(kDefs[id])) return NotAnExtra();
	const Def& d = kDefs[id];
	ExtraState need = NotAnExtra();
	const ExtraState* needp = NULL;
	if (d.needsKey)
	{
		const Id nid = FindKey(d.needsKey, strlen(d.needsKey));
		if (nid != kCount && nid != id && depth < 8)
		{
			need = ExtraAt(o, nid, depth + 1);
			needp = &need;
		}
	}
	ExtraState s = ExtraOf(d, o.value[id], needp, o);
	if (s.decidedBy == kCount && (s.why == kExtraOn || s.why == kExtraFlagOff)) s.decidedBy = id;
	return s;
}
}  // namespace

ExtraState ExtraOf(const Def& d, int ownValue, const ExtraState* need, const Options& o)
{
	if (!IsExtraRow(d)) return NotAnExtra();
	// ticket 4940: the switch of the value's category (RoadLayout = 2 is a visual addition: its switch off gives stock, 0)
	Id sw = SwitchOf(CategoryOf(d, ownValue));
	if (sw == kCount) sw = SwitchOf(d.category);   // a broken per-value entry: the row's own switch decides (the safe side)
	ExtraState s = {false, 0, kExtraOn, kCount};
	if (o.value[sw] == 0)
	{
		s.why = kExtraCategoryOff;
		s.decidedBy = sw;
	}
	else if (ownValue == 0)
		s.why = kExtraFlagOff;
	else if (d.needsKey && (!need || !need->on))
	{
		s.why = kExtraNeedsOff;
		s.decidedBy = FindKey(d.needsKey, strlen(d.needsKey));
	}
	else
	{
		s.on = true;
		s.value = ownValue;
	}
	return s;
}

ExtraState Extra(const Options& o, Id id) { return ExtraAt(o, id, 0); }

size_t DescribeExtraOf(const Def& d, int ownValue, bool ownFromFile, const ExtraState& s, const Options& o, char* buf,
                       size_t cap)
{
	char line[400];
	const char* key = d.key && d.key[0] ? d.key : "(no key)";
	const char* src = ownFromFile ? "from the file" : "default";
	// ticket 4940: the value's category (CategoryOf), as ExtraOf decides
	const unsigned char cat = SwitchOf(CategoryOf(d, ownValue)) != kCount ? CategoryOf(d, ownValue) : d.category;
	const Id sw = SwitchOf(cat);
	if (s.why == kExtraNotAnExtra || sw == kCount)
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: not an extra", key);
	else
	{
		const char* swSrc = o.fromFile[sw] ? "from the file" : "default";
		switch (s.why)
		{
		case kExtraOn:
			_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: in effect (%s = %d %s; %s = %d %s)", key, key, ownValue, src,
			            kDefs[sw].key, o.value[sw], swSrc);
			break;
		case kExtraCategoryOff:
			if (d.valueCategory && ownValue != 0)   // ticket 4940: name the value whose category it is
				_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: off: %s = %d (%s) switches off every %s (%s = %d is one)", key,
				            kDefs[sw].key, o.value[sw], swSrc, cat == kCatBugfix ? "bugfix" : "visual addition", key, ownValue);
			else
				_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: off: %s = %d (%s) switches off every %s", key, kDefs[sw].key,
				            o.value[sw], swSrc, cat == kCatBugfix ? "bugfix" : "visual addition");
			break;
		case kExtraNeedsOff:
			_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: off: it needs %s, which is off", key,
			            d.needsKey ? d.needsKey : "(none)");
			break;
		default:  // kExtraFlagOff
			_snprintf_s(line, sizeof(line), _TRUNCATE, "%s: off: %s = %d (%s)", key, key, ownValue, src);
			break;
		}
	}
	Appender a = {buf, cap, 0};
	a.Put(line);
	a.Finish();
	return a.len;
}

size_t DescribeExtra(const Options& o, Id id, char* buf, size_t cap)
{
	if ((int)id < 0 || (int)id >= (int)kCount || !kDefs[id].key[0])
	{
		char line[64];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "option %d: not an extra", (int)id);
		Appender a = {buf, cap, 0};
		a.Put(line);
		a.Finish();
		return a.len;
	}
	return DescribeExtraOf(kDefs[id], o.value[id], o.fromFile[id] != 0, Extra(o, id), o, buf, cap);
}

size_t DescribeSwitches(const Options& o, char* buf, size_t cap)
{
	char line[200];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "%s = %d (%s), %s = %d (%s)", kDefs[kEnableBugfixes].key,
	            o.value[kEnableBugfixes], o.fromFile[kEnableBugfixes] ? "from the file" : "default",
	            kDefs[kEnableVisualAdditions].key, o.value[kEnableVisualAdditions],
	            o.fromFile[kEnableVisualAdditions] ? "from the file" : "default");
	Appender a = {buf, cap, 0};
	a.Put(line);
	a.Finish();
	return a.len;
}

// ================================================================================================ files
LoadStatus LoadFile(const wchar_t* path, Options& out, WarnFn warn, void* ctx, unsigned* warnings)
{
	out.SetDefaults();
	Warner w = {warn, ctx, 0};
	LoadStatus st = kLoaded;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	char* data = NULL;
	char* conv = NULL;
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
			st = kMissing;
		else
		{
			w("cannot open the file: error %lu; every option takes its default", (unsigned long)err);
			st = kUnreadable;
		}
	}
	else
	{
		LARGE_INTEGER size;
		DWORD got = 0;
		if (!GetFileSizeEx(h, &size))
		{
			w("cannot read the file's size: error %lu; every option takes its default", (unsigned long)GetLastError());
			st = kUnreadable;
		}
		else if (size.QuadPart > kMaxFileBytes)
		{
			w("the file is %I64d bytes, more than the %d an options file may have; it is not read and every option "
			  "takes its default", size.QuadPart, (int)kMaxFileBytes);
			st = kTooLarge;
		}
		else if ((data = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 1)) == NULL)
		{
			w("out of memory reading the file; every option takes its default");
			st = kUnreadable;
		}
		else if (!ReadFile(h, data, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart)
		{
			w("cannot read the file: error %lu; every option takes its default", (unsigned long)GetLastError());
			st = kUnreadable;
		}
		CloseHandle(h);
		if (st == kLoaded)
		{
			const char* text = data;
			size_t len = got;
			if (len >= 2 && (unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF)
			{
				w("the file is saved as UTF-16 big-endian, which the patch does not read; save it as UTF-8 or ANSI. "
				  "Every option takes its default");
				st = kUnreadable;
			}
			else if (len >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE)
			{
				// UTF-16LE with a byte order mark (Notepad's "Unicode"): convert to UTF-8.
				const int wn = (int)((len - 2) / 2);
				const wchar_t* ws = (const wchar_t*)(data + 2);
				const int need = wn ? WideCharToMultiByte(CP_UTF8, 0, ws, wn, NULL, 0, NULL, NULL) : 0;
				if (wn && (need <= 0 || (conv = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need)) == NULL ||
				           WideCharToMultiByte(CP_UTF8, 0, ws, wn, conv, need, NULL, NULL) != need))
				{
					w("cannot convert the UTF-16 file to UTF-8; every option takes its default");
					st = kUnreadable;
				}
				else
				{
					text = conv;
					len = (size_t)need;
				}
			}
			if (st == kLoaded) w.count += Parse(text, len, out, warn, ctx);
		}
	}
	if (st != kLoaded) out.SetDefaults();
	if (conv) HeapFree(GetProcessHeap(), 0, conv);
	if (data) HeapFree(GetProcessHeap(), 0, data);
	if (warnings) *warnings = w.count;
	return st;
}

CreateStatus CreateDefaultFile(const wchar_t* path, unsigned long* error)
{
	if (error) *error = 0;
	const size_t n = DefaultFileText(NULL, 0);
	char* text = (char*)HeapAlloc(GetProcessHeap(), 0, n + 1);
	if (!text)
	{
		if (error) *error = ERROR_NOT_ENOUGH_MEMORY;
		return kCreateFailed;
	}
	DefaultFileText(text, n + 1);
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		HeapFree(GetProcessHeap(), 0, text);
		if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) return kExists;
		if (error) *error = err;
		return kCreateFailed;
	}
	DWORD put = 0;
	const BOOL ok = WriteFile(h, text, (DWORD)n, &put, NULL) && put == (DWORD)n;
	const DWORD err = ok ? 0 : GetLastError();
	CloseHandle(h);
	HeapFree(GetProcessHeap(), 0, text);
	if (!ok)
	{
		DeleteFileW(path);  // never leave a half-written file behind: the next start tries again
		if (error) *error = err ? err : ERROR_WRITE_FAULT;
		return kCreateFailed;
	}
	return kCreated;
}

PathSource DefaultPath(wchar_t* buf, size_t cap)
{
	if (!buf || !cap) return kPathNone;
	buf[0] = 0;
	const DWORD n = GetEnvironmentVariableW(VP64OPT_ENV_VAR, buf, (DWORD)cap);
	if (n > 0 && n < cap) return kPathEnv;
	buf[0] = 0;
	if (n >= cap) return kPathNone;  // set but longer than the buffer: refuse rather than fall back silently
	// SHGetFolderPathW(NULL, CSIDL_PERSONAL = 5, NULL, SHGFP_TYPE_CURRENT = 0, out) through shell32 loaded by name.
	typedef HRESULT(WINAPI * SHGetFolderPathWFn)(HWND, int, HANDLE, DWORD, LPWSTR);
	HMODULE shell = GetModuleHandleW(L"shell32.dll");
	if (!shell) shell = LoadLibraryW(L"shell32.dll");  // kept loaded: the EXE has it loaded anyway
	void* proc = shell ? (void*)GetProcAddress(shell, "SHGetFolderPathW") : NULL;
	SHGetFolderPathWFn fn = (SHGetFolderPathWFn)proc;
	wchar_t docs[MAX_PATH] = {0};
	if (!fn || FAILED(fn(NULL, 5, NULL, 0, docs)) || !docs[0]) return kPathNone;
	const wchar_t* tail = L"\\My Games\\Sid Meier's Civilization 5\\" VP64OPT_FILE_NAME;
	const size_t dl = wcslen(docs), tl = wcslen(tail);
	if (dl + tl + 1 > cap) return kPathNone;
	memcpy(buf, docs, dl * sizeof(wchar_t));
	memcpy(buf + dl, tail, (tl + 1) * sizeof(wchar_t));
	return kPathDocuments;
}

bool WideToUtf8(const wchar_t* in, char* out, size_t cap)
{
	if (!in || !out || !cap) return false;
	const int n = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, (int)cap, NULL, NULL);
	if (n <= 0)
	{
		out[0] = 0;
		return false;
	}
	return true;
}

bool Utf8ToWide(const char* in, wchar_t* out, size_t cap)
{
	if (!in || !out || !cap) return false;
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in, -1, out, (int)cap);
	if (n <= 0)
	{
		out[0] = 0;
		return false;
	}
	return true;
}
}  // namespace Vp64Opt
