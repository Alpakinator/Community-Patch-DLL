# The offline scene harness: the bundle contract (C1) and premises P2, P5, P7 (epic 0301, task 3901)

Written 2026-11-14 23:36-23:58 EST by the implementer of task 3901 (account B, branch `task-3901-bundle-contract`) for
lead-0301. The plan is the Architect's (`research/07-architecture-decisions/architect-2026-11-14-offline-scene-harness.md`),
the epic brief `docs/briefs/epic-0301-offline-scene-harness.md`. This file is the contract that 3902-3909 build against.
The MC reviews it before any code goes into `server/` or `render/`. 3909 completes it later with the formats as built, the
commands and the measured times. (2026-11-15, 3909: the formats as built are the "As built" paragraphs of sections 1-4
and 6; the commands, the two rules, the sizes and the time placeholders for 3906 are the section "Commands and rules"
after section 0; the unit-draw analysis is `research/05-graphics-composition/unit-draws.md`.)

**What was read.** Every `[C]` statement was read in the code of the team branch at bf1f2d51. That branch holds `main`
b81d4dae, and `server/` and `render/` have not changed since 5d20c30c, the commit the brief names. `[L]` statements come
from the conversion of one stock capture made on 2026-11-14 (section 8). `[I]` statements are inferred. All dates are
2026-11-14 unless marked.

## 0. Results in one screen

- **P7 holds [C].** Every tap allows only one reader. The water tap and the height-field tap have one ack slot, and the
  producer may close its section once that slot is written. The overlay tap protects a slot with a single `inUseSlot`
  word. So the recorder sits inside the readers, at the hand-off points in section 5. The bake-cache tap would allow a
  passive reader (it opens by name, read-only), but it is not on the product's path (`render_tiles=bake` is the default),
  so it is left out of the bundle.
- **P2 holds on one condition [C].** The recorded feed must contain the plot tables the render thread read with
  `SrvFeed::CopyPlots`, not only the drained batches. Read live, `CopyPlots` returns the table at the moment of the call.
  That table can be newer than the batch, and a `Batch` carries no record values. Section 7 gives the details and what
  3904 and 3905 test.
- **P5 holds [L].** In capture 3701 stock A the unit draws are `UnitShader_Skinned_CODE` (material 90): the SHA-256 of
  their shaders equals the EXE's blobs. The bone palette is not a constant buffer: it is a dynamic buffer SRV in VS t27,
  98 float4 per instance (2 header rows, then 32 bones x 3 rows), in world space. The conversion holds it without a
  replay (one `Map(WRITE_DISCARD)`/`Unmap` per frame). Each instance's root bone falls on its plot within a few world
  units. Section 8.
- **Two findings change the plan's picture:**
  1. **The coast waves carry state.** `WaterScene::draw` integrates `cw::updateWaves` over every drawn frame's dt since
     the water image was taken. A per-frame clock value alone therefore cannot redraw frame N. The bundle records the
     water clock state at the start of the span (section 6).
  2. **`render_test_dump` works only with the v3 sink.** The product's server.cfg has `render_handover=ring`, and with
     it nothing is dumped (`h.v3` is null). The recorder therefore reads back the drawn targets itself (section 2.8).
- **P1, offline equals live (3905, 2026-11-15) [L]:** Tiny byte-equal in 8 of 8 dumped frames (colour and depth). Huge:
  depth byte-equal in 8 of 8; colour equal in 5, with one pixel off by 1 in 3 frames. That residue is the renderer's
  own run-to-run variation when the water draws with the overlay, not the replay. The MC accepted the tolerance 1:1 as
  the standing tolerance (#1403, 2026-11-15) until follow-up 0336 makes that draw bit-stable (section 16).
- **Cost with the keys absent:** none expected `[I]`. Decision 1 stands: test keys, as `render_test_dump` (section 13).
- **Renderer inputs not yet in the bundle:** the bake-cache tap (left out on purpose). (Corrected 2026-11-15, 3911: epic
  0168's fog inputs are in the bundle from format version 2, section 2.11; replaced "and epic 0168's fog inputs, which
  are not merged at b81d4dae".) A bundle of format version 1 (every bundle recorded before 3911) is refused by the
  checker and the player as `OlderVersion` (exit 26). (Corrected 2026-11-15, 0364: format version **3** carries epic
  0166's decals (2.10) and epic 0169's shadow and ice (2.12); a version-2 bundle is refused as older; the library was
  recorded again at version 3, section 19. The bake-cache tap stays the only input left out.) (Added 2026-11-16, 0403:
format version **6** carries the FOWLevel-3 fog particles' state, `particles.bin`, section 23; version 5 is refused as
older; the library recorded again with the particles on replays byte-equal in 8 of 8 frames of all seven bundles.)
(Added 2026-11-17, 0493: format version **7** changes no member: it marks the feed's read order of f916762a5, which the
format-6 bundles do not hold; version 6 is refused at load naming that change; the library recorded again, section 24.)
(Added 2026-11-17, 0495: format version **8** carries the trees of epic 0431 (`forest.bin`, the forest's table reads as
`feed.bin` PLOTS caller 4, `render_forest`) and the near-camera pairs of issue 0371 (`near.bin`); version 7 is refused
as older; the library recorded again with both on, section 25.) (Added 2026-11-19, 4883: format version **9** carries the
compositor's model tap frames of epic 0561 (`modeltap/`, ticket 4875); the library recorded again with options 12 / 13 on
into `staging-v9/`, six bundles byte-equal, huge noted for the CPU-built meshes that have no member yet; section 31.) Epic 0179 adds no renderer input: route (b) lets the EXE draw the arrows and the path, and
  its `kUiMirror` records arrive in the feed and are recorded with it (section 3).

**The plan's premises, as answered (3909, 2026-11-15 02:40; the plan is
`research/07-architecture-decisions/architect-2026-11-14-offline-scene-harness.md` section 8):**
- P1 offline equals live: Tiny byte-equal in 8 of 8 dumped frames, Huge depth equal and colour within 1 pixel per frame
  by 1 [L, 3905, section 16]; the MC's tolerance `1:1` (#1403) until follow-up 0336.
- P2 feed replay: holds with the `CopyPlots` tables recorded (section 7) [C, 3901; tested 3904].
- P3 several frames per capture request: works; every second presented frame; about 7 frames per Tiny process at the
  guard's floor [L, 0302, `renderdoc-capture.md` 10].
- P4 the interface hidden by Lua leaves the world picture unchanged: holds by the draws [L, 3907, `scene-catalogue.md` 1].
- P5 unit draws told apart, bone palette without a replay: holds [L, 3901 section 8; extended to the whole catalogue by
  3909, `research/05-graphics-composition/unit-draws.md`].
- P6 time between captured frames: about 1.1-1.25 s, read from the Present timestamps [L, 0302].
- P7 the taps allow no passive second reader: holds (section 5) [C, 3901].
- **Catalogue size, corrected:** the plan's "about 23 captures of about 90 MB" became 24 captures (20 unit frames of
  229 placeable units and 4 city frames of 16 sets x 2 sizes) of 91-148 MB, 15.0 GB on F: with their exports and the
  retaken attempts; at most 6 unit frames or 2 city frames per game process at the guard's 100 MB floor, so six game
  processes [L, 3907, `scene-catalogue.md` 5, 6]. (Replaced in effect: the plan's 4.5 sizing; the plan file itself is
  the Architect's and is not edited without the MC.)

## Commands and rules (task 3909, 2026-11-15; for `COMMON.md` and the handoff at 0301's merge)

Written 2026-11-15 by the implementer of 3909 for lead-0301 and the MC. The two rules are D137's (the Architect's plan
4.1 rule 2 and section 7). They take effect when 0301 merges into `main`; until then they bind nobody, 0301 included.
The MC copies the rule block into `docs/briefs/COMMON.md` and the handoff; the commands below are what a worker types.

### The two rules (final text)

> **Rule A: a new renderer input comes with its file form and its recorder in the same change.** A change that makes
> the server's render thread read something it did not read before (a shared section or tap, a feed record kind, a
> texture, a clock, an option) also, in the same merge: adds the bundle member that holds it, with its file form and
> a raised `format_version` (this file, section 2); writes it in the recorder (`srv_scene_recorder.*`); reads it in the
> player through the same checks as the live reader (`srv_scene_sources.*`, `sceneshot`); adds a positive and a negative
> test of the file form; and adds the input's row to section 3. A change that cannot do all of this says so in its
> merge request, and the recorder names the input as a `gap:` in every bundle's `version_notes` until it is done.
> Otherwise the bundle lags the product and offline work silently tests an old world.
> (Added 2026-11-17, ticket 0493: a change to the ORDER of the render thread's feed reads, a new `copyPlots`,
> `copyUnits`, `copyPlayers` or `drain` call, one moved or dropped, or another caller id, counts as a new input here: it
> raises `format_version` with its `FormatVersionChange` line in `srv_scene_bundle.cpp` and the library is recorded again
> (section 24). f916762a5 did not, and every bundle failed the replay with "feed: out of order". Same day: a feature
> turned on by default that the player does not reproduce is a new input too. Epic 0431's trees (`render_forest`) and
> issue 0371's near pairs (`render_bake_near`) came with no member, no player support and no gap note, so the library
> is recorded with both off until ticket 0495, section 24.)
> (Added 2026-11-17, ticket 0495, section 25: **a feature that the server draws by default ships, in the same change,
> its bundle member (with the recorder writing it and the player replaying it), its `render_config` key, or a `gap:`
> note that the recorder writes into every bundle it cannot carry.** A change that turns on by default a draw that was
> off counts as a new input, also when every input it reads is already recorded, because the player must turn it on
> too. And a reader that bypasses the host's `FeedSource` (the forest called `SrvFeed::CopyPlots` itself until 0495) is a
> new input: its reads go through the feed with their own caller id, or the replay cannot give them.)
>
> **Rule B: a renderer work package names the bundle it iterates on; a live run is for acceptance and for recording a
> bundle.** The issue or brief of a renderer package names the bundle ids (`F:/c5-64bit-work/scene-library/bundles/<id>`)
> its work is checked on, and the loop of change, build and look runs on them with `sceneshot`, with no game instance.
> A live game run is for acceptance (D112), for recording a bundle, and for recording it again after an input changed
> (rule A). A bundle the checker or the player refuses, or of an older format, is recorded again; it is never skipped.
> (Added 2026-11-22, ticket 4749, section 40: "an older format" means a format_version below `kOldestReadFormatVersion`
> in `srv_scene_bundle.h`, the refusal floor; format 10 is the first raise that keeps the version before it (9) readable.)

### Commands a worker uses

All builds and runs through `perf/quietlock/quietlock.py run --who <ticket>` (no `--take`), C5_JOB_OWNER set; outputs
under `C:/c5-64bit-work/<ticket>/`; the library's master copy on F: (D141; section 11).

1. **Record a bundle** (product path, a game instance; the keys of section 9 in server.cfg). With 3904's driver:
   ```
   python -I server/tests/render/scenebundle/recorder_live.py prep --save tiny|huge --instance c5h-a --work C:/c5-64bit-work/<ticket>/rec \
          --bin <layout>/bin --proxy <build>/proxy/bin/CvGameCore_Expansion2.dll --composite <shell build> \
          --bundle F:/c5-64bit-work/scene-library/bundles/<save>_<yyyymmdd-hhmmss>_<machine> --after 900 --frames 8 --dump all
   python -I server/tests/render/scenebundle/recorder_live.py run --save tiny|huge --instance c5h-a --work C:/c5-64bit-work/<ticket>/rec \
          --checker <test build>/recorder_test.exe
   python -I server/tests/render/scenebundle/recorder_live.py restore --save tiny|huge --instance c5h-a --work C:/c5-64bit-work/<ticket>/rec
   ```
   Any other driver: the server.cfg lines `render_test_record=<absolute empty folder>`, `render_test_record_after=N`,
   `render_test_record_frames=N`, `render_test_record_dump=all` (section 15: `all` keeps a span whole). Then stamp what
   the server cannot know and check: `recorder_test.exe --stamp DIR save.sha256=... save.turn=... repo_commit=...
   build_config=... pins.<name>=...`, `recorder_test.exe --check DIR` (exit 0 `scenebundle: OK`, else 21-42 with the
   `scenebundle: REFUSED ...` line).
2. **Replay it (the player):** build with `python -I server/tools/sceneshot/build_sceneshot.py --out
   C:/c5-64bit-work/<ticket>/ss` (through quietlock), then
   ```
   sceneshot.exe --bundle F:/c5-64bit-work/scene-library/bundles/<id> --dumped --compare --tolerance 1:1 --out C:/c5-64bit-work/<ticket>/shots
   sceneshot.exe --bundle <dir> --camera <name> | --frame <id> | --span <first>:<last> --out <dir> [--diff] [--set render_KEY=VALUE]
   ```
   Exit codes and selectors: section 16 and `server/tools/sceneshot/README.md` (3905). **The accepted standing
   tolerance is `1:1`** (MC #1403, 2026-11-15): depth byte-equal always, colour at most 1 pixel per frame off by at most
   1 in any channel. The reason (corrected 2026-11-16 by 0336, water-system.md section 15): the D3D11 driver's internal
   threading optimisations make the ocean draw flip one pixel by 1 in one channel from redraw to redraw, overlay or not
   (it read: "where the water module draws over the overlay"). Since 0336 the player creates its device with
   D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS and is byte-stable; bundles recorded from a server with
   flags 0 keep `1:1` against their live dumps until 0435 gives the server the flag. The player's default stays `0:0`. A `--set`
   run is a diagnosis, not the check.
   (Ticket 4938, 2026-11-19: `--option KEY=VALUE` sets an `[Extras]` line of the player's options, repeatable, so an
   extra's on and off pictures come from the same bundle (rule E9 of the contribution guide for extras); with every extra
   off it is P1 as before. The player links `server/src/extras/`; `server/tools/sceneshot/README.md`. The player reads the
   unit art through the instance's mods database: an instance whose database has no mod activated fails the unit
   pre-check, exit 8 (c5h-a at 23:30, [L]); `--install`/`--exe`/`--mods` of another Goldberg copy (c5h-e) replayed.)
3. **The standing check "offline equals live":** `server/tests/render/scenebundle/test_offline_equals_live.py` over
   every bundle of the library, with `C5_SCENE_LIBRARY` (default `F:/c5-64bit-work/scene-library/bundles`),
   `C5_SCENESHOT_EXE` (the built player); the tolerance is `1:1` by default (MC #1403, 0336), `C5_P1_TOLERANCE`
   overrides it. No library or no bundle fails. (3911, 2026-11-15: from format version 2 on, the version-1 bundles are in
   `F:/c5-64bit-work/scene-library/retired/`, section 2.11; `bundles/` holds the current ones. 0353, 2026-11-15: seven
   bundles of format version 2, section 18: the fog bundle, Tiny, huge and the four full scenes. Corrected 2026-11-15,
   0364: those seven are retired; `bundles/` holds the same seven scenes recorded again at format version 3 with
   decals, shadow and ice, section 19.)
4. **All bundles, all cameras, the report (3906, 2026-11-15; section 17, `server/tools/scenerun/README.md`):**
   ```
   python -I perf/quietlock/quietlock.py run --who <ticket> --what "scenerun" -- python -I server/tools/scenerun/scenerun.py run \
          --player C:/c5-64bit-work/<ticket>/ss/bin/sceneshot.exe --out F:/c5-64bit-work/<ticket>/run --issue <ticket> [--bundle ID ...]
   ```
   Every bundle of `F:/c5-64bit-work/scene-library/bundles` (or of `index.json` when there is one: a listed bundle that
   is missing fails, exit 3), every member's SHA-256 checked first (exit 4), the dumped frames and every named camera
   drawn; the report is `<out>/report.md` and `report.json` (P1 against the server's dump, regression against the
   baseline `F:/c5-64bit-work/scene-library/baselines/<machine>__<adapter>/`, fidelity against the stock shot of the
   same camera with region masks and a difference image in `<out>/fidelity/`). Machines: the workstation, and oldpc
   through the testbed (`scenerun.py copy --host oldpc`, then the `tb.py submit` line of the README; `--adapter hw`,
   the testbed's `pure_vp` base as the install). Times: add `--no-pictures --no-record --repeat 5`.
5. **Pictures:** every picture kept or looked at: `python -I tools/visualcheck/visualcheck.py record PICTURE --what
   "DIAGNOSTIC: ..." --result info|pass|fail --issue <ticket> [--reference <the bundle's dump or stock shot>]`; none is
   committed (D138); `visualcheck.py list --issue <ticket> --result undescribed` empty at hand-back.
6. **Which game and EXE the tests read (ticket 0589, 2026-11-19; section 33).** No render test reads the Steam install
   by default any more (it is Firaxis' build 26577625 since 2026-11-19, D200). Point them at a pre-update copy, read
   only: `C5R_EXE=<copy>/CivilizationV_DX11.exe` and `C5R_GAME_DIR=<copy>` (run_all.py `--exe` / `--game`; it sets
   `C5_GAME_DIR` from `C5R_GAME_DIR`). A test program run by itself takes `C5_GAME_DIR`, else `C5R_GAME_DIR`, else the
   setting `render_game_dir` of `tools/release/settings.py` (`C5_RENDER_GAME_DIR`, else `deps/local-settings.ini`); with
   none it fails naming the variables. An EXE that is not build 4390913 (`tools/harness/exe_build.py`'s stamp and size)
   fails the test; `C5_ALLOW_UNKNOWN_EXE_BUILD=1` lets it through, only once the migration is approved.
7. **Which instance the players read (ticket 0614, 2026-11-20; section 34).** By default the player reads the recording
   pool instance's install, MODS folder and mods database. It refuses one the pool shows claimed (exit 9), because a
   live run rewrites the database while its game starts. On the workstation, use `--asset-root DIR`: a frozen copy made
   by `server/tools/sceneshot/assetroot.py make --from <free instance> --out DIR`, with pure_vp as the install,
   checked by its manifest hash.

### Measured times (filled by 3906, 2026-11-15; indications of the workflow, never product figures: PERFORMANCE.md)

All three from `scenerun.py run ... --no-pictures --no-record --repeat 5` over the six bundles of section 17 (48 shots a
pass; "per shot" = sceneshot's settle + draw + readback + compare, without the PNG; the first shot of a player run
carries the residency's bake-ahead and is given apart). Section 17 has every number.
- TIME [L, 2026-11-15 03:44-03:50 EST]: **workstation**, RTX 4080 (driver 32.0.16.1088), n 5 passes = 40 shots per
  bundle: later shots median 121-141 ms on the five Tiny bundles (min 83, max 209) and 177 ms on Huge (105-215); the
  first shot 3.3-3.6 s (Tiny) and 11.3 s (Huge, 10.9-13.7); a whole player run of 8 shots median 7.4-12.1 s (Tiny) and
  24.0 s (Huge); the whole library pass 68.7 s (62.0-69.9). `quietlock.py check --quiet` exit 1 before every pass
  (other teams' games, suites and builds registered): numbers from a busy machine. With pictures (PNG and depth written),
  a later shot is 650-1100 ms (3905's 851 ms median was this).
- TIME [L, 2026-11-15 03:32-03:37 EST host clock]: **oldpc** (testbed job 20261115-033219-05dd, RTX 2080 driver
  31.0.15.4592, session 0, no other job on the host), n 5 passes = 40 shots per bundle: later shots median 119.7-124.5 ms
  (min 118.4, max 131.8); first shot 2.53-2.55 s (Tiny) and 8.66 s (Huge); a player run 6.96-7.01 s (Tiny) and 17.44 s
  (Huge); the library pass 52.4 s (52.38-52.54).
- TIME [L, 2026-11-15 03:20-03:31 EST]: **the live cycle** on the workstation (`server/tools/scenerun/live_cycle.py`,
  pool instance c5h-a, stock route with the instance's own DLL, the Tiny save copy C5H-0301-RUN_0008): start client ->
  load -> popups closed -> screenshot, n 4: median 115.9 s (110.5-123.2); of it the launch 20.4-22.9 s and the load to
  "in game" median 110.8 s from the start; the safe stop after it 23.7-24.1 s more. quietlock exit 1 before every run.
  Set beside it: one offline look at a Tiny scene is a player run of 7-12 s (8 shots) on either machine, or 0.12-0.14 s a
  shot once the scene is up. The live figure has no build and no change in it; a renderer change adds its server build.

### Sizes and limits (as measured by 3904, 3905 and 3907)

- A Tiny bundle of 8 frames with every frame dumped: 23 members, 281 MB; Huge: 30 members, 544 MB (8 overlay textures
  of 22.4 MB, one per frame while the overlay animates) [L, section 15].
- Recording with `_dump=all` spaces the span's draws about 250 ms apart (two 14.4 MB readbacks per frame): a span of 8
  frames is about 2 s of game time [L, section 15].
- A RenderDoc catalogue capture: 91-148 MB, its `--select all` export 242-340 MB; at most 6 unit frames or 2 city
  frames per game process at the guard's 100 MB floor; the 24-capture catalogue with its exports and retaken attempts
  is 15.0 GB on F: [L, `scene-catalogue.md` 5, 6].

## 1. The manifest `scene.json`

A bundle is a folder holding `scene.json` and its members (section 2). The JSON is UTF-8 and the keys are fixed. The
player and the checker refuse a bundle with a missing key, an unknown `format`, or an older `format_version`. They
refuse it with an exit code and never skip it (plan 4.1 rule 3). Proposed version 1:

```json
{
  "format": "c5-scene-bundle",
  "format_version": 1,
  "bundle_id": "C5H-0301-TINY_0009_20261115-101500_workstation",
  "created": "2026-11-15T10:15:00-05:00",
  "recorder": {"keys": {"render_test_record": "F:/...", "render_test_record_frames": "8", "...": "..."},
               "server_exe_sha256": "...", "repo_commit": "<git rev-parse of the build>", "build_config": "release|fast|debug"},
  "pins": {"i0_lock": {"commit": "26234371e...", "build": "i0ship-26234371-ia"},
           "dll_lock": {"commit": "04b1211b8...", "layout": "x64-04b1211b-pa803776", "dll_sha256": "7458..."},
           "server_sha256": "...", "proxy_sha256": "...", "composite_sha256": "...", "exe_sha256": "..."},
  "save": {"name": "C5H-0301-TINY_0009", "sha256": "...", "map_w": 56, "map_h": 36, "turn": 9, "load_seq": 1},
  "asset_set": {"vt_present": true, "vt_folder_hash": "...", "install": "%USERPROFILE%/c5-instances/c5h-a/install",
                "machine": "workstation"},
  "adapter": {"luid": "0000...", "description": "NVIDIA GeForce RTX 4080", "driver": "..."},
  "render_config": {"render_pass": -1, "render_tess": 0, "render_tile_plots": 0, "render_water": "full",
                    "render_water_freeze": 0, "render_overlay": 1, "render_tiles": "bake", "render_baker": "pipeline",
                    "render_bake_radius": -1, "...": "every resolved HostConfig value the scene reads"},
  "frames": {"first_id": 1762, "count": 8, "dumped": [1762, 1775]},
  "members": {"heightfield/load0001.c5hf": {"kind": "heightfield", "version": 1, "bytes": 27525248, "sha256": "..."},
              "water/load0001.wtap": {"kind": "water_tap", "version": 1, "bytes": 0, "sha256": "..."},
              "overlay/1a2b3c4d5e6f7a8b.ovl": {"kind": "overlay_texture", "version": 1, "bytes": 22369748, "sha256": "..."},
              "feed.bin": {"kind": "feed", "version": 1, "...": "..."},
              "frames.bin": {"kind": "frames", "version": 1}, "cameras.bin": {"kind": "camera_blocks", "version": 1},
              "water_clock.bin": {"kind": "water_clock", "version": 1},
              "dumps/1762.bmp": {"kind": "dump_colour"}, "dumps/1762.depth.f32": {"kind": "dump_depth"},
              "cameras/default.json": {"kind": "named_camera"}, "refs/default.png": {"kind": "stock_shot"}},
  "version_notes": ["gap: bake-cache tap left out (render_tiles=bake)",
                    "gap: epic 0168 fog inputs (FOW mask, cloud planes, FOW LUT) not on main at b81d4dae"],
  "counters": {"water_refused": 0, "overlay_refused": 0, "overlay_stale": 0, "feed_overflow_events": 0,
               "record_errors": 0}
}
```

Rules [I, proposed]:
- The SHA-256 of every member is in `members`. The checker hashes every file and refuses on a mismatch, a missing file,
  or a file present that is not listed.
- The pins follow COMMON 18, from `deps/i0.lock`, `deps/dll.lock` and the hashes of the binaries the run really loaded.
- `asset_set` uses D94's method: `tools/visualcheck/visualcheck.py` `asset_set()` (VT present, the VT folder hash by the
  method of 9047, install, machine). Call the function or port it; do not invent a second hash.
- `version_notes` lists every renderer input on `main` that has no member (section 3). A merged input without a member
  is reported to the lead and the MC in the same hour.
- The bundle is game data (EXE memory, pictures made from the game's files) and is never committed (D83). Its numbers
  (counters, hashes, sizes) may go into evidence records.

**As built (3911, 2026-11-15) [C].** `format_version` is **2**: the fog's members (2.11). The version-1 note of 0168's
fog in the example above is no longer written; a version-1 bundle is refused as older.
**As built (0364, 2026-11-15) [C].** `format_version` is **3** (corrected; replaces "2" above as the current version): the
decal member (2.10) and the shadow and ice members (2.12). A version-2 bundle is refused by the checker and the player as
`OlderVersion` (exit 26) with `format_version 2 is older than 3 (refused, not read)`.
**As built (4457, 2026-11-16) [C].** `format_version` is **4** (corrected; replaces "3" above as the current version):
feed.bin's file form 2 with the unit feed's records 3-6 (2.5, section 20). A version-3 bundle is refused as
`OlderVersion` (exit 26) with `format_version 3 is older than 4 (refused, not read)`; the feed member's `version` is 2
(`MemberKindVersion(Feed)`), every other member's 1.
**As built (0440, 2026-11-16) [C].** `format_version` is **5** (corrected; replaces "4" above as the current version):
the member kinds `units_db` (`units/gen<NNNNNNNNNN>.db`) and `units_history` (`units_history.bin`), with
`render_units=1` only, and `render_config`'s `render_units_anim` (section 22). A version-4 bundle is refused as
`OlderVersion` (exit 26) with `format_version 4 is older than 5 (refused, not read)`.

**As built (3902, 2026-11-15) [C: `server/src/render/srv_scene_bundle.{h,cpp}`, tested by
`server/tests/render/scenebundle/bundle_format_test.cpp`].** Where the built manifest differs from the proposal above:
- `asset_set` holds exactly the dict of `visualcheck.py asset_set()`: `vt` (one of "present", "absent", "packages only",
  "present (other hash)", "unknown"), `vt_hash`, `vt_files`, `install`, `machine`, `why` ("" when none). It replaces the
  proposal's `vt_present`/`vt_folder_hash`. `ComputeAssetSet` is the C++ port (VT folder hash by 9047's method); the
  suite's runner checks it against the Python function on three synthetic installs (absent, present, missing).
- Every value in `recorder.keys`, `pins` and `render_config` is a JSON string (`"render_pass": "-1"`). `pins` is nested
  at most one level (`"i0_lock": {"commit": ...}`, `"server_sha256": ...`). The manifest has no number with a fraction.
- Every member entry has all four keys `kind`, `version`, `bytes`, `sha256` (64 lowercase hex). The member's `bytes`
  and SHA-256 are checked before any parser reads it.
- `counters` has exactly the five names. Unknown keys are refused at every level, and so are duplicate keys.
- `format` and `format_version` are checked before the other keys, so an older bundle is refused as `OlderVersion`, not as
  a key it lacks. Version 1 is the first, so "older" is `format_version` 0.
- Member paths are fixed per kind: `feed.bin`, `frames.bin`, `cameras.bin`, `water_clock.bin` exactly;
  `heightfield/*.c5hf`, `water/*.wtap`, `overlay/<16 lowercase hex>.ovl`, `dumps/<frameId>.bmp`,
  `dumps/<frameId>.depth.f32`, `cameras/<name>.json`, `cameras/<name>.vpcam`, `refs/*.png`, `refs/*.rdc`,
  `refs/<camera>.export/<anything>`. A path with `..`, `.`, an empty segment, `\` or `:` is refused (`BadPath`).
- An incomplete bundle has a `version_notes` entry starting with `incomplete: ` and no members; the checker refuses it
  (`Incomplete`).
- Cross-checks: `frames.bin` and `cameras.bin` hold `frames.count` records and the first one is `frames.first_id`. Every
  dumped frame is in the span, has bit 0 of its flags set, and has both dumps, whose sizes are its target's width and height.
  Every bound overlay of a frame is a member, and an overlay's file name is the first 16 hex digits of its own SHA-256.
- Refusal codes and exit codes: `RefusalExitCode` = 20 + the code (21 NoManifest .. 42 BadValue); the tool's output line is
  `scenebundle: REFUSED <Name> <member or scene.json>: <why>`. Added codes: `BadPath`, `BadRecord`, `BadValue`.
- Not enforced by the checker: at least two dumped frames (the recorder's key parse refuses fewer, section 9); that a feed
  holds a PLOTS record at all (3903's file feed refuses a feed without its tables, 4.5).

## 2. Each member's file form

All binary files are little-endian. Each starts with a 16-byte header: 4-byte magic, u32 version, u32 header bytes, u32
reserved 0 [I, proposed]. The reader checks magic, version, sizes and the SHA-256 from the manifest before it trusts a
byte. Each rule below names the reader it must pass.
As built (3902, 2026-11-15) [C]: the header is as proposed, `headerBytes` is always 16, and each form's own fixed
fields follow it. A file form version other than 1, or a reserved word other than 0, is refused (`BadVersion`).

### 2.1 Height field: C5HF version 1, unchanged [C]

`heightfield/load<NNNN>.c5hf` is exactly the tap's payload (`render/include/c5r/heightfield/heightfield.h`: the 128-byte
header, u16 heights, then the byte layer when flag bit 1 is set), cut to `tapSize(mapW, mapH, withMaterial)`, because a
shared section is page-rounded. `c5h::readTap` accepts it unchanged, and `shell_server.exe --save-tap` and
`terrainshot --c5hf` already write and read this form. There is one file per accepted load (`loadSeq` in the name). Tiny
(56 x 36 plots, a grid of 56 x 40 plot rows) holds 3,584 x 2,560 samples: 18,350,080 B of u16, plus 9,175,040 B of byte
layer, plus 128 = 27,525,248 B [C arithmetic].
As built (3902, 2026-11-15) [C]: as proposed. `ParseHeightfield` runs `readTap`, and it also refuses a file that is not
exactly `tapSize()` bytes (`WrongSize`). The bundle's file has no page-rounding tail.

### 2.2 Water tap: the section's image as the reader accepted it [C for the checks, I for the form]

`water/load<NNNN>.wtap`, magic `C5WT` version 1:

| Offset | What |
|---|---|
| 0 | the 16-byte header |
| 16 | the `VPShellWaterTap` block as `SnapBlock` returned it (`shell/common/shell_shared.h`; its size is stored and checked) |
| then | the camera block's grid values the check used: `haveCam` u32, `gridSeq` u32, `samplesX` u32, `samplesY` u32 |
| then | the image: `tb.imageBytes` bytes copied from the mapped view before `UnmapViewOfFile`, `VPShellWaterImage` header first |

The file source runs the same `CheckImage` (magic, version, header and record sizes, counts, every record's FNV-1a 64,
the image hash, VB and IB sizes, every index of every draw inside its VB) and the same map-size check against the stored
grid values. So the records `WaterTapReader::records()` yields from the file are those it yielded from the section, byte
for byte. Nothing is re-encoded: the records' `dataOffset` keeps pointing into the image, as in the section. There is one
file per accepted copy (load and copy sequence). A refused copy is not written; it is only counted in the manifest.
As built (3902, 2026-11-15) [C]: header; a u32 `tapBlockBytes`, which must be 320 (`sizeof(VPShellWaterTap)`, asserted at
compile time); the block; the four grid u32s; then the image at byte 356 (`kWaterTapFixedBytes`). The format library checks
the following, and nothing else. The block size. That the file ends exactly at the end of the image (`Truncated` /
`WrongSize`). The image header's magic `WATR`, version 1, header and record sizes 120 and 128, and its `imageBytes`
against the block's. The image's record count and load against the block's (`BadRecord`). The records and hashes are
checked by `CheckImage` through 3903's file entry `WaterTapReader::loadImage`.
*(Added 2026-11-23, ticket 0622: a later copy of a load is written as `water/load<NNNN>-c<copySeq>.wtap`; from format
version 11 `water_frames.bin` says which copy each frame was drawn with, and the player switches at that frame
(section 42). Before, the player drew the load's last copy for every frame.)*

### 2.3 Overlay texture and its OverlayProj [C for the reader, I for the form]

- **What the reader holds [C].** `OverlayTapReader::acquire` opens each slot's shared texture by name: 2048 x 2048,
  R8G8B8A8 typeless family, 12 mips, an R8G8B8A8_UNORM SRV over all mips. With it comes the slot's `numViews` (1..4),
  `overlayProj[4][16]`, `generation` and `presentCount`. The producer copies the whole resource with `CopyResource`
  (`overlay-system.md` 7: all 12 mips, 22.4 MB). The overlay's bytes are therefore a GPU resource on the server's device.
- **Size [C arithmetic].** Mip 0 is 2048 x 2048 x 4 = 16,777,216 B. All 12 mips together are 22,369,620 B. Record all 12:
  the server samples the EXE's own mips (from the EXE's `GenerateMips`), and regenerating them on another device would
  make the player's picture depend on that driver's filter.
- **How the recorder reads it back [I].** The recorder wraps the scene's `OverlaySource` (section 4.3). When a new
  generation is acquired, it calls `CopyResource` from the slot's texture into one of a ring of three STAGING textures
  (2048 x 2048, 12 mips, R8G8B8A8_UNORM), on the render thread's immediate context, in the same loop iteration as the
  draws that sample it. Each staging texture is mapped two iterations later, so the render thread does not stall on the
  GPU. The slot is safe for that copy as long as `inUseSlot` names it. The copy is queued in the same iteration as the
  scene's own sampling, under the same protection; there is no fence between the EXE's device and the server's (the
  limit the reader already lives with, `srv_overlay_tap.h`).
- **When it is recorded.** Per new generation within the recorded span, deduplicated by content. Each distinct texture
  is written once, as `overlay/<first 16 hex of its SHA-256>.ovl` (magic `C5OV` version 1: the header, then width,
  height, mips, DXGI format, the 12 mip offsets and tight row pitches, then the 12 mips tight, 22,369,620 B of texels).
  Each frame's record in `frames.bin` names the texture by its full SHA-256, together with the pair's
  `numViews`/`overlayProj`/`generation`/`presentCount`. The selection ring's per-frame rotation and scale differ in
  every one of epic 0167's stock captures (`overlay-system.md` 2): it animates. With a unit selected, each frame
  therefore brings a new texture [I]:
  22.4 MB per frame. A span of 8 frames is about 179 MB, and 60 frames about 1.34 GB [arithmetic]. The default span
  stays short (section 9). Without a selected unit, identical copies are written once.
- **The file source [I].** It creates an IMMUTABLE texture from the 12 mips with the same SRV description, so that
  `OverlayFrame` and `Scene::draw` bind it exactly as they bind the live one.
- **As built (3902, 2026-11-15) [C].** The file is laid out as follows. The header. Then u32 width, height, mips and
  dxgiFormat. Then u32 `rowPitch[12]` and u32 `offset[12]`, each offset from the start of the file. That is 128 B of
  header and tables. The texels follow tight from byte 128, mip 0 first: 22,369,620 B, so the file is 22,369,748 B. (The
  manifest example said 22,369,684 B before 3902 and is corrected.) Refused: mips other than 12 (`BadMipCount`, checked first); a size other
  than 2048 x 2048; a format other than 28 (R8G8B8A8_UNORM); a row pitch or offset that is not the tight layout; bytes
  after the last mip (all four `WrongSize`). Also refused: a short file (`Truncated`), and a file name that is not the first
  16 hex digits of the file's SHA-256 (`ShaMismatch`).
  In `frames.bin` a bound frame names the texture by its full SHA-256, and the checker refuses the bundle when that
  member is absent.

### 2.4 Bake-cache tap: left out [C]

`ExeCacheTap` (`srv_exe_cache_tap.*`) is the tile source only with `render_tiles=tap`, the EXE's caches used as the test
reference. The default `bake` uses the server's own `c5r::bake::Residency`, whose inputs are the height field, the feed's
changed plots (section 2.5), the install and the EXE's shader section. A bundle of the product's path needs no member for
it. It carries `"render_tiles": "bake"` in `render_config` and the note in `version_notes`. An optional reference set
for `bakecompare` (plan 4.1) would be a later member kind, `bake_ref` (version 2). It is not needed for "offline equals
live".

### 2.5 The feed: drained batches and the plot tables the render thread read [C for what, I for the form]

`feed.bin`, magic `C5FD` version 1: a sequence of typed records, each `u32 type, u32 bytes, payload`:

| Type | Payload |
|---|---|
| 1 DRAIN | `loopIter` u64 (the host loop's iteration), `nextFrameId` i64 (the camera frameStamp of the next draw, -1 when none followed), then the `Batch` as drained: `snapshotGen`, `width`, `height` u32, `dropped` u64, `nEvents` u32 and `Event[nEvents]` (32 B each, as `srv_feed.h`, including `kSnapshot` 100, `kOverflow` 101 and `kUiMirror` 102), `nChanged` u32 and `u32[nChanged]` |
| 2 PLOTS | `loopIter` u64, `caller` u32 (1 = `ResidencySource::takeBaseline` at `onField`, 2 = `onPlotsChanged`), `snapshotGen`, `width`, `height` u32, `full` u32; full = 1: `PlotRecord[width*height]` (20 B each); full = 0: `nDiff` u32, then `(u32 index, PlotRecord)` pairs of every record that differs from the previous PLOTS record |

Only drains that returned true are written: `Drain` returns false and leaves the batch empty when nothing changed.
Sizes: a full Tiny table is 2,016 x 20 = 40,320 B. The huge save (128 x 80) is 204,800 B; there, diffs keep a span
small [arithmetic]. The file source yields, at each call, exactly what the live calls returned, in the recorded order.
Why the PLOTS records are needed: section 7.
As built (3902, 2026-11-15) [C]: the records are as proposed. The DRAIN payload is 40 + 32 n + 4 + 4 m bytes. The PLOTS
payload is 28 B of fields, then a full table of 20 B per plot or a diff of 4 + 24 n bytes. Fields are packed with no
padding. The diff indices strictly increase. A diff must have the width and height of the previous table, and it is taken
against the previous PLOTS record, full or diff. `ParseFeed` resolves every PLOTS record into the whole table
(`FeedRecord::plots`). `FeedEncoder::addPlots` writes a full table the first time, whenever the generation or the size
changes, and whenever a diff would not be smaller. Refused: no record (`EmptyFeed`); a diff before any table
(`PlotsDiffBeforeFull`); an unknown type, a caller other than 1 or 2, a size above 256, a `loopIter` that goes back, or a
diff index out of order or out of range (`BadRecord`); a payload whose size disagrees with its counts (`WrongSize`); a
record past the end of the file (`Truncated`). The library does not check event contents or changed-plot indices.
As built (3911, 2026-11-15, format version 2) [C]: PLOTS caller 3 = the fog scene's table reads (2.11); a caller other
than 1, 2 or 3 is `BadRecord`. No new record type: types 3 UNITS, 4 PATHS and 5 COMBATS are epic 0327's (lead #1469),
6 is the next free one.
As built (4457, 2026-11-16, format version 4, MC #2174) [C]: feed.bin's header version is 2 (a form-1 file:
`BadVersion`, "file form version 1, this reader reads 2"). DRAIN gains `unitTableGen` u32 and `drainMs` f64 after
`dropped` (payload 52 + 32 n + 4 + 4 m). Types 3 UNITS, 4 PATHS, 5 COMBATS, 6 PLAYERS are epic 0375's unit feed
(no code on this branch writes or reads feed.bin types 3-5 but 4457's, grep of models/, render/, tools/, server/ 2026-11-16; the numbers follow `unit-server-scene.md` 11, decision 11); their
payloads, rules and placement: section 20 and `unit-server-scene.md` 11.1. `ParseFeed` folds them into their DRAIN and
still yields only DRAIN and PLOTS records.
(Added 2026-11-17, ticket 0493, format version 7) [C]: the file form is unchanged, but the ORDER of the records is the
render thread's call order, and the player replays it strictly. Since f916762a5 (4555's follow-up) PLOTS caller 2 also
comes from `ResidencySource::onFeedGeneration` (from `Scene::fowFeed`, at each new snapshot generation the residency has
not taken), before the fog's caller-3 read of the same drain. A change to the reads raises the format version: section 24.

### 2.6 Cameras [C for the block, I for the form]

- **Per drawn frame:** `cameras.bin` (magic `C5CB` version 1) holds the `VPShellCamera` block (`shell_shared.h`) of
  each recorded frame, as `Scene::decide` took it with `SnapCamera`. It includes `presentCount`, the status, the views,
  `camWorld`, the viewport, the composite's `bbW`/`bbH` and the grid constants (`gridSeq`, `wrapPeriod`, `h2w`, `sx..oy`,
  `samplesX/Y`). The block size is stored and must equal `sizeof(VPShellCamera)`, checked at compile time. It is 712 B
  [C, 3902's `static_assert`, 2026-11-15; replaced "about 708 B by counting the fields [I]"].
- **Named cameras:** `cameras/<name>.json` in today's form (`shell/tools/gt_reader.py camera --pid PID --out F`, read
  from the EXE's memory), plus `cameras/<name>.vpcam`, the `VPShellCamera` block of the recorded frame taken at that
  camera. The player draws a named camera from its `.vpcam`. The `.json` is the stock side's record (what
  `i2_capture.py` and the stock shots use) and holds no grid constants.
- **As built (3902, 2026-11-15) [C].** `cameras.bin` and `.vpcam` share one form, magic `C5CB`: the header, then a u32
  `blockBytes`, then the blocks. `sizeof(VPShellCamera)` is **712 B** (the compiler's `static_assert`; the count above
  said about 708). A `blockBytes` other than 712 is refused (`WrongSize`), and so is a partial block (`Truncated`). A
  `.vpcam` must hold exactly one block (`WrongSize`). A `cameras/<name>.json` is only checked to be a JSON object
  (`BadJson`).

### 2.7 The clock value per frame and the water clock state [C for the sources, I for the form]

- `frames.bin` (magic `C5FR` version 1) has one fixed-size record per drawn frame of the span: `frameId` i64 (the camera
  block's frameStamp), `loopIter` u64, `nowMs` f64 (the value `Scene::draw` passes to `WaterScene::decide`, section 6),
  target `width`/`height` u32 (the sink's targets), the overlay pair (`bound` u32, `generation` u32, `presentCount` u64,
  `numViews` u32, `overlayProj` f32[4][16], the texture's SHA-256 [32 B]), and `flags` u32 (bit 0 dumped, bit 1 the
  scene drew water with the module, bit 2 waves drawn).
- `water_clock.bin` (magic `C5WC` version 1) is the water scene's clock at the first recorded frame, before that frame's
  draw: `clockStartMs`, `waveLastMs`, `lastNowMs` f64, `nWaves` u32 and `WaveRecord[nWaves]` (16 B each,
  `c5r/water/waves.h`). The player seeds the water scene with it and then passes each frame's `nowMs` in turn (section 6).
- **As built (3902, 2026-11-15) [C].** `frames.bin` is the header, then a u32 `recordBytes` (344), then
  `FrameRecord[]`. That struct (`srv_scene_bundle.h`, 344 B, 8-byte packing) holds, at these offsets:
  - 0 `frameId`, 8 `loopIter`, 16 `nowMs`, 24 `width`, 28 `height`;
  - 32 `overlayBound`, 36 `overlayGeneration`, 40 `overlayPresentCount`, 48 `overlayNumViews`, 52 `overlayProj[4][16]`;
  - 308 `overlaySha256[32]` (binary), 340 `flags`.

  Refused: another record size (`WrongSize`), a partial record (`Truncated`), and the following `BadRecord` cases: a
  `frameId` that does not increase, a `nowMs` that is not finite, `numViews` above 4, `bound` above 1, and flags above
  bit 2. (As built 3911, 2026-11-15, format version 2: flag bit 3 = the fog drawn, 2.11; flags above bit 3 refused.)

  `water_clock.bin` is the header, then f64 `clockStartMs`, `waveLastMs` and `lastNowMs`, then u32 `nWaves` and a u32
  `waveRecordBytes` (16, added so the size is checked), then the waves. The parser returns the bundle's own
  `WaterClockFile`; 3903 converts it to its `WaterClockState`. Refused: a wave record size other than 16, or bytes after
  the waves (`WrongSize`); a short file (`Truncated`); a value that is not finite (`BadRecord`).

### 2.8 The server's own pictures of the recorded frames [C for the gap, I for the form]

`dumps/<frameId>.bmp` (32-bit top-down BMP) and `dumps/<frameId>.depth.f32` (raw float32 rows) are the forms of
`render_test_dump` (`V3Sink::dump`). That dump exists only in the v3 sink. The product runs `render_handover=ring`
(`server/README.md`), and `render_test_dump` writes one frame only. So the recorder reads the drawn colour and depth
targets back itself, sink-independent: in `DrawFrame`, after `Scene::draw` and before `sink->end`, for the frames chosen
by `render_test_record_dump` (at least two per bundle, done item 1). These dumps are the "live" side of "offline equals
live". The pixels are the scene's target format (the v3 path's BMP writes 4 bytes per pixel as stored). 3905 compares
them byte for byte with the player's dump of the same frame before it writes any PNG.
As built (3902, 2026-11-15) [C]: `ParseDumpColour` accepts exactly `V3Sink::dump`'s form and nothing else:
- the BITMAPFILEHEADER and a 40-byte BITMAPINFOHEADER, with pixel data at offset 54;
- one plane, 32 bits per pixel, BI_RGB, a negative height;
- file size and `bfSize` both 54 + w·h·4.

Any other header is refused as `BadVersion`, a short file as `Truncated`, a long one as `WrongSize`. A depth dump must be
whole float32 values. The checker compares both dumps' sizes with the frame record's `width` x `height`.
`EncodeDumpColour` writes the same form. The depth dump is named `dumps/<frameId>.depth.f32`; V3Sink appends
`.depth.f32` to the colour path instead, so the recorder names it itself.

### 2.9 Stock references

`refs/<camera>.png` is the harness shot at the named camera (`cli.py ... shot --what ...`, COMMON 27). Where one was
taken, `refs/<camera>.rdc` with its `rdc-export` folder `refs/<camera>.export/` sits beside it (only on Tiny:
`renderdoc-capture.md` 4). Every harness picture is recorded with `visualcheck.py record ... --what "DIAGNOSTIC: ..."`
(D138). The bundle holds the files; the Visual Checks store holds the annotation.
As built (3902, 2026-11-15) [C]: the `.png` is checked for the PNG signature and the `.rdc` only for being non-empty.
RenderDoc's file magic is not established here, so the checker does not test it. The export folder's files are hashed
and listed but not parsed (kind `stock_export`).

### 2.10 The decals: epic 0166's decal tap (format version 3, task 0364, 2026-11-15) [C for the code and the tests, L where named]

Epic 0166 made the render thread read the decal tap (`srv_decal_tap.{h,cpp}`, `Scene::decalPoll`, the residency's
`setDecals` / `decalChanges`; `research/05-graphics-composition/terrain-decals.md` 10, 12). The form is 0166's own file
form (`terrain-decals.md` 10.4), unchanged; this section is its bundle side as built.

- **Member** `decals/load<NNNN>.dtap` (kind **`decal_tap`**, form version 1; manifest entry with `kind`, `version`, `bytes`,
  `sha256` as every member): the decal tap's image bytes as the reader accepted them (`VPShellDecalImage` first: magic
  `C5DT`, version 1, `headerBytes` 112, `decalBytes` 48; exactly `imageBytes` long; no 16-byte member header of 2.2-2.7,
  the image carries its own). `NNNN` = the image's `loadSeq`, exactly four digits (`DecalTapPath`; a load above 9999 has
  no name and the recorder marks the bundle incomplete). At most one per bundle: the set in force at the span's first frame.
- **Checks** (`ParseDecalTap`, `srv_scene_bundle.cpp`): shorter than 112 bytes or than `imageBytes` `Truncated`; magic
  `BadMagic`; version `BadVersion`; header or record size `WrongSize`; longer than `imageBytes` `WrongSize`; then the
  reader's own file entry on the same bytes (`DecalTapReader(0).loadImage`, the code `loadFile` runs after reading the
  file: every image check of `terrain-decals.md` 10.1 without the control block's) refuses as `BadRecord` with the
  reader's reason; the file name's `NNNN` not the image's `loadSeq` `BadRecord`. Cross-checks (`CheckBundle`):
  `render_config` `render_decals` "1" with no decal member and no version note starting `decals: no decal set in force at
  the span's first frame` is `MissingMember` (member named `decals/load<NNNN>.dtap`); a decal member while `render_decals`
  is not "1", or a second one, is `BadValue`.
- **`render_config`** (the host's `RecorderRenderConfig`): `render_decals` = "1" when `render_decals` is on and
  `render_tiles=bake` (the decals live in the server's bake), else "0"; `render_decal_mods` = the mods' folder the scene's
  decal names and textures are read from (`ModFolders`: `C5_DOCUMENTS`, else Documents, + `\My Games\Sid Meier's
  Civilization 5\MODS`; on a harness instance its own Documents), "" with decals off.
- **Recorder** (`SceneRecorder::setDecalReader`, given `Scene::decalReader` by the host's `StartRecorder` when decals are
  on): in `beforeDraw` of the span's first frame, before its draw, the reader's set in force is written with
  `DecalTapReader::writeCurrentImage` into a temporary file (`%TEMP%/c5_scene_decal_<pid>_<qpc>.dtap`, deleted after),
  read back, checked with `ParseDecalTap`, its generation compared with the reader's, and added as
  `decals/load<NNNN>.dtap`; any failure is a record error and marks the bundle incomplete. No set (or no reader yet): the
  version note `decals: no decal set in force at the span's first frame (frame F, ...): the span is drawn without
  decals` and no member. Each later frame of the span: a reader generation other than the first one's is noted once:
  `decals: decal generation changed inside the span: generation A at the first frame, B before frame F (version 1 of the
  member holds the first set only: frames from F on drew another set)`; such frames are not P1.
- **Player** (`LoadDecalMember`, `srv_scene_sources.cpp`; sceneshot): with `render_decals` "1" the scene is built with
  decals on (the residency's decal pass, the decal names' view of the install with the mods' folders above it); the
  mods' folder is `render_decal_mods` (or `--mods DIR`), handed to the scene through `C5_DOCUMENTS`; a folder not ending
  in `\My Games\Sid Meier's Civilization 5\MODS` is exit 2, a missing one exit 9; with none recorded `C5_DOCUMENTS` is set
  to a folder that does not exist, so the player's own Documents are never read. Before the field's load (so the
  field's first bakes read the set, as the live residency's re-bakes did), the member is read (`ReadMember`: size and
  SHA-256), parsed and handed to `Scene::loadDecalImage`: the scene's decal reader (one without a tap,
  `DecalTapReader(0)`, made there) takes it through its file entry and the set goes to the residency's `setDecals`; a
  name the install and the mods' folders do not hold is exit 8 with the name. `render_decals` not "1" (or `--set
  render_decals=0`): no set is applied.
- **Not in version 1 of the member [I, open]:** the generations applied inside a span (noted, above). A later form would
  name each generation with the loop iteration that applied it.
- **Tests [C, 2026-11-15, plain and ASan: the numbers in issue 0364's log].** `bundle_format_test.exe`
  (`TestDecalMember`): written and read back (manifest entry `decal_tap` with bytes and SHA-256, the member byte-equal,
  `format_version` 3 in `scene.json`), `render_decals` 1 with the no-set note accepted, 0 without a member accepted; each
  refusal through the child process (exit code, line, nothing written) and in process: truncated, shorter than the header,
  a byte after, wrong magic, version 2, header 116, record 52, a texture index and a triangle count the reader refuses,
  the name of another load, listed but missing, `render_decals` 1 without member or note, a member with `render_decals` 0,
  two members; and paths of another form refused by the writer. `recorder_test.exe` (`TestDecals`): a reader with
  generation 5 at the first frame and 6 before the third: the member is the generation-5 image byte for byte and the
  change is noted; a reader without a set and no reader at all: the no-set note and no member; the checker accepts all
  three; no temporary file left. `sceneshot_test.exe` (`CaseDecals`, WARP and the RTX 4080; the stand-in baker draws no
  decals, so the case asserts the hand-over): the live side applies the set before its field, the player prints the
  recorded mods' folder, its hand-over line, the scene reader's `applied from a file` and the residency's `in use` lines,
  P1 equal; `--set render_decals=0` applies none; `--mods` not a MODS folder exit 2; the recorded mods' folder missing exit
  9; a texture name the install lacks exit 8 naming it; the no-set note plays without a set.

### 2.11 The fog of war: epic 0168's inputs (format version 2, task 3911, 2026-11-15) [C for the code, L where named]

(2.10 was reserved for 0166's decal member; written by 0364 on 2026-11-15.) Epic 0168 (merged to `main` at 172a715a) made the render thread read the
fog's inputs. Rule A (D137) asks for a member, or a `[C]` statement that the input is derived from members already
recorded, with a test that proves the replay equal. Read in `srv_fow_scene.{h,cpp}`, `srv_render_scene.cpp` (`init`,
`decide`, `draw`) and `render/include/c5r/fow/fow.h` of the merged tree; `render/` is unchanged. Every input:

| Input of the fog (`FowScene`) | Read where | In the bundle |
|---|---|---|
| The plot table's fog bits (`PlotRecord::fow`) through `FowPlotTable::copy` = `SrvFeed::CopyPlots` | `FowScene::readTable`: at the fog's start (`Scene::init`), and from `onBatch` at a new snapshot, a sequence gap, an overflow, dropped events or changed plots | **member: `feed.bin` PLOTS records of caller 3** (`kPlotsFog`). These reads are not the residency's (callers 1 and 2): they happen at other times (the first one before any field, in `Scene::init`) and may see another table, so the fog's own reads are recorded (contract 7's argument applies to them unchanged) |
| The feed's drained batches (events, `seq`, `kOverflow`, `dropped`, `changedPlots`, `snapshotGen`) | `Scene::fowFeed` -> `FowScene::onBatch`, in the host's `Loop` right after `plotsChanged` | already in `feed.bin` DRAIN (2.5); the player calls `fowFeed` after `plotsChanged` as the host does |
| The overlay pair (numViews, OverlayProj, generation, slot) | `FowScene::admit` (from `Scene::decide`) | already in `frames.bin` (2.7); the player calls `Scene::fowAdmit` (decide's fog step) with each frame's pair |
| The camera block (views, projection, the world box, wrap, grid constants) | `admit`, `beforeWorld` | already in `cameras.bin` (2.6) |
| The height field's grid (map size, M_h2w, sx, sy, ox, oy, samples) | `admit` (the table must be mapW x (mapH - 4)), `beforeWorld` (the placement, the world box through `c5r::water::worldFromGrid`) | already in `heightfield/*.c5hf` (2.1) |
| FOWAnim's clock T: `m_clockS` += min(max(0, nowMs - last), 250 ms) over every frame the fog drew since the server started | `beforeWorld` | **member: `fog_clock.bin`** = (T, the last fog frame's nowMs) right before the span's first frame, plus each frame's `nowMs` (`frames.bin`). Before 3911 `Scene::draw` passed the wall clock to `beforeWorld` itself; now it passes `SceneFrame::nowMs` when set (the water's rule, 6), the wall clock otherwise (MC #1450) |
| `render_fow` (0 / 1) and FOWLevel (`render_fow_level`, else the player's `GraphicsSettingsDX11.ini`, else 3) | `ParseHostConfig`, `FowScene::init` | **`render_config`**: `render_fow`, and `render_fow_level` = the level the live fog resolved (`FowScene::level()` after `Scene::init`), so the player reads no ini of its own machine |
| The mask (`MaskRenderer`: hexagons per plot of each view's range, MAX blend, mips) | `beforeWorld` -> `buildMask` | **derived [C]**: a function of the states above, the pair's OverlayProj, the camera's view x projection, the grid's placement and FOWLevel's size; rebuilt only when the fog generation or the pair changed, from the same inputs, so it needs no member. Tested by the replay (below) |
| The cloud planes (FOW CULL on a copy of the depth, three FOWBaseLayer planes) | `afterWorld` -> `drawClouds` | **derived [C]**: the mask, the same geometry, FOWAnim and the install's `fogcloudtile_dif_01..03.dds` |
| The cloud base t5 (`fogcloudtile_dif_02.dds`), the planes' textures, the TerrainFOW LUT t6 (`TerrainFOW_<Style>_ColorKey.tga` through `bake::buildColorKeyLut`) | `CloudRenderer::init`, the terrain module | not members (D83): the install, as section 3 row 10 (`asset_set` and `install` in the manifest) |
| The FOW programs (FOWDrawMask passes 0-3, FOWBaseLayer) | `MaskRenderer::init`, `CloudRenderer::init` | not members: the EXE's `.shader` section, as row 11 (`pins.exe_sha256`) |
| The wall clock in `admit` (the install's 10 s wait, the held-frame notes) | `Scene::decide` | not recorded: it times the install wait only, never a pixel [C]; the player passes the frame's `nowMs` |

**The forms (as built) [C: `srv_scene_bundle.{h,cpp}`, tested by `bundle_format_test.cpp`].**
- `scene.json` `format_version` **2**. A version-1 bundle is refused as `OlderVersion` (exit 26) by the checker and the
  player: every bundle recorded before 3911 is refused once 3911 is merged.
- `fog_clock.bin` (kind `fog_clock`, magic `C5FC`, form version 1): the 16-byte header, f64 `clockS` (T in seconds,
  >= 0), f64 `clockLastMs` (-1 when the fog drew no frame yet); 32 bytes. Refused: shorter (`Truncated`), longer
  (`WrongSize`), a value not finite or T below 0 (`BadRecord`), another form version (`BadVersion`), another magic
  (`BadMagic`). Written by the recorder when the fog is not OFF at the span's first frame (render_fow=1).
- `feed.bin` PLOTS: caller 3 accepted (1, 2 or 3; 4 is `BadRecord`); the record's form is unchanged (2.5).
- `frames.bin` flags: bit 3 = the fog's mask was bound in the draw (`FowScene::terrainInputs().on` after it); bits above
  3 are `BadRecord`. The player reports a frame whose fog it does not reproduce as `MISMATCH ... the fog drawn (flag
  bit 3)` (exit 8 at the end).

**The recorder [C: `srv_scene_recorder.{h,cpp}`, the host's 0301 blocks].** `StartRecorder` (before the first device)
gives the fog the recording feed (`FowScene::setTable(MakeFeedSourcePlotTable(feed))`; `Scene::init`'s fog start then
reads its table through it, as PLOTS caller 3 of loop iteration 0) and the fog scene to the recorder (`setFog`).
`RecorderAfterInit` replaces `render_fow_level` in `render_config` by the level the fog resolved. `beforeDraw` of the
span's first frame takes `FowScene::clockState()`; `finish` writes it as `fog_clock.bin`. `RecorderAfterDraw` sets flag
bit 3. The version note "gap: the fog inputs of epic 0168 ..." is no longer written.

**The player [C: `sceneshot.cpp`].** With `render_fow=1` in `render_config`: `fog_clock.bin` and `feed.bin` are required
(else exit 8 before anything is drawn); the fog's table is the file feed's before `Scene::init`; after the file overlay
source is set the fog is started again with the overlay on (`RestartFowWithOverlay`: `Scene::init` ties the fog's
overlay to the shared section, which the player has not; the restart reads no table); every DRAIN goes to
`plotsChanged` and then `fowFeed`; `fog_clock.bin` seeds `FowScene::setClockState` before the first frame; each frame
runs `Scene::fowAdmit(cam, nowMs)` before its draw (a frame the fog holds is exit 8: the live server drew it). A
`render_config` without `render_fow` draws no fog.

**Tests [C, run on the branch; numbers in the issue log].** `bundle_format_test.exe` (`TestFogMembers` and the full
bundle with `fog_clock.bin` and a fog frame): the round trip, caller 3 through `ParseFeed` and the checker, each
refusal above through the child (exit code, line, nothing written), a version-1 manifest refused as older.
`recorder_test.exe` (`TestFog`): a fog scene with stand-in modules behind the recording feed: `feed.bin` record 0 is the
fog's start read (caller 3, iteration 0, 16 plots), a second caller-3 record after a fog edit, `fog_clock.bin` = the
clock the fog had before the span, `render_fow` / `render_fow_level` in `render_config`, no version note naming 0168;
render_fow=0 writes neither. `sceneshot_test.exe` (`CaseFog`, WARP and the RTX 4080): a live side in the host's order
with the real `c5r::fow` modules (the EXE's FOW programs, the install's cloud textures), FOWLevel 1, an 8 x 4 table of 2
visible, 2 seen and 28 unexplored plots on an 8 x 8 grid, the camera inside the overlay's window over unexplored plots,
three fog frames before the span (T = 0.2 s at its start), one plot made visible inside the span: every dumped frame
byte-equal with the fog drawn (flag bit 3 on every frame). The replay has teeth: `--set render_fow=0` changes all
three pictures and reports the fog flag as a mismatch; the same bundle with T moved by 10.37 s differs; a bundle without
`fog_clock.bin` is refused before anything is written. Found on the way [C, L in the test]: FOWAnim's T moves the
clouds by 2 T world units times k = floor(W 0.002) / W of the texture, so on a map under 500 world units wide (4 plots:
W = 255) k is 0 and T changes no pixel; the case's map is 8 plots wide (W = 511) for that reason.

**The first fog bundle, offline equals live [L, 2026-11-15 05:29-05:35 EST, workstation, c5h-f].** Product path by
`recorder_live.py --save fog` (3904's route, section 15; owner 3911 through `C5_RECORDER_OWNER`): server.exe 2941ae42...
and proxy 2e7cba39... of `task-3911-main-and-fog` 89974efb, composite a5df3a9f..., the pin's DLL 74588135...
(x64-04b1211b-pa803776), EXE 04cfdc71...; save copy C5H-0301-FOG_0008 of C5H-0167-TINY_0008 (b2ceb663..., game turn 8;
c5h-a holds the source and was claimed by another worker, so a copy of the source save was put into c5h-f for the run
and removed by hash after it); keys `_after=900 _frames=8 _dump=all`; `render_fow` at its default 1, FOWLevel 3 from
c5h-f's `GraphicsSettingsDX11.ini`. Guard checks 0 / 0 / 0, `startup_watch_ok` true, stop rc 0, every changed file
restored by hash, `provision.py verify` rc 0; no picture taken.

| Bundle (F:/c5-64bit-work/scene-library/bundles/) | Span | Fog | Feed | Size | Checker | Offline equals live |
|---|---|---|---|---|---|---|
| `C5H-0301-FOG_0008_20261115-052907_workstation` (format 2; asset set VT present 45b3b433...) | frames 1103-1160, 8 dumped, 2554 x 1411 | installed at frame 80; fog clock at the span's start T = 34.374705 s; render_fow 1, level 3; flag bit 3 on every frame | 7 drains, 8 tables (the fog's start read was an empty table: the renderer starts before the game) | 31 members, 437,577,232 B (8 overlay files, two views each) | OK (after the stamp too) | colour and depth **byte-equal in 8 of 8 frames**, at `--tolerance 0:0` and at the standing `1:1`, no MISMATCH |

The first recording of the same kind (05:01, c5h-a, `..._20261115-050103_workstation`) was byte-equal in 8 of 8 frames
as well but was refused by the player (exit 8): the recorder dropped flag bit 3 (`afterDraw` kept bits 1-2 only), fixed
in 89974efb; that bundle is in `retired/` with its log line.

**The library after the raise (2026-11-15 05:36).** Every bundle of format version 1 (3904's Tiny and huge bundles and
four SCNB_0009 bundles of 02:37-03:09) was moved, not deleted, to `F:/c5-64bit-work/scene-library/retired/` with one line each in
`retired/RETIRED.log`; the player refuses them as `OlderVersion` (exit 26). The standing check
(`test_offline_equals_live.py`) runs on what is left in `bundles/`: the fog bundle above. Rule B: a bundle that is
needed again is recorded again with the 3911 recorder. (Corrected 2026-11-15 by task 0353; replaced "the huge one and
the SCNB scenes have not been": the Tiny, huge and four SCNB scenes were recorded again at format version 2 on
2026-11-15 07:03-07:47, section 18; `bundles/` holds those six and the fog bundle; the version-1 bundles stay in
`retired/`, kept, not deleted.)

### 2.12 The models' shadow and the polar ice: epic 0169's inputs (format version 3) [C for the code, L where named]

Written by worker 4308 (epic 0169, 2026-11-15) into tracker issue 4308's section "Bundle members" while this file was held
by 0358; moved here by task 0364 on 2026-11-15, which made the three kinds members of **format version 3** (4308 had made
them optional members of version 2; the version-2 bundles are now refused as older). The forms are 4308's, unchanged. As
built in `server/src/render/srv_scene_bundle.{h,cpp}` (forms, parsers, checker), `srv_scene_recorder.{h,cpp}` (recording,
the player's file sources) and `server/tools/sceneshot/sceneshot.cpp` (playing).

**Members** (three kinds, manifest kind names `shadow`, `shadow_texels`, `ice_mesh`; every binary file starts with the
16-byte header of 2.2-2.7: magic, version 1, header bytes 16, reserved 0; little-endian):

| Path | Kind | Form |
|---|---|---|
| `shadow.bin` | shadow | magic `C5SG`; u32 recordBytes = 344; then one `ShadowRecord` per record of `frames.bin`, same order and frame ids |
| `shadow/<first 16 hex of the file's SHA-256>.shtx` | shadow_texels | magic `C5ST`; u32 width, height, dxgiFormat (56 R16_UNORM, 10 R16G16B16A16_FLOAT at quality 1), bytesPerTexel (2 / 8); then width x height texels, rows tight, row 0 first |
| `ice/gen<generation as 10 digits>.ice` | ice_mesh | magic `C5IC`; u32 generation, loadSeq, vbBytes, ibBytes, chunkCount, fieldsBytes (= 0x1A0), fieldsValid (0/1); the fields (Ice+0xFE80..+0x1001F); per chunk 28 bytes (box min x, min y, max x, max y as f32; triangles, startIndex as u32; baseVertex as i32); the VB bytes; the IB bytes |

`ShadowRecord` (344 bytes, pack 8): i64 frameId; u32 flags (bit 0 `kShadowBound` a map bound, bit 1 `kShadowQ0` quality 0
bound: no texture, strength 0, bit 2 `kIceDrawn`); u32 generation; u64 presentCount; u32 quality, dxgiFormat, width, height,
numViews; f32 strength; f32 matrices[4][16] (ShadowMatrix0 per EXE view, row-major as the EXE stores them); u8
texelSha256[32] (the `.shtx` file's SHA-256 when bit 0, else zero); u32 iceGeneration, iceLoadSeq (bit 2).

**Recording** (`SceneRecorder::afterDrawSeamG`, called by the host's frame hook right after `afterDraw` for the same frame):
the triple the scene bound in that draw (`Scene::lastShadowFrame`) and, when the ice was drawn, its mesh (`Scene::iceMesh`,
`iceGeneration`, `iceLoadSeq`). The map is copied on the server's device into a STAGING texture in the same iteration (the
reader names that slot in inUseSlot until its next acquire, so the copy is ordered before any reuse), mapped in `afterEnd`
(after the sink's end, as the dumps), hashed and written once per content: deduplicated by content (no zlib: the overlay's
`.ovl` files are not compressed either), equal maps share one file (8,388,640 bytes at quality 3; live at the Tiny load
camera every frame's map differed, the units' idle animations move their shadows, so 8 frames gave 8 files [L, 4308]). The
ice mesh is written once per generation (Tiny: about 1.07 MB). `shadow.bin` is written at the finish, one record per
recorded frame (a frame the hook did not reach gets an empty record); a bound record whose texels never arrived makes the
bundle incomplete. `render_config` carries `render_shadow` and `render_ice`. Cost: a test run's (section 13), never a
product figure.

**Not the tap's image form** (4308's deviation from its brief): the ice member is the `c5r::ice::Mesh` the scene applied,
not `IceTapReader::loadFile`'s image bytes, because the reader's contract (`srv_ice_tap.h`) hands over the Mesh and not the
image; the member carries everything loadFile yields. If the reader later exposes the image, the member can switch to it in
a form version step.

**Checks** (`ParseShadow`, `ParseShadowTexels`, `ParseIceMesh`, `CheckBundle`; refusal names): recordBytes != 344
(WrongSize), a partial record (Truncated), flags above bit 2, bit 0 and bit 1 together, frame ids not increasing, quality
above 3, numViews above 4 or 0 with a map, strength not finite or outside [0, 1], a bound matrix not finite, a bound format
other than 56 / 10 or a size of 0, a bound record without a hash, quality 0 with a texture or a strength, bit 2 with
generation 0 (BadRecord); `.shtx`: a format / bytesPerTexel pair other than 56/2 or 10/8, a size of 0 or above 16384
(BadRecord), texels short (Truncated) or long (WrongSize), the file name not the first 16 hex of its SHA-256 (ShaMismatch);
`.ice`: fieldsBytes != 0x1A0 or fieldsValid above 1, 0 or more than 4096 chunks, a VB not whole 8-byte vertices, an IB not
whole R16 indices, a chunk box not finite or min > max, 3 x triangles from startIndex past the IB (BadRecord), sizes that do
not add up (Truncated / WrongSize), the file's generation not its name's (BadRecord). Cross-checks: `shadow.bin` holds exactly
frames.count records with frames.bin's ids (WrongSize / BadRecord); every bound record's `.shtx` is listed with that SHA-256
(MissingMember / ShaMismatch); every `kIceDrawn` record's `.ice` is listed (MissingMember); a `.shtx` or `.ice` without
`shadow.bin` (MissingMember). The module's own mesh checks (`c5r::ice::Renderer::setMesh`) run when the player applies it.
The checker keeps `shadow.bin` optional (a recording whose frame hook did not call `afterDrawSeamG`: the recorder's own
tests).

**Playing** (`LoadSeamGMembers`, `MakeFileShadowSource`, `MakeFileIceSource`; sceneshot): the render_config keys
`render_shadow` / `render_ice` ("1" = on; absent = off); each distinct map as an IMMUTABLE texture of its recorded format with
a default view; before each frame k's poll the sources seek to record k: the shadow source hands frame k's triple (state
Active) or nothing (Waiting), the ice source frame k's mesh when its generation differs from the one in the module. Each
replayed frame is checked against its record (MISMATCH lines, the run fails): a triple bound, a map bound, the ice drawn.
Format version 3 (0364): a bundle without `shadow.bin` whose `render_config` has `render_shadow=1` or `render_ice=1` is
refused before anything is drawn (exit 8; 4308's "a bundle recorded before 4308" cannot occur any more); with both off the
player draws neither and says so.

**Tests:** `server/tests/render/shadow/test_scene_shadow.exe bundle_round_trip` (4308: a q3 R16 map, a q1 float map, a q0
frame and an empty one, the ice drawn in two frames: shadow.bin, two `.shtx` and one `.ice` encoded, parsed back
byte-equal; 19 damaged copies each refused with its named refusal; the file sources refuse a missing map and a missing
mesh; played into a scene, every frame binds the recorded triple, the map's texels read back equal the recorded ones, the
recorded mesh is drawn). The recorder -> bundle -> sceneshot path end to end on a live recording [L, 4308, 2026-11-15 16:17]:
8 frames, 8 `.shtx`, one `.ice`, complete; sceneshot: no MISMATCH, depth equal, colour equal but in the decal tiles (that
bundle had no decal input; 2.10 closes it, section 19).

## 3. Every renderer input on `main` (b81d4dae) and its member

Read from the render thread's code: `srv_render_host.cpp` `Loop` and `DrawFrame`, `Scene::poll`/`decide`/`draw`,
`WaterScene`, `ResidencySource`, `OverlayTapReader` [C].

| # | Input | Read where | Member / gap |
|---|---|---|---|
| 1 | Camera block `VPShellCamera`: views, proj, camWorld, status, presentCount, frameStamp, viewport, bb size, grid constants, wrap | `Scene::decide` (`SnapCamera`, `presentCount`), the overlay's wrap period, the water's decide | `cameras.bin` (2.6) |
| 2 | Height field (C5HF through the height-field tap `VPShellTap`) | `Scene::tapPoll` -> `applyField` | `heightfield/*.c5hf` (2.1) |
| 3 | Water tap image (river mesh, coast-wave chunks, the Water object's fields, Wave_System object and waves) | `WaterScene::poll` -> `WaterTapReader::poll` | `water/*.wtap` (2.2); the copy in force per frame: `water_frames.bin` (format version 11, ticket 0622, section 42) |
| 4 | Overlay pair (2048 x 2048 x 12 texture, OverlayProj per view, generation, presentCount) | `Scene::overlayPoll` -> `OverlaySource::acquire` | `overlay/*.ovl` + `frames.bin` (2.3) |
| 5 | Feed: drained batches (events incl. `kSnapshot`, `kOverflow`, `kUiMirror`; changed plots; snapshot generation; map size) | host `Loop` (`SrvFeed::Drain`) -> `Scene::plotsChanged` | `feed.bin` DRAIN (2.5) |
| 6 | Feed: plot tables (`SrvFeed::CopyPlots`) | `ResidencySource::takeBaseline`, `onPlotsChanged` | `feed.bin` PLOTS (2.5) |
| 7 | Wall clock: `QpcToMs(Qpc())` into `WaterScene::decide` (water clock, wave dt) | `Scene::draw` (srv_render_scene.cpp line 644) | `frames.bin` nowMs + `water_clock.bin` (2.7, 6) |
| 8 | Wall clock and budgets in the residency (token bucket `rate`, `burst`, `budgetMs`, `signalMs`) | `ResidencySource` -> `c5r::bake::Residency` | not recorded: replaced by the settle call (6) |
| 9 | The sink's target size per frame (`render_size`, the composite's back buffer) | `DrawFrame` -> `Scene::draw(w, h)` | `frames.bin` (2.7) |
| 10 | The install: TerrainDef.csv and layer textures (bake), water art, `Wave_Coastline_ALL.dds` | `ResidencySource::init`, `WaterScene::init` (c5r::gamefs, read-only) | not a member (D83): `asset_set` + `install` in the manifest; the player reads the player's own install |
| 11 | The EXE's shader section (TerrainShader, bake, water, wave programs) | `Scene::init`, `ResidencySource::init`, `WaterScene::init` (`render_exe`) | not a member: `pins.exe_sha256` |
| 12 | Options: server.cfg `render_*` keys; VP64Options.ini `[Renderer] BakedTileCameraRadius` (`SrvOptions()`, `srv_render_options.cpp`) | `ParseHostConfig`, `RenderOptionBakedTileCameraRadius` | `render_config` (the resolved values) |
| 13 | The adapter (LUID, the composite's `clientLuid`) | `CreateDevice` | `adapter` (the comparison stays on the same adapter, plan 4.3) |
| 14 | Bake-cache tap (EXE caches) | `ExeCacheTap::poll`, only with `render_tiles=tap` | **gap by choice** (2.4) |
| 15 | Epic 0179: `kUiMirror` records (issue 0178); since ticket 0199 part 2 (2026-11-17) kinds 1 (SpawnArrowEvent with the end points) and 2 read by the arc | pushed into the feed; `Scene::arcFeed` reads kinds 1 and 2 with `render_arc=1` (default 0); the path models stay later work (0300) | recorded inside `feed.bin` DRAIN (no new member); `render_config` `render_arc` (section 28; the arc's gap note while its default is 0) |
| 16 | Epic 0168 (on main at 172a715a): the fog's table reads (`PlotRecord.fow` through `FowPlotTable::copy`) | `FowScene::readTable` (`Scene::init`, `onBatch`) | `feed.bin` PLOTS caller 3 (2.5, 2.11; format version 2, task 3911) |
| 16a | Epic 0168: FOWAnim's clock T (the frames the fog drew) | `FowScene::beforeWorld` (`Scene::draw`, now with `SceneFrame::nowMs`) | `fog_clock.bin` + `frames.bin` nowMs (2.11) |
| 16b | Epic 0168: `render_fow`, FOWLevel (`render_fow_level` or the player's `GraphicsSettingsDX11.ini`) | `ParseHostConfig`, `FowScene::init` | `render_config` (`render_fow_level` as the fog resolved it) |
| 16c | Epic 0168: the mask, the cloud planes | `beforeWorld`, `afterWorld` | derived [C] from rows 1, 2, 4, 5, 16, 16a, 16b (2.11), tested by the replay |
| 16d | Epic 0168: the cloud textures, the TerrainFOW LUT; the FOW programs | `CloudRenderer::init`, the terrain module; `MaskRenderer`/`CloudRenderer::init` | not members, as rows 10 and 11 |

| 17 | Epic 0166: the decal tap's set (records, names, vertices, area and walk blocks); `render_decals` and the mods' folders | `Scene::decalPoll` -> `DecalTapReader::poll` -> `ResidencySource::setDecals` / `decalChanges`; `ModFolders` | `decals/load<NNNN>.dtap` (the set in force at the span's first frame) + `render_config` `render_decals`, `render_decal_mods` (2.10; format version 3, task 0364). A generation applied inside the span: a version note, not a member |
| 18 | Epic 0169: the shadow tap's triple (ShadowBlur map, ShadowMatrix0 per view, strength, quality) | `Scene::shadowPoll` -> the terrain's t3 / Arg6 and the water's t13 | `shadow.bin` + `shadow/<sha16>.shtx` + `render_config` `render_shadow` (2.12; format version 3) |
| 19 | Epic 0169: the ice tap's mesh | `Scene::icePoll` -> `c5r::ice::Renderer::setMesh`, drawn after the ocean | `ice/gen<N>.ice` + `shadow.bin` bit 2 + `render_config` `render_ice` (2.12; format version 3) |
| 20 | Epic 0561: the compositor's model tap frame (cities and landmarks) | `ModelsScene::poll` / `beginFrame` -> `ModelLayer::accept` | `modeltap/frame<NNNNNN>.mtap` + `render_config` `render_cities`, `render_landmarks`, `render_models_source` (format version 9, ticket 4875; section 31) |
| 21 | Epic 0561: the CPU-built meshes' buffers (ticket 4879's geometry section: walls, the Great Wall, bridges) | `ModelGeomReader::poll` -> `ModelLayer::addGeometry` | `modelgeom/frame<NNNNNN>.mgeo` (format version 9, optional; ticket 4888, section 32) |

(Row 16 replaced 2026-11-15 by 3911; it read "Epic 0168 ... not on main at b81d4dae ... known future gap". Rows 17-19
added 2026-11-15 by 0364. Rows 20-21 added 2026-11-19 by 4888.)

Not inputs: the reflection target t4 (an empty texture the water scene creates), the distance-fog globals and shadow
blend (the module's stock defaults; fog.w from the camera height), the terrain renderer's constants (from the field and
the camera) [C].

## 4. The readers' source interface (live section or file)

The principle: the live path keeps its code and order exactly. The file path enters each reader **after** the handshake,
at the point where the live path holds the bytes it is about to trust, and runs the same checks from there. Proposed
signatures [I]:

### 4.1 Water tap (`srv_water_tap.{h,cpp}`)

```cpp
// The bytes the live poll() accepted, before they are unmapped (null: no recorder; the default).
using WaterImageSink = void (*)(void* ctx, const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes,
                                bool haveCam, uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY);
void WaterTapReader::setImageSink(WaterImageSink f, void* ctx);
// The file source: the same CheckImage and map-size check, no ack, no request. True = records() and image() replaced,
// accepted++; false = refused++, lastResult() / lastWhy() as in poll(). Never throws.
bool WaterTapReader::loadImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes,
                               bool haveCam, uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY);
```

Kept bit for bit: `poll()`'s order (snap, request, the READY check, the ack written after the checks) and `CheckImage`.
The sink is called after `CheckImage` returns `WTACK_OK` and before `UnmapViewOfFile`. With a null sink, the one change
on the live path is one pointer test per accepted copy.

### 4.2 Height-field tap (inside `Scene::tapPoll`, `srv_render_scene.cpp`, shared)

```cpp
void Scene::setFieldSink(void (*f)(void* ctx, uint32_t loadSeq, const uint8_t* c5hf, size_t bytes), void* ctx);
// The file source: c5h::readTap + the map-size checks + applyField, as tapPoll after a good copy; no ack.
c5r::Status Scene::loadField(const uint8_t* c5hf, size_t bytes, uint32_t loadSeq);
```

### 4.3 Overlay tap: no change in `srv_overlay_tap.*`

The scene already takes any `OverlaySource` (`srv_render_scene.h`; `setOverlaySource`, used by the tests). There are two
new implementations in the new file `srv_scene_sources.{h,cpp}`. The first is `RecordingOverlaySource(std::unique_ptr<OverlaySource>
live, SceneRecorder*)`: it passes every call through and hands each new generation's `srv` and pair to the recorder
(2.3). The second is `FileOverlaySource(const SceneBundle&)`, which yields frame k's pair with an IMMUTABLE texture made
from the recorded mips. The host installs the recording wrapper after `Scene::init` when the key is set. Live is
`OverlayTapReader`, untouched.

### 4.4 Bake-cache tap: no interface (2.4)

### 4.5 The feed (new, in `srv_scene_sources.{h,cpp}`; `srv_feed.*` untouched)

```cpp
struct FeedSource
{
    virtual ~FeedSource() {}
    virtual bool drain(SrvFeed::Batch& out) = 0;                                    // as SrvFeed::Drain
    virtual void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h,
                           uint32_t caller) = 0;                                   // as SrvFeed::CopyPlots
};
FeedSource& LiveFeed();                                       // calls SrvFeed::Drain / CopyPlots, nothing else
std::unique_ptr<FeedSource> MakeRecordingFeed(FeedSource& live, SceneRecorder& rec);
std::unique_ptr<FeedSource> MakeFileFeed(const SceneBundle& b);  // refuses a feed.bin whose PLOTS records are missing
```

The host's `Loop` calls `h.feed->drain(batch)` where it calls `SrvFeed::Drain` today. `ResidencySourceConfig` gets
`FeedSource* feed` (default `&LiveFeed()`), used by `takeBaseline` and `onPlotsChanged`. With the live source the calls
and their order are today's; the only change is one virtual call per loop iteration.

### 4.6 The clock and the settle call: section 6

### 4.7 As built (task 3903, 2026-11-15, branch `task-3903-scene-sources`) [C, tested]

Where the build differs from 4.1-4.5 (the API lines are in `tracker/issues/3903-scene-sources-clock-settle.md`):
- **Water tap (4.1):** `setImageSink` and `loadImage` as proposed. The map-size check of `poll()` moved before the
  `UnmapViewOfFile` (it reads only the image header and the camera), so the sink sees only copies that passed every
  check; the block checks (stale, limits) and the map check are shared functions of `poll()` and `loadImage()`, texts
  and codes unchanged. `WaterScene::loadImage(...)` = the reader's `loadImage` + `useImage()`, with a section-less reader
  when the scene has none (the player); `WaterScene::reader()` has a non-const form for the recorder's sink.
- **Height field (4.2):** `Scene::loadField(c5hf, bytes, loadSeq, const VPShellCamera* cam = nullptr)`: `readTap`, the
  camera's grid checks of `tapPoll` when `cam` is given (stale and samples, as `BadLayout`), then the field kept and
  `applyField` as after a good tap copy. `Scene::setFieldSink` is left to 3904.
- **Overlay (4.3):** `MakeRecordingOverlaySource(live, OverlayRecordSink&)`: `onOverlayPair` at the acquire that got a
  new pair, `onOverlayTexels` (tight 12 mips, `OverlayTexels`) when its STAGING copy is mapped two `acquire()` calls
  later (ring of three; an entry still busy when its turn comes, or at the wrapper's end, is mapped then). The file
  source takes decoded data: `MakeFileOverlaySource(dev, vector<FileOverlayFrame>, map<sha256 hex, OverlayTexels>,
  FileOverlayControl**, why)`, IMMUTABLE textures with the live SRV description; refuses a texture not 2048 x 2048 x 12
  mips of the R8G8B8A8 family or of the wrong byte size, a missing texture, numViews outside 1..4.
  `Scene::poll()` without a section (the player) now runs `overlayPoll()` and the residency's redraw flag only;
  server.exe always attaches, so its path is unchanged.
- **Feed (4.5):** the recording wrapper takes a `FeedRecordSink&` (`onDrain` for drains that returned true, `onPlots`),
  not `SceneRecorder&`. `MakeFileFeed(vector<FeedCall>, FileFeedControl**, why)` takes the decoded calls (3902's
  `ParseFeed` records); `drain()` yields the next record when it is a DRAIN within the loop iteration set by
  `setLoopIter`, `copyPlots(caller)` the next record when it is a PLOTS of that caller, else the feed fails (out of
  order, LOUD, every later call yields nothing). `ResidencySourceConfig::feed` is a `FeedSource*`, null = `LiveFeed()`.
  The host's `Loop` still calls `SrvFeed::Drain` (its switch to the feed source is 3904's).
- **The bundle forms** (over 3902's `ReadMember` and parsers): `MakeFileFeed(bundleDir, manifest, ...)` (feed.bin),
  `MakeFileOverlaySource(dev, bundleDir, manifest, ...)` (frames.bin's pairs, frame k = record k; each bound frame's
  `overlay/<sha16>.ovl`, its full SHA-256 checked against the member's), `LoadWaterImageMember`, `LoadFieldMember`,
  `LoadWaterClockMember`; a refusal names the member (`CheckResult::line()`). Tested on a bundle written with 3902's
  `Writer` and encoders (case `bundle_forms`), with a changed SHA-256, a missing member, a missing overlay and a size
  mismatch as negatives.
- **Bit for bit [L, 2026-11-15 00:16]:** `render_host_test.exe` of the base 38a3fcf4 and of the task branch, keys absent,
  `render_test_dump` compared byte for byte (colour and depth, two runs each): equal in three scenarios (EXE-cache tap;
  bake stand-in with `--feed-plots 8`; the water module with `render_water_freeze=1`).

## 5. How the recorder gets each input (P7)

**P7 [C]: the handshakes rule out a passive second reader for the water, height-field and overlay taps.** The recorder
therefore sits inside the readers at their hand-off points.

| Tap | Handshake (code) | Why a second reader fails | Hand-off point for the recorder |
|---|---|---|---|
| Water (`srv_water_tap.cpp`) | One ack slot: `ackServerPid`, `ackResult`, `ackCopySeq`, then `ackLoadSeq` written last; "the DLL may close its section now" after the ack; a reader without the load asks for a copy (`requestSeq++`) | A second reader that acks overwrites the server's ack: the producer has one slot and cannot tell readers apart. One that does not ack races the server's ack, after which the section may be closed, and when it misses the copy, the protocol's step 4 makes it request one and disturbs the producer | `WaterTapReader::poll`: after `CheckImage` OK, before `UnmapViewOfFile` (4.1). After the unmap only parsed records remain, not the image the file source must check again |
| Height field (`Scene::tapPoll`) | The same protocol (shell_shared.h VPShellTap, steps 1-4; `writeTapAck`, `ackCopySeq` required, issue 3206) | As for water | After `c5h::readTap` OK, before `UnmapViewOfFile`: the raw C5HF payload (4.2) |
| Overlay (`srv_overlay_tap.cpp`) | Dekker-style slot hand-over: the reader writes `inUseSlot = s`, then checks slot s's seqlock and generation; the producer keeps off the slot named in `inUseSlot`; one `ackGeneration` and `ackServerPid` | A single `inUseSlot` word is the only thing keeping the producer off a slot. A second reader that writes it releases the server's slot, and the producer may then rewrite the texture the server samples. One that does not write it has no protection: the texture can be rewritten during its copy, and there is no fence across devices | The new pair that `acquire` returns (generation changed), through `RecordingOverlaySource` (4.3) |
| Bake-cache tap (`srv_exe_cache_tap.cpp`) | One `ackManifestSeq` / `ackServerPid`; manifest and textures opened read-only by name | A passive reader would be possible (it needs no write) | None: not on the product's path (2.4) |
| Feed (`srv_feed.*`) | Inside the server: `Drain` moves the queue out (a second drainer would steal events), `CopyPlots` reads the live table | A second `Drain` caller would split the batches | Host `Loop` after `Drain`; `ResidencySource` after each `CopyPlots` (4.5) |
| Camera, clock, targets | Read by the scene | none | `DrawFrame`: the `SceneFrame` drawn (camera block, frameId), the `nowMs` used (6), the targets for the dumps (2.8) |

The plan's section 8 lists "the tap handshake rules out a passive second reader" as not verified. It is verified now
(this section, 2026-11-14).

## 6. The clock and the settle call

- **Where the wall clock enters the water [C].** `Scene::draw` calls `m_water.decide(f.cam, QpcToMs(Qpc()))`
  (srv_render_scene.cpp line 644). `WaterScene::decide` stores `m_lastNowMs`, and at the first frame drawn with an image
  it sets `m_clockStartMs = nowMs`. `clockAt(nowMs)` gives Water+0xDC and +0xE4 as `m_dc0 + m_dcRate * t` and
  `m_e4 + m_e4Rate * t`, with `t = (nowMs - m_clockStartMs) / 1000`. `render_water_freeze=1` sets t = 0 and stops the
  waves. **The coast waves are integrated:** each drawn frame calls `cw::updateWaves(m_waveObj, dt, m_waves)` with
  `dt = min(max(0, nowMs - m_waveLastMs), 250 ms)`. The LCG restarts from the object's seed at every call
  (`c5r/water/waves.h`), so the waves are deterministic given the sequence of dts. **A frame is therefore redrawn
  exactly from the water clock state at the start of the span plus every recorded frame's nowMs.**
- **Proposed change (3903) [I].** `SceneFrame` gets `double nowMs = -1`. `Scene::draw` uses `f.nowMs >= 0 ? f.nowMs :
  QpcToMs(Qpc())`, so the live path reads the clock at the same point as today. The recorder writes the value used.
  `WaterScene` gets `WaterClockState clockState() const` and `void setClockState(const WaterClockState&)`, where the
  state is {clockStartMs, waveLastMs, lastNowMs, waves}. The player seeds it from `water_clock.bin` and then draws the
  frames in order. The check against `render_water_freeze` stays: the clock held equals the frozen picture.
- **"No bake pending" [C].** `ResidencySource::pending()` returns `!s.stopped && (!s.aheadDone || s.pending > 0)` from
  `c5r::bake::ResidencyStats`, but it rereads `stats()` at most every 100 ms (`m_pendingAt`). The settle call must read
  `m_res.stats()` itself, not `pending()`. The residency's background work is bounded by wall-clock budgets (`rate`
  400/s token bucket, `burst` 16, `budgetMs` 4). In the player those are options, so the player gives large values.
- **The settle call (3903) [I]:**

```cpp
struct SettleResult { bool settled = false; int calls = 0; double ms = 0; uint32_t pending = 0;
                      bool aheadDone = false, stopped = false; uint32_t visibleUnbaked = 0; };
// srv_residency_source.h: beforeDraw(view) and idle(budget) in turn until !stopped && aheadDone && pending == 0 and
// every visible land tile has a current bake (tileBaked); after maxMs: ok() with settled = false (c5r::Err has no timeout code; the caller refuses the shot).
c5r::Status ResidencySource::settle(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view,
                                    double maxMs, SettleResult* out);
// srv_render_scene.h: forwards to the residency source; no source (render_tiles=none) or the tap: settled at once.
c5r::Status Scene::settle(ID3D11DeviceContext* ctx, const SceneFrame& f, double maxMs, SettleResult* out);
```

  A residency that `stopped` (out of memory at radius 0) is not settled: the player refuses to shoot and says so. On the
  huge map the bake-ahead covers every land tile at radius -1 (D88). At the live `rate` of 400 per second that is at
  least about 10 s for some 4,000 land tiles [I, arithmetic; the count is not measured here]. That is why the player
  raises the rate.
- **As built (task 3903, 2026-11-15) [C, tested on WARP and the stand-ins]:**
  - `SceneFrame::nowMs` (default -1) is used at the `WaterScene::decide` call of `Scene::draw` as proposed; the live
    path reads `QpcToMs(Qpc())` there as before.
  - `WaterClockState` is declared in `srv_water_scene.h` (not `srv_scene_sources.h`: the water tests build without
    `server/src`). `WaterScene::clockState()` returns {`m_clockStartMs`, `m_waveLastMs`, `m_lastNowMs`, `m_waves`};
    `setClockState` refuses (false, logged, nothing changed) a time that is not finite or a wave array whose size is not
    the image's. It must follow the image's use: `useImage()` resets the clock start and the waves.
  - Tested: the clock held (the same nowMs every frame) gives `render_water_freeze`'s frames, and the clock state at
    frame 4-5 followed by the recorded nowMs (dts of 16-650 ms, one above the 250 ms clamp) gives the live frames: on a
    fake module (the clock fields, clock seconds and the wave array, compared bit for bit) and with the real
    `c5r::water` module on WARP inside the `Scene` (pixels, 160 x 120, with visible synthetic coast waves; a scene
    without the state differs, so the comparison has teeth). `sources_test.cpp` cases `clock_*`, `scene_clock_*`.
  - `ResidencySource::settle(ctx, r, view, maxMs, out)` and `Scene::settle(ctx, f, w, h, maxMs, out)` (the target size
    added: the view needs it). The loop: `beforeDraw(view)` then `idle(budgetMs)`, `m_res.stats()` read each round.
    Settled = not stopped, bake-ahead done, `pending == 0`, and the round's `beforeDraw` saw no visible land tile
    without a current bake. That count is `visibleUnbaked` = the round's growth of `grayTileFrames` (visible land tiles
    without any bake) + the growth of `staleFrames` (1 if a visible tile's bake is stale): a lower bound, because the
    residency counts stale tiles per frame only. A visible tile whose bake failed is never settled (not pending, but
    gray). A stopped residency returns at once with `settled = false`. `SettleResult::what` names what is pending. When a
    round baked nothing, the loop sleeps 1 ms so that the token bucket refills. Tested with the stand-in baker: 16 land
    tiles at 60 bakes/s settle after more than 100 ms and the next draw bakes nothing and shows no gray tile; at
    2 bakes/s a 300 ms limit gives `settled = false` with the pending count; `memoryLimitMb` 0.001 stops the residency.

## 7. P2: a recorded feed replayed in order gives the same renderer state

**Answer [C]: yes, if the bundle holds the `CopyPlots` tables (`feed.bin` PLOTS) beside the drained batches; no, with
batches only.**

- **What the renderer keeps from the feed:** only `ResidencySource`'s baseline `m_rec` with its generation `m_recGen`,
  and the plots it passes to `c5r::bake::Residency::onPlotsChanged` (`m_pass`, filtered by `BakeRelevantChange`). The
  host only counts events. `Scene::plotsChanged` forwards the changed plots, or counts them as lost while no source
  exists. No render code reads `kUiMirror`, `kOverflow` or the events' arguments.
- **Why batches alone fall short:** `onPlotsChanged` calls `SrvFeed::CopyPlots` in the same loop iteration as the
  `Drain` and compares the live table with the baseline. DLL threads write the table between the two calls
  (`SetPlot`/`UpdatePlot` under the SRW lock), so the table read can already hold changes whose plots arrive in the next
  batch. A `Batch` carries plot indices and events, not record values. The getter fields (terrain, plot type, feature,
  owner, flags, fog) are refreshed at `LeaveUpdate` and cannot be rebuilt from events alone (`srv_feed.h`, issue 3303).
  So the filter's decision, and with it which tiles re-bake, depends on what `CopyPlots` returned.
- **Snapshot generations:** `onPlotsChanged` with `gen != m_recGen` (a load, a new table) takes the table as the new
  baseline and counts every plot as filtered. `takeBaseline` at `onField` takes it too. Both are reproduced when the
  PLOTS records are replayed with their generation.
- **Overflow events:** `PushLocked` drops events beyond the cap (65,536 by default, `SetQueueCap`) and `Drain` adds one
  `kOverflow` event with the count. Changed plots are marked separately (`MarkChangedLocked`, `g_changed`), so an
  overflow loses events but not changed plots. The render side does nothing special for `kOverflow`. A replay therefore
  gives the same state through an overflow as long as the DRAIN records keep the events as drained.
- **What 3904 must test:** keys present, the recorder writes, for each `onPlotsChanged`, the passed plot list as a check
  record (a DRAIN-adjacent type 3 CHECK: `loopIter`, `nPass`, `u32[]`, plus `plotsChanged/Passed/Filtered`). Then a
  replay through `MakeFileFeed` into a `ResidencySource` with the stand-in baker gives the same lists and counters. It
  does so on a span with a load (snapshot), plot edits in the span (a road or an improvement through the Lua channel on a
  copy of the save), and a forced overflow (`SetQueueCap` small in a test build or a test key).
  Negatives: a `feed.bin` with a PLOTS record removed is refused, not replayed; a truncated record; an unknown type.
- **What 3905 must test:** "offline equals live" on a bundle whose span holds plot edits. After `settle`, the player's
  frame after the edit equals the live dump of that frame (the re-baked tile included).

## 8. P5: unit draws in a capture, and their bone palette without a replay [L, 2026-11-14 23:37-23:42 EST]

- **Capture:** 3701 stock A, `D:/c5-64bit-work/0167/captures/c5h-a_20261114-131137_frame1762.rdc` (103,900,071 B,
  SHA-256 db058c39...4aabf3), copied to `C:/c5-64bit-work/0301/3901/` with an equal SHA-256. Save Tiny
  `C5H-0167-TINY_0008` (turn 9, 56 x 36), 32-bit `i0ship-26234371-ia`, stock config, RenderDoc 1.47, c5h-a, VT present
  (`renderdoc-capture.md` 9). Converted through `quietlock.py run` with `cli.py rdc-export ... --select draws:0` (convert
  5.1 s) and `--select draws:368`; 618 draws in the frame.
- **Scan:** a scratch script (not kept: it was 0154's `terrain_draws_0154.py` loop with the input layout, the vertex
  buffers and VS t27 added). It rebuilds every buffer in execution order (frame-start contents plus each `Unmap` and
  `UpdateSubresource`) and selects the draws whose input layout has `BLENDWEIGHT`. It found 20 draws in 4 VS/PS pairs.
  Their shaders were named by SHA-256 against the EXE's SM 5.0 blobs at the VAs of `evidence/2900/2903/material_table.txt`
  (read at run time from `%USERPROFILE%/c5-instances/c5h-c/install/CivilizationV_DX11.exe`; 429 blobs hashed):

| Draws (frame order) | VS / PS | Material, pass | Target | Index count x instances |
|---|---|---|---|---|
| 12-15 | 0x6804E48 / 0x6815328 | 90 UnitShader_Skinned_CODE, pass 1 | 2554 x 1411 R16G16B16A16_FLOAT (section 5's pass A) | 3708 x 6, 1962 x 3, 2022 x 6, 1914 x 3 |
| 57-60 | 0x6803140 / 0x6813630 | 90, pass 0 | 1024 x 1024 R8G8B8A8 (format 27) | the same four |
| 368-371 | 0x6803140 / 0x6813630 | 90, pass 0 | the back buffer (between the 3D lists and the UI) [I for the target: the scan leaves the back buffer's view unresolved] | the same four |
| 29-30, 61-62, 325-328 | 0x61A2BB8 / 0x61A76F8 (p1), 0x61A0CB0 / 0x61A56E8 (p0) | 6 BuildingShader_Skinned_CODE | the same three passes | 510, 510, 16119, 2313 x 1 [I: Honolulu's or a resource's animated parts] |

  So a unit's draws are told apart by their shader, which is UnitShader_Skinned_CODE (material 90, not 84). 84's PS
  slots, t0-t5 = BaseTexture, SREF, irradiance cube, dull and full environment cubes, distance-fog ramp
  (`slot_names.txt`), match what these draws bind: t0/t1 256 x 256 or 128 x 128 BC (format 76) with 9 or 8 mips, t2-t4
  32 x 32, t5 128 x 8.
- **The vertex input [C, the capture's VS bytecode, disassembled with the system's `d3dcompiler_47.dll` on C: and not
  kept, D83].** Slot 0 has a stride of 28 B: POSITION R16G16B16A16_FLOAT, BLENDWEIGHT R8G8B8A8_UNORM, BLENDINDICES
  R8G8B8A8_UINT, NORMAL R16G16B16A16_FLOAT, TEXCOORD R16G16_FLOAT. Slot 1, per instance, with a stride of 4: BLENDINDICES1
  R32_UINT, the instance id. The EXE's vertex format is thus not the `.gr2`'s 68-byte one (`landmarks-and-models.md`
  4.7). The VS reads its bone rows from `t27` at element `instanceId * 98 + 2 + 3 * boneIndex` (+0, +1, +2) and blends up
  to three bones, the weights in x, y, z. The rows are the three rows of a 3x4 matrix with the translation in w. Then
  `cb1[4..7]` is the ViewProj, and `cb1[3]`, the camera position, goes into the world-to-eye vector. **There is no world
  matrix: the bone matrices are in world space.**
- **The bone palette [L].** VS t27 is a buffer SRV (R32G32B32A32_FLOAT, 16,384 elements) over a 262,144-byte
  `D3D11_USAGE_DYNAMIC` buffer (resource 858). It is written once per frame by `Map(WRITE_DISCARD)`/`Unmap`, and the
  instance-id vertex buffer (857, 8,192 B) is written the same way. Both are therefore in the conversion as written,
  with no replay. The exporter's `buffers/858_c0a44b58.bin` (from `--select draws:368`) has the same SHA-256 as the
  scan's rebuild at the draw (c0a44b58...2932f). Each instance's block is 98 float4: 2 header rows, then 32 bones x 3
  rows (32 = the materials' `SkinBoneCount`, `landmarks-and-models.md` 4.7). 18 instances are filled (3,324 elements
  used). Header row 0 = (0.851, 0.345, 0, 1) on all 18 [I: team colour]. Header row 1 = (1, 0, 0, w) with w varying
  [meaning not known].
  **Corrected 2026-11-15 (3909, `research/05-graphics-composition/unit-draws.md` 4-6, 26 catalogue exports) [C]:** an
  instance is one mesh of one figure, not one figure. Header row 0 is the team colour: the owner's
  `PlayerColors.SecondaryColor`, for six owners within 0.004. Header row 1's x, y, z are the PS's alpha triple
  (`x + z (y - x)`, (1, 0, 0) everywhere); w is read by no program. Of the three targets, the 2554 x 1411
  R16G16B16A16_FLOAT one is a shadow mask that the ShadowShader turns into the terrain's shadow map, and the 1024 x 1024
  one is the water's reflection (t4). (Replaced: "[I: team colour]", "[meaning not known]", "section 5's pass A".)
- **World matrix to plot [L for the numbers, I for the formula].** Instances 0-11, with 20 bones filled each (warrior.gr2
  binds 20) and meshes of 1962, 2022 (= warrior.gr2's 674 triangles x 3) and 1914 indices, have root bones around
  world (1471, -276). Instances 12-17 (24 bones, mesh of 3708 indices) are around (1534, -168). With the height-field
  relation (`heightfield.h`: x = c sx - ox, ox = 32 W; y = r sy - oy, oy = 32 (H + 4) sy, sy = 0.866), plot centres on
  this map (W = 56, H = 36) are x = 64 px + 32 (py odd) + 32 - 1792 and y = 55.43 (py + 2) - 1108.5. That gives
  (50,13) -> (1472, -277) and (51,15) -> (1536, -166), within 3 world units of both group means. The 12 figures stand on
  (50,13), where unit 1002 is selected, and the 6 on (51,15). The figures' spread around a centre (about ±20 units) is
  the formation. **Correction note:** `renderdoc-capture.md` 9 calls (51,15) "the warriors". The meshes say that the
  warrior.gr2-sized mesh is drawn on (50,13). Which unit types stand on which plot is for 3907's state record, which
  writes them down [not resolved here].
  **Corrected 2026-11-15 04:56 (3909; epic 0327 merged on `main` 7f25199a, queue #1497, `unit-playback.md` 5.4, task
  4151) [L]:** the 6 figures on (51,15) are a Shoshone Pathfinder (`ART_DEF_UNIT_U_SHOSHONE_PATHFINDER`, 6 x member
  `U_SHOSHONE_PATHFINDER`, Scale 0.14), fortified: five fit the fortify idle clip and one the fortify idle fidget. The
  12 figures on (50,13) are warriors, also fortified (`unit-playback.md` 1.3). Checked against 3909's records: in
  3907's catalogue the Pathfinder (L13 frame 624, draw 465) is the same mesh, 3,708 indices with 24 bones weighted, 6
  instances. (Replaced: "[not resolved here]". `renderdoc-capture.md` 9's "(51,15) the warriors" is wrong, as said
  above.)
- **Result:** P5 holds for this capture. A unit's draws are found by the shader hash (material 90). The figure's world
  position comes from its root bone and gives its plot. The whole palette, together with the instance ids, is read from
  the zip.xml conversion as 0154 read the terrain constants. 3907's catalogue can take its pose data from the conversion,
  and 3909 extends the scan into the unit-draw analysis. P5 is not left to 3907.

## 9. Proposed file and folder names, and the recorder's keys

| Brief's proposal | 3901 | Why |
|---|---|---|
| `server/src/render/srv_scene_bundle.{h,cpp}` | **confirmed** (3902): manifest struct, reader and writer of every member, the checker | picked up by `build_server.py`'s and `build_render_tests.py`'s globs of `server/src/render/*.cpp` |
| `server/src/render/srv_scene_recorder.{h,cpp}` | **confirmed** (3904) | as above |
| (new) `server/src/render/srv_scene_sources.{h,cpp}` | **added** (3903): `FeedSource` (live, recording, file), `RecordingOverlaySource`, `FileOverlaySource`, `WaterClockState` helpers | keeps `srv_feed.*` and `srv_overlay_tap.*` unchanged (4.3, 4.5) |
| `server/tests/render/scenebundle/` | **confirmed** (3902, 3903, 3904 tests) | |
| `server/tools/sceneshot/` | **confirmed** (3905) | |
| `server/tools/scenerun/` | **confirmed** (3906) | |

Recorder keys (`ParseHostConfig`, beside `render_test_dump`; one row in the `server/README.md` keys table):

| Key | Default | Meaning; refused at parse when |
|---|---|---|
| `render_test_record` | off | `DIR`: write a bundle there. Refused: not an absolute path, more than 200 characters. An unwritable or non-empty folder at the start is LOUD, the recorder stays off and the game goes on |
| `render_test_record_after` | 1 | start at the N-th published frame (as `render_test_dump_after`); 1..1000000 |
| `render_test_record_frames` | 8 | frames in the span; 1..100000. At 22.4 MB of overlay per frame with a unit selected, 8 frames are about 180 MB |
| `render_test_record_dump` | `first,last` | which frames of the span get the server's own dump (2.8): `first,last`, `all`, or `every:N`; at least two frames, else refused |

The height field and the water image arrive once per load, before the span. The recorder therefore writes every
accepted height-field and water copy from the server's start: the key is read at the start, as all `render_*` keys are.
The frame span (cameras, clock, overlay, dumps) starts at `render_test_record_after`, and the feed is recorded from the
first drain, so the PLOTS baseline of `onField` is in the bundle. A recorder that started after the load would have to
rebuild the members from the readers' memory. That does not work for the water: `records()` holds no raw image. For the
height field it is exact only if `writeTap` reproduces the payload. The route is not proposed.

## 10. The test plan with its negatives (3902-3906)

A test whose bundle is missing **fails** (exit code asserted), never skips (done item 5). Every negative asserts its exit
code and its output, and that nothing was written. The suites run plain and once under MSVC `/fsanitize=address` with 0
reports.

| Task | Positive tests | Negatives |
|---|---|---|
| 3902 bundle library | round trip of every member kind (2.1-2.9) on synthetic data made by the test (nothing from the game, D83); the manifest written and checked; C5HF through `readTap` | an older `format_version` refused (not skipped); an unknown `format`; a SHA-256 mismatch; a missing member; an unlisted file; a truncated file per member kind; a wrong size (water block size, camera block size, overlay mip sizes); an empty feed; a PLOTS diff before any full table; a `.ovl` with 11 mips |
| 3903 sources, clock, settle | the live source is bit for bit the picture before (`test_render_host.py`'s dump, keys absent); `loadImage` yields the same `records()` as `poll()` on the stand-in section; `FileOverlaySource` binds the same `lastOverlay()`; `FileFeed` yields the recorded batches and tables; the clock held = `render_water_freeze`'s picture; `setClockState` then N frames = the same N frames live (waves included); `settle` returns only with no bake pending (stand-in baker) | each source: a refused image (bad hash, stale grid seq, map size), a texture of the wrong size or format, a feed record out of order, a settle that times out (`settled = false`, the result names what is pending), a stopped residency |
| 3904 recorder | keys absent: no file, the picture bit for bit as before; a live Tiny run writes a bundle that passes 3902's checker; the dumped frames' numbers are in the manifest; P2's replay check (7); the overlay dedupe (two equal generations, one file) | an unwritable folder (LOUD, the game goes on); a disk-full stand-in (a test seam: the writer fails at byte N, the bundle is marked incomplete and refused by the checker); a bad key value refused at parse (relative path, `render_test_record_frames=0`, `render_test_record_dump=first`) |
| 3905 sceneshot, offline equals live (as built 2026-11-15: section 16; every negative here tested; P1 measured, standing tolerance 1:1, MC #1403, until 0336) | for at least two frames per bundle, the player's colour and depth equal the server's dump of the same frame, same adapter (the aim: equal bytes; tolerance stated after the first measurement, decision 2); PNG and depth out | an older format refused; a bad member refused; a missing bundle fails; a camera name not in the bundle; a frame number outside the span; a settle timeout ends the shot with its own exit code (no picture) |
| 3906 run and report (as built 2026-11-15: section 17; every negative here tested, the copy one live on oldpc) | all bundles x all cameras on the workstation and one other machine; regression against the player's own earlier picture; fidelity against the stock shot; every picture recorded as DIAGNOSTIC | the run fails when a bundle is missing, when `visualcheck.py list --issue <id> --result undescribed` is not empty, or when a copy's SHA-256 differs on the other machine |

## 11. Where the library lives (MC decision 3 as changed by D141, 2026-11-14 23:43: no D: copy)

- **Master copy:** `F:/c5-64bit-work/scene-library/`. F: is reliable long-term storage (D141). D: is not used by this
  epic. Finished capture batches are kept on F: as well.
- **Working copy for a run:** under the run's ticket folder on C: (`C:/c5-64bit-work/0301/<ticket>/lib/`), refreshed by
  SHA-256 from F: for the bundles that run needs and removed at the end. On `oldpc` and the laptop: `<root>/scene-library/`,
  through 3906's copy script (`tools/testbed/tb.py`), checked by hash.
- **Layout [I, proposed]:**

```
F:/c5-64bit-work/scene-library/
  index.json                      bundle id -> format version, scene.json SHA-256, save, map, turn, created, machine
  bundles/<bundle-id>/            scene.json + members (section 2); bundle id = <save>_<yyyymmdd-hhmmss>_<machine>
  stock/<scene-name>/             full-scene stock references: <camera>.png, <camera>.rdc, <camera>.export/, state.json
  catalogue/<batch-id>/           3907's catalogue: shots/, captures/, exports/, state/ (which type of which player on which plot)
  poses/<sequence-id>/            3908's pose sequences: captures, frame times (Present timestamps), bone matrices
```

## 12. The shared paths each later child needs (for the REVIEW-REQUEST)

| Path | Child | Change in one line |
|---|---|---|
| `server/src/render/srv_render_host.{h,cpp}` | 3904 (3903 for the feed call) | the four `render_test_record*` keys in `HostConfig`/`ParseHostConfig`; the recorder created at the start; `h.feed->drain` instead of `SrvFeed::Drain` in `Loop`; the recording overlay wrapper installed after `Scene::init`; the frame hook in `DrawFrame` after `Scene::draw` (per-frame record, dumps) |
| `server/src/render/srv_render_scene.{h,cpp}` | 3903, 3904 | `SceneFrame::nowMs` used at line 644 (3903); `Scene::loadField` and `Scene::settle` (3903); `setFieldSink` in `tapPoll` (3904); all additive, none on the live path's data |
| `server/src/render/srv_water_scene.{h,cpp}` | 3903 | `WaterClockState` get and set (clock and waves); the water tap's file entry through `reader()` (needs a non-const accessor) |
| `server/src/render/srv_render_options.cpp` | none | not needed (test keys only, no VP64Options.ini key) |
| `server/src/feed/srv_feed.{h,cpp}` | none | not needed (`FeedSource` wraps `Drain`/`CopyPlots` from outside, 4.5) |
| `server/tests/render/build_render_tests.py` | 3905, only if needed | not needed for 3902-3904 (the glob); `sceneshot` builds from its own script reusing the object list; one line here only if that fails (ask then) |
| `server/build_server.py` | none | not needed (the glob) |
| `tools/harness/cli.py` | none | not needed (`capture --frames`, `rdc-export`, `shot` exist; `scenerun` is its own command) |
| `server/src/render/srv_v3_sink.*` | none | not needed (the recorder reads back the targets itself, 2.8) |
| owned, free on 2026-11-14 22:39 (brief): `srv_water_tap.*` (3903: `setImageSink`, `loadImage`), `srv_residency_source.*` (3903: `FeedSource*` in the config, `settle`); `srv_overlay_tap.*`, `srv_exe_cache_tap.*` | 3903 | the last two are not needed (4.3, 4.4) |

## 13. The recorder's cost with its keys absent

[C for the hooks as proposed, I for the cost]: with no `render_test_record` key the recorder object is not created. What
stays on the live path:
- one pointer test per accepted water copy (`setImageSink` null) and per accepted height-field copy;
- one virtual call per loop iteration for the feed (`LiveFeed().drain`) and one per `CopyPlots` (a few per turn);
- a `SceneFrame::nowMs < 0` test per drawn frame, with the clock read where it is read today;
- one pointer test per drawn frame in `DrawFrame`.

No allocation, no file, no GPU copy and no change of any byte the renderer reads. That is nanoseconds against a frame of
milliseconds, and not measurable. The binary grows by the recorder's code. **Decision 1 stands:** test keys in the
product binary, as `render_test_dump`. 3904 confirms that the picture with the keys absent is bit for bit the one before
(`test_render_host.py`'s dump compare); it makes no timing claim (PERFORMANCE.md). With the keys present, the cost is
the overlay readback (22.4 MB per new generation, through staging textures mapped two iterations later) and the file
writes. That cost is a property of a test run, labelled as such and never a product figure.

## 14. Not verified

- (Settled by 3902, 2026-11-15: the file forms of sections 1 and 2 are built and tested on synthetic data. Each "as
  built" line gives the form; the sizes of the `.ovl` (22,369,748 B), `.wtap` head (356 B) and `FrameRecord` (344 B)
  are compiler and test facts. Replaced: "every file form above is a proposal [I] until 3902 builds and tests it".) No
  bundle from a live run has been checked yet (3904). (As built, 3904, 2026-11-15: two bundles of the joined arm, Tiny and
  huge, pass the checker; section 15.)
- (Settled by 3902, 2026-11-15: `sizeof(VPShellCamera)` is 712 B by `static_assert`, not about 708. Replaced: "by counting
  fields, not by the compiler".)
- That the residency's bakes are byte-identical between two runs on the same adapter and inputs (the premise under P1).
  The settle call removes the timing, not a driver's nondeterminism. 3905 measures it.
- The overlay readback's safety argument (2.3) rests on the reader's own cross-device limit. It is not tested under load.
- Back-buffer identity of draws 368-371 (the scan left the back buffer's view unresolved); which unit types stand on
  (50,13) and (51,15) (settled 2026-11-15 04:56: warriors and a Shoshone Pathfinder, section 8). (Settled 2026-11-15 by 3909: the header row 1, `unit-draws.md` 4; replaced "the meaning of the
  bone block's header row 1".) The scene target of the catalogue exports (res 2419) has no description in the export
  either.
- The land-tile count behind "at least about 10 s" of bake-ahead on the huge map at 400 per second.
- (3911, 2026-11-15, the fog, 2.11.) A span whose first frame has no overlay bound while the live fog kept an older pair:
  the player's fog has no pair and holds the frame (exit 8, named); no bundle had one. A device rebuild that reads the
  fog's table again (only while it is still empty) is not exercised. Within the span the live fog's T is taken from the
  recorder's clock read before the draw (as the water's, section 15), some microseconds before the draw's own read
  would have been: a test run's property, the keys absent change nothing (bit for bit). No fog bundle of the huge map.

## 15. The recorder as built (task 3904, 2026-11-15)

Written 2026-11-15 by the implementer of task 3904 (account B, branch `task-3904-scene-recorder`). [C] = read in the code
of that branch or asserted by its tests; [L] = a live run named below.

**Files [C].** `server/src/render/srv_scene_recorder.{h,cpp}` (`SceneRecorder`, `ParseRecorderKeys`); the host's blocks
in `srv_render_host.{h,cpp}` (each marked "epic 0301, issue 3904"); `Scene::setFieldSink` in `srv_render_scene.{h,cpp}`
(one block in the header, one `else if` after `readTap` in `tapPoll`); tests `server/tests/render/scenebundle/
recorder_test.cpp` (also the `--check` and `--stamp` tool), `test_recorder_host.py` (the host against the fake
composite), the live driver `recorder_live.py` (over `shell/evidence/3308/run_3308.py`).

**Keys [C]** (section 9 as proposed, with these as-built rules; `server/README.md` has one row each):
- `render_test_record=DIR`: absolute (a drive `X:\` or `X:/`, or a UNC path), at most 200 characters, trailing slashes
  dropped. Missing parent folders are made; the leaf must be absent or empty (3902's `Writer::open`). A folder the
  recorder cannot use is one line `LOUD: recorder off: render_test_record=DIR: <why>; the game goes on` and no recorder.
- `render_test_record_after` 1..1000000 (default 1): the span starts at the draw for which `published + 1 >= N`.
- `render_test_record_frames` 1..100000 (default 8).
- `render_test_record_dump`: `first,last` (default), `all`, `every:N` (frame k of the span when `k % N == 0`). A choice that
  dumps fewer than two frames is refused at parse (`first`; `every:N` with N >= frames; frames = 1 with `first,last`).
- Any of the last three without `render_test_record` is refused at parse ("needs render_test_record"). Every refusal is
  ParseHostConfig's: `key=value: what is wanted`, the renderer does not start (exit 2 in render_host_test, as every
  render_* key).

**What the host does with a recorder, and only then [C].** At the render thread's start (before the first device):
`SceneRecorder::Open`, the recording feed `MakeRecordingFeed(LiveFeed(), rec)` as `h.feed` (Loop's drain) and as
`SceneOptions::bake.feed` (the residency's `CopyPlots`), the water reader's `setImageSink`, the scene's `setFieldSink`.
After each `Scene::init` that succeeded (the first device and every rebuild): the scene's overlay source is replaced by
`MakeRecordingOverlaySource(MakeTapOverlaySource(header, dev), rec)` (the source `init` made has not been asked yet, and
`OverlayTapReader`'s constructor touches nothing, so the replacement is the same reader state), and the adapter's LUID,
description and UMD version go to scene.json. Per loop iteration: `beginIteration()` before the drain, `step()` after
the idle work. In `DrawFrame`: `beforeDraw` right before `Scene::draw` (a frame of the span is drawn with
`SceneFrame::nowMs` = the clock value the recorder read there and records), `afterDraw` after `OMSetRenderTargets(0)`
(the record; the chosen frames' colour and depth copied into STAGING textures), `afterEnd` right after the sink's `end`
(the copies mapped and written: the publish is not held up by the map). At the thread's end, after the device and with
it the overlay wrapper are released (so its pending readbacks are handed out): `finish`. With the keys absent: `h.feed`
is `LiveFeed()` (one virtual call per iteration instead of `SrvFeed::Drain`), `bake.feed` stays null (LiveFeed), and the
rest is one pointer test per site; nothing else runs.

**What the recorder writes [C]:**
- `heightfield/load<NNNN>.c5hf` per accepted copy (a later copy of the same load: `load<NNNN>-<k>.c5hf`), cut to
  `c5h::tapSize`; the sink runs only for a copy that also passes tapPoll's map-size checks (the same tests, made before the
  unmap). `save.map_w`/`map_h`/`load_seq` come from the last one.
- `water/load<NNNN>.wtap` per accepted copy (later copies of a load: `-c<copySeq>`), through `EncodeWaterTap`.
- `feed.bin`: every drain that returned true (DRAIN) and every `CopyPlots` table (PLOTS, 3902's FeedEncoder: full or
  diff), in call order, from the first iteration. A DRAIN's `nextFrameId` is patched in place with the frame id of the
  next `Scene::draw` (the records wait in memory until then; past 16 MiB they are written with -1 and a version note
  says so). A table whose size is not width x height is not recorded (a record error).
- The span: one `FrameRecord` and one camera block per frame drawn in it; `frames.bin`, `cameras.bin` and
  `water_clock.bin` (the water scene's `clockState()` taken in `beforeDraw` of the span's first frame) are written at the
  finish. Flags: bit 1 when `WaterScene::drawsTotal` grew in the draw, bit 2 when `waveDrawsTotal` grew, bit 0 when the
  dump was written.
- The overlay: each frame whose draw bound the pair the wrapper handed last (`Scene::lastOverlay().srv` = that pair's
  `srv`) records the pair's generation, presentCount, numViews and OverlayProj; its texels (two acquire calls later) are
  encoded (`EncodeOverlay`) and hashed; the file is `overlay/<first 16 hex>.ovl` and written once per content (equal
  texels of two generations: one file, the second counted as deduplicated). Texels arriving before the span are kept
  for the newest 4 pairs (22.4 MB each) so that the pair bound at the span's start has them.
- Dumps: `dumps/<frameId>.bmp` (EncodeDumpColour, the target's 4-byte pixels as stored) and `dumps/<frameId>.depth.f32`
  (the D32_FLOAT / R32 target's float rows), both of the frame's drawn size (the top-left w x h of the target).
  A target that is multisampled, smaller than w x h, not 4 bytes a pixel or not 32-bit float depth gives no dump and a
  record error.
- scene.json: bundle id = the folder's name; save name and machine from it when it has the form
  `<save>_<yyyymmdd-hhmmss>_<machine>` (else the whole name, and the computer's name); `created` = the local time at
  Open with its offset; `recorder.keys` = the keys as given; `server_exe_sha256` = `pins.server_sha256` = server.exe's own
  file; `pins.exe_sha256` = `render_exe`'s; the asset set by `ComputeAssetSet(render_bake_install, machine)`;
  `render_config` = 23 resolved HostConfig values (handover, size, depth, pass, tess, tile plots and d4, water and its
  freeze, overlay, colour, fallback, constant, stale frames, tiles, baker, bake radius (resolved), budget, rate, burst,
  GPU budget, bake install, exe); version notes: the bake-cache gap (with `render_tiles=bake`), the fog-inputs gap of
  epic 0168 (no member in format version 1; as built 3911, 2026-11-15: no longer written, the fog's members are in
  format version 2, 2.11, and `render_config` has `render_fow` and the resolved `render_fow_level` too), a short span,
  an early feed flush; counters: `water_refused` (the water
  reader's), `overlay_refused` / `overlay_stale` (the overlay source's, last seen while it lived), `feed_overflow_events`
  (kOverflow events in recorded drains), `record_errors`. Left empty for the stamp: the save's SHA-256 and turn, the
  i0/dll pins, the proxy and composite hashes, `repo_commit`, `build_config`.
- **The stamp** (`recorder_test.exe --stamp DIR KEY=VALUE ...`: `save.name`, `save.sha256`, `save.turn`, `repo_commit`,
  `build_config`, `pins.<name>`, `note`; an unknown key exits 2) rewrites scene.json through ParseManifest /
  ManifestToJson (scene.json is not a member, so no hash changes) and runs the checker; exit = its code.

**When the bundle is finished, and when it is incomplete [C].** `step` finishes once the span is drawn and every pair a
frame bound has its texels or a readback error, or 600 iterations after the span's end; `finish` at the thread's end
otherwise. The bundle is INCOMPLETE (scene.json `incomplete: <why>`, no member moved out of `.partial`, the checker
refuses it with exit 38 = 20 + `Incomplete`; LOUD line) when: a write failed (the writer's first reason wins; the
disk-full seam `writer().failAfterBytes(N)`), the span never started, fewer than two frames were dumped, the feed
recorded nothing (an empty `feed.bin` would be refused as `EmptyFeed`), or a bound frame's overlay has no texels. A frame
id that does not increase (a redraw of the same camera, a new composite) or a failed draw ends the span early; with two
or more dumps the bundle is complete with the note `recorder: short span: N of M frames: <why>`. So a live span is best
recorded with `render_test_record_dump=all`.

**Tests [C, run 2026-11-15].** `recorder_test.exe` (build_scenebundle_tests.py --run): 114 checks, 0 failed, 0 skipped:
the keys (4 positives, 14 refusals), a whole bundle (field, water, feed, three overlay generations of which two equal:
2 files; 4 frames; first,last; the checker in process and as a child; the dumps' pixel and depth values; DRAIN
nextFrameId; the stamp and an unknown stamp key), an unusable folder (non-empty; under a file), and the negatives, each
with the child checker's exit code 38 and line: disk full, never started, frame id going back (first,last), empty feed,
overlay readback error, D24 depth target; a short span with `all` that is complete; P2's replay (contract 7) through
`feed.bin` -> ParseFeed -> MakeFileFeed into a second scene with the stand-in baker across a snapshot, plot edits, a
forced overflow (queue cap 4) and a second load: the same changed / passed / filtered counters (13 / 6 / 7) and
residency raises; a feed without its PLOTS records refused. `test_recorder_host.py` (render_host_test.exe and the fake
composite): keys absent (no recorder line, no scene.json), a bundle from the host (v3, 4 frames, all dumped, the checker
OK, the dumps' size the sink's 320 x 240), the unusable folder (LOUD, frames published, exit 0), 4 parse refusals
(exit 2 with `result: config refused: key=value: ...`, nothing written).

**Keys absent: bit for bit [C, run 2026-11-15 01:20-01:28 EST].** `server/tests/render/scenebundle/recorder_bitcompare.py`
(the renderfx rig of `test_render_host.py`, hidden desktop): render_host_test.exe of the tree without 3904 (3903's
02202612) and with it, 3 runs each of 4 scenarios (tap; bake stand-in with `--feed-plots 8`; `render_overlay=0`;
`render_water=1` + `render_water_freeze=1` with the Steam install's art, then build 4390913; since ticket 0589,
2026-11-19, the script takes gameinstall.py's install, never the Steam one), the same view rewritten with a new frame stamp
at 40 Hz and `render_test_dump_after=40`: colour and depth byte for byte equal in all 24 runs. With
`render_test_dump_after=1` the base alone gave two different pictures for the bake stand-in (the first frame is drawn
while tiles bake), so a compare must dump a frame after the bakes. `test_render_host.py` passes unchanged on the task's
build (47).

**The first bundles from the joined arm [L, 2026-11-15, c5h-a, workstation].** Product path by
`recorder_live.py` (over run_3308: ServerRenderer = 1, the composite with its manifest, D33 with Overlay on, ring +
fence, lockstep, `render_tiles=bake` with the pipeline baker; the keys through `server_cfg_extra`); server.exe
5b5159a6... and the proxy 6dcb747e... of this branch (build id repo a5a96ec3a, dirty 0), the pin's DLL 74588135...
(layout x64-04b1211b-pa803776), composite 54ee8dc8...; keys `_after=900 _frames=8 _dump=all`; no AI turn, no reload,
no picture taken; every changed file restored by hash, `provision.py verify` rc 0 after each.

| Bundle (F:/c5-64bit-work/scene-library/bundles/) | Save (copy of) | Field | Water image | Span | Overlay | Feed | Size | Checker |
|---|---|---|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261115-013528_workstation` | C5H-0301-TINY_0009 (C5H-0167-TINY_0008, b2ceb663...), game turn 8 | load 3, 27,525,248 B (56 x 40 C5HF plots) | 396,304 B | frames 1545-1601, 8 dumped, 2554 x 1411, flags 7 | 1 file, 7 pairs deduplicated | 6 drains, 2 tables | 23 members, 280,988,688 B | OK |
| `C5H-0301-HUGE_0350_20261115-014344_workstation` | C5H-0301-HUGE_0350 (C5H-VP8P-HUGE_0350, c499d3c6...), game turn 350 | load 3, 132,120,704 B (128 x 84) | 1,786,832 B | frames 1094-1150, 8 dumped, 2554 x 1411 | 8 files, 0 deduplicated | 10 drains, 1 table | 30 members, 544,071,108 B | OK |

Both: 0 record errors, every counter 0, the span started at published frame 899 (the key's 900) about 37-43 s after the
game was up. Every dump is drawn on every pixel (alpha > 0, depth < 1; numbers, no picture looked at). The water clock
state held 533 (Tiny) and 1702 (huge) waves. On the huge map every frame of the span had a new overlay texture (8 files,
22.4 MB each: as section 2.3 expected with an animating selection or highlight [I: what animated is not looked at]); on
the Tiny one all 8 pairs had equal texels. The synchronous map and write of two 14.4 MB targets per dumped frame spaced
the span's draws about 250 ms apart (frame ids step 6-11 under the 30 fps cap): with `_dump=all` the span is 2 s of game
time, a test run's cost.

**Not verified (3904).** P2's replay is tested on synthetic feeds (recorder_test.exe) and the live bundles' `feed.bin`
passes the parser, but no live bundle has been replayed into a residency and compared with the live counters (3905's
"offline equals live" does that). The recorder with `render_handover=v3` is tested only against the fake composite. A
device rebuild during a span, the 16 MiB early feed flush and the 600-iteration overlay wait are not exercised. The
bundles were made before epic 0168 (fog) and issue 0140 reached `main`; after those merges the bit-for-bit compare is
to be run again (MC #1304).

## 16. The player `sceneshot` and the check "offline equals live" (task 3905, 2026-11-15)

Written 2026-11-15 by the implementer of task 3905 (account B, branch `task-3905-sceneshot`). [C] = read in the code of
that branch or asserted by its tests; [L] = a run named below. (Section 15 is 3904's recorder.) The tool is
`server/tools/sceneshot/` (its README: build, command line, exit codes); its tests are
`server/tests/render/scenebundle/sceneshot_test.cpp` and the standing check `test_offline_equals_live.py`. No API of
3902's or 3903's files was changed.

**What the player does [C].**
1. `scenebundle::CheckBundle` on the whole bundle first. A refusal prints the checker's line and exits with
   `RefusalExitCode` (21..42); a missing bundle is `NoManifest`, exit 21: it fails, it never skips. Nothing is written
   for a refused bundle, command line, camera or frame (`--out` is created only at the first output).
2. The scene's options from `render_config`, read as **server.cfg keys** (`render_tiles`, `render_baker`,
   `render_bake_radius`, `render_color`, `render_water`, ...) through the server's own `ParseHostConfig`, then a port of
   the host's `SceneOpts` (file-local in `srv_render_host.cpp`). `render_water=full` (the proposal's spelling in section
   1) is read as `1`. A value the parser refuses, or `render_tiles=tap` (no bake-cache member, 2.4), exits 13
   Unsupported. The install is `--install` or the manifest's `asset_set.install`; the EXE `--exe` or
   `<install>/CivilizationV_DX11.exe`. The EXE is checked against `pins.exe_sha256`, and the install's asset set
   (`ComputeAssetSet`) against the bundle's `vt`/`vt_hash` when the bundle names one (not `unknown`); a difference exits
   9 unless `--allow-other-exe` / `--allow-other-assets` is given (then "not P1" is printed). The residency's budgets are
   the player's (`--bake-rate` 100000/s, `--bake-burst` 4096, `--bake-budget-ms` 50): they decide when a tile is baked,
   not what is baked; the bundle's values are printed beside them.
3. The device: `--adapter bundle` (default) = the adapter whose LUID is the manifest's, else whose description is the
   manifest's (a LUID changes at every start of Windows); `hw` = the largest hardware adapter (the host's rule); `warp`;
   or a LUID of 16 hex digits. Not found: exit 7. A comparison on another adapter than the bundle's is labelled "not P1".
4. The scene as the render thread builds it, in the player's form (`Scene::init` without a section, 3903), with the file
   feed in `ResidencySourceConfig::feed` (with `render_tiles=bake` feed.bin is required) and the file overlay source.
5. The replay, in the recorded order. Every feed record up to a frame's `loopIter` is applied first (the host drains
   before it polls and draws in the same iteration). A DRAIN record is drained and its changed plots go to
   `Scene::plotsChanged`, whose residency reads the next PLOTS (caller 2) record itself. A PLOTS record of caller 1 marks
   where a height field was applied, so the next heightfield member is loaded there (`LoadFieldMember`; members in
   (load, copy) order: `load<NNNN>.c5hf`, then 3904's `load<NNNN>-<k>.c5hf`), and its `takeBaseline` reads that record.
   A PLOTS record that no drain read, or a baseline the field load did not read, exits 8; a baseline inside the span
   exits 13. Without a residency (`render_tiles=none`) the last heightfield member is loaded. Then the last water image
   of the field's load (highest `copySeq`) and `water_clock.bin` (required beside a water image); then every frame of the
   span from the first to the last shot with its camera block, `nowMs`, overlay pair (`seekFrame(k)`, `Scene::poll`) and
   target size, into targets of the sinks' formats (colour B8G8R8A8_UNORM, depth R32_TYPELESS through a D32_FLOAT view).
   Frames before a shot are drawn too: the coast waves integrate every drawn frame's dt (6).
6. Each recorded frame's flags against the replay, as 3904 sets them (section 15): bit 1 = `WaterScene::drawsTotal` grew
   in the draw, bit 2 = `waveDrawsTotal` grew; and the overlay bound or not. A difference is a
   `sceneshot: MISMATCH frame ...` line; the run goes on so that every comparison is printed, then exits 8.
7. Before each shot `Scene::settle` (`--settle-ms`, default 120000). Not settled: exit 5 SettleTimeout (stopped: 6), the
   result's `what` printed, no picture. Then colour and depth are read back and, with `--compare`, compared byte for byte
   with `dumps/<frameId>.bmp` (pixels from byte 54) and `.depth.f32` **before any PNG is written**. Then `<stem>.png`
   (WIC, 32bpp BGRA as stored, alpha kept), `<stem>.depth.f32`, with `--diff` `<stem>.diff.png` (equal pixels as the
   offline picture's gray at a quarter, differing ones red by their difference), and `sceneshot.json`.
8. The comparison's numbers per frame (`sceneshot: COMPARE frame ...` and sceneshot.json): the differing pixels, those
   differing in alpha, the largest channel difference, the median and p95 of the per-pixel largest channel difference
   over every pixel and over the differing pixels; for depth the differing values (bitwise), those where one side is the
   clear value 1.0 and the other not (coverage), and the largest absolute difference.

**Selectors [C].** `--dumped` (every dumped frame: P1); `--frame ID` (a recorded frame; inside the span but not drawn:
exit 4); `--span FIRST:LAST`; `--camera NAME` (`cameras/NAME.vpcam`: with its `frameStamp` in the span that frame is
drawn with the named block, else the named block is drawn after the span's last frame and `--compare` exits 12). A
camera not in the bundle exits 3 and names the cameras the bundle has.

**Tests [C, run 2026-11-15 02:03 EST].** `sceneshot_test.exe` (built and run by `build_scenebundle_tests.py --run`), every
case on WARP and on the workstation's RTX 4080: 119 checks, 0 failed, 0 skipped, plain (clang-cl, 02:33 EST) and under MSVC
`/fsanitize=address` (02:42 EST, no AddressSanitizer report). The live side is the
test's own run of the scene in the host's order: the feed through `MakeRecordingFeed(LiveFeed())`, encoded with
`FeedEncoder` per loop iteration; the field and the water image in iteration 1; frames 100..105 in iterations 3..8; two
plot edits that pass the filter, before iterations 5 and 7; the module's water with the synthetic image's coast waves,
one dt of 300 ms above the 250 ms clamp; one overlay texture bound from frame 102; dumps of 100, 103 and 105. It is
written with 3902's Writer and passes the checker. Positives: offline equals live byte for byte on all three dumped
frames (bake with the stand-in baker, water, waves, overlay), and with `render_tiles=none`; `--frame`, `--span`,
`--camera` (in and outside the span), `--no-pictures`. The comparison has teeth: the same live run with two pixels and
one depth value of frame 103's dumps changed gives exit 10 with exactly "2 of 19200 pixels differ, 0 in alpha; largest
channel difference 16" and "1 of 19200 values differ", the other two frames equal, and a difference image. Negatives
(exit code and output line each, and nothing written): a missing bundle (21); no selector, two selectors, `--diff`
without `--compare`, an unknown argument, a reversed span (2); a camera not in the bundle (3); a frame and a span
outside it (4); a settle timeout (5, no picture; rate 0.5/s, burst 0, 300 ms); an older `format_version` (26); a changed
feed.bin byte (30 ShaMismatch); an unlisted file (29); a wrong `pins.exe_sha256` and a missing EXE (9);
`render_tiles=tap` (13); an adapter LUID not here (7); `--compare` on a frame without a dump (12). `--tolerance`: two
colour pixels (16 and 1) pass at 2:16 and fail at 1:16 and 2:15 (10); a depth difference passes no tolerance (10);
a malformed value (2). Not tested: exit 6
(a stopped residency needs a memory limit the player does not expose), 8 by a flag mismatch, and 11.

**P1 fails on main since f916762a5 (ticket 0482, 2026-11-17 07:45 EST) [L for the failure, C for the cause].** The
first `server/tests/render/run_all.py` plain run on main bf572fe37 failed `test_offline_equals_live.py` on all 7 bundles of
`F:/c5-64bit-work/scene-library/bundles` (recorded 2026-11-16 21:21-22:00): `sceneshot: FAILED 8 ReplayFailed: feed: out
of order: copyPlots (caller 2) called, the next record (N of M, loop iteration L) is PLOTS of caller 3`. Cause by code
reading: f916762a5 (4555 follow-up, 2026-11-16 23:21) added `ResidencySource::onFeedGeneration`, called from
`Scene::fowFeed` at each new snapshot generation, which reads the table through `copyPlots(caller 2)`; bundles recorded
before it hold the fog's caller-3 read at that place, so the replay's strict order refuses them. Bug 0493 fixes it:
re-record the library and add a feed/bundle version so that a stale bundle is refused at load with a clear message.
(Fixed 2026-11-17 by ticket 0493: bundle format version 7, the library recorded again; section 24.)

**Offline equals live on recorded bundles (P1) [L, 2026-11-15 02:13-02:28 EST].** On the workstation's NVIDIA GeForce
RTX 4080 (driver 32.0.16.1088, the bundles' adapter, picked by `--adapter bundle`), with the install
`%USERPROFILE%/c5-instances/c5h-a/install` (its EXE = `pins.exe_sha256` 04cfdc71...; asset set VT present 45b3b433...,
equal to the bundles'). Bundles of 3904 (section 15): product path, pins DLL 74588135, server 5b5159a6, proxy 6dcb747e,
composite 54ee8dc8; `render_config` in server.cfg form (pipeline baker, radius -1, water 1, overlay 1, ring hand-over),
read unchanged. Player: `sceneshot.exe --dumped --compare --diff` of `task-3905-sceneshot`.

| Bundle | Frames dumped | Colour | Depth |
|---|---|---|---|
| `C5H-0301-TINY_0009_20261115-013528_workstation` (2554 x 1411) | 8 of 8 (1545-1601) | byte-equal in all 8, in 2 runs | byte-equal in all 8 |
| `C5H-0301-HUGE_0350_20261115-014344_workstation` (2554 x 1411) | 8 of 8 (1094-1150) | byte-equal in 5; in 1109, 1118, 1135 one pixel of 3,603,694 off by 1 in one channel (median and p95 of the per-pixel difference 0) | byte-equal in all 8 |
| `C5H-0301-SCNB_0009_20261115-023727_workstation` (2554 x 1411; recorded 02:37, found in the library at 02:44) | 8 of 8 (1686-1730) | byte-equal in 7; in 1711 one pixel, (525, 62), B 235 live against 234 | byte-equal in all 8 |

The three Huge pixels: 1109 (2103, 68) live BGRA 240 190 158 255, offline 239 190 158 255; 1118 (2018, 1196) R 25 against
26; 1135 (912, 61) B 229 against 228. Their depth values are equal.

**The cause of the Huge differences [L for the runs, I for the mechanism].** The differences come from the server's own
renderer, not from the replay. Its picture is not bit-stable from run to run when the water module draws with the
overlay bound (water t9):
- In 8 runs of the player on the Huge bundle, frames 1135 and 1142 each take one of two states that differ by one pixel,
  ±1 in one channel. 1135: one state in 7 runs, the other in 1, which equals the live dump. 1142: two runs equal the live
  dump, six give the other state.
- 1109 and 1118 are stable over the 8 runs, one pixel from the live dump.
- With `--set render_water=0` the 8 runs are bit-identical in every frame, and with `--set render_overlay=0` too.
- The camera is still through the span (one depth hash for all 8 frames). Huge has a new overlay texture in every frame
  (8 `.ovl` files). Tiny has one and matched in all 8 frames of 2 runs; SCNB (the same Tiny save) also has one and shows
  the residue in 1 frame of 8. So a new texture per frame is not needed for the variation (corrected 02:45: an earlier
  draft of this line contrasted the 8 files with Tiny's one). All four differing pixels found lie near the top of the
  picture (y 1-68: the far, most minified part) or in dark water.

The mechanism inside the water module's sampling of the overlay is not identified. Candidates, none tested: the three
wrap-copy views blended into the same target, the t9 sampler state, a GPU-side effect.

**The standing tolerance: `1:1`, accepted by the MC (decision 2; asked 2026-11-15 02:29 in the 0301 queue, accepted as
#1403).** Depth byte-equal always; colour at most 1 pixel per frame, by at most 1 in any channel (`--tolerance 1:1`).
The reason is the variation above: the water module drawn over the overlay is not bit-stable from run to run. Follow-up
**0336** (water over overlay not bit-stable) is to find and remove it; when 0336 lands, the tolerance goes back to
`0:0` (equal bytes, the aim). The player's own default stays `0:0`; the standing check passes `1:1` and prints it.

**Times [L, an indication only, PERFORMANCE.md].** Each player run is one process: checker, scene, replay of the span, 8
shots. Tiny: 15.8 s for the whole run, of which 3.5 s is the first settle (the bake-ahead of 1352 land tiles). Huge:
30.6 s, of which 12.4 s is the first settle (4922 tiles). Per shot, settle + draw + readback + compare + PNG and depth
written: n 16 (Tiny, 2 runs), median 851 ms, min 748, max 4323 (the first shot carries the bake-ahead). Huge base runs,
3 with pictures and 5 without (`--no-pictures`): n 64, median 57 ms, min 36, max 13478. Other jobs shared the GPU.

**The plot-edit case of section 7 on live bundles: not covered.** No span holds a plot edit that passes the filter
(Tiny and SCNB: 1 changed plot, filtered; Huge: 3 changed, none passed, 0 re-bakes). It is covered only by the synthetic test
(two edits re-baked inside the span, equal bytes).

The standing check is `test_offline_equals_live.py`. It runs over every bundle in `F:/c5-64bit-work/scene-library/bundles`
(`C5_SCENE_LIBRARY`), with `C5_SCENESHOT_EXE` naming the built player. It runs with the accepted `1:1` by default and
prints "tolerance 1:1 (MC #1403; back to 0:0 when 0336 lands)"; `C5_P1_TOLERANCE` overrides it. No library or no bundle
fails. Run 2026-11-15 02:43-02:45 EST on the three bundles: with equal bytes 1 passed (Tiny) and 2 failed (Huge, SCNB);
with `1:1` 3 passed.

**Not verified.** That a live frame with a tile not yet baked can be matched: the player settles and the live server
does not, so such a frame would differ by that tile (a cause to name, not a player fault). Neither live bundle had one:
the bake-ahead was done before both spans. Exit 6 (a stopped residency) is not tested. The mechanism above is not known.

## 17. The library run `scenerun`, its report and the second machine (task 3906, 2026-11-15)

Written 2026-11-15 by the implementer of task 3906 (account B, branch `task-3906-scenerun`). [C] = read in the code of
that branch or asserted by its tests; [L] = a run named below. The tool is `server/tools/scenerun/` (`scenerun.py`,
`live_cycle.py`, README: commands and exit codes); its tests are `server/tests/render/scenebundle/test_scenerun.py` with
the stand-in player `scenerun_fake_player.py`.

**What the run does [C].** `scenerun.py run` takes every bundle of the library (the folder, and every entry of
`index.json` when there is one), checks every member's size and SHA-256 against scene.json, and scene.json against the
index, before anything runs (exit 4); a bundle the index or `--bundle` names that is missing, or an empty library, is
exit 3; in both cases nothing is written. Then it runs 3905's player once per bundle for the dumped frames (`--dumped`,
with `--compare --tolerance` on the bundle's adapter) and once per named camera (`cameras/<name>.vpcam`). A camera is a
distinct camera block (view 0 and projection rounded to 1e-3); the report groups the shots by camera. Three comparisons
are kept apart:
- **P1** (on the bundle's adapter only): the player's own comparison with the server's dump (section 16).
- **Regression:** each shot against the player's earlier picture of the same bundle and shot on the same adapter, kept in
  `F:/c5-64bit-work/scene-library/baselines/<machine>__<adapter>/<bundle>/` (on oldpc under
  `E:/c5-testbed/scene-library/baselines/`). Equal hashes = `equal`; depth not byte-equal = `differs`; colour within
  the tolerance (default 1:1, as P1) = `within`; a shot without a baseline is `new` and becomes it. A run without
  pictures (the time passes) does not judge a colour hash difference (`not judged`): the first oldpc timing pass failed
  on it (below).
- **Fidelity:** each shot against the stock shot of the same camera, found by the camera and not by name: the stock
  `<camera>.camera.json`'s EXE view 0 and projection against the bundle's camera block (rotation and projection within
  1e-3; translation within 0.01 world units = `same`, within 0.5 = `approximate`; a stock turn other than the bundle's
  load turn also makes it `approximate`, because auto end turn lets the AIs play turn 8 out during a run,
  `scene-catalogue.md` 9). Numbers per region mask: `all`; `server_drawn` (offline depth below 1.0) and `server_empty`;
  `stock_dark` (luminance <= 12); `stock_cloudlike` (luminance >= 150 and chroma <= 24: the fog's clouds, a heuristic
  [I] that also catches surf and snow); `compared` = drawn, not dark, not cloud-like. For each: the median, p95 and
  maximum of the per-pixel largest channel difference, and the shares equal, within 4 and within 16; a difference image
  x4 in `<out>/fidelity/`. Per draw (a terrainreplay comparison in `stock/<scene>/<camera>.terrainreplay/compare.json`):
  none exists in the library, so every shot says "no replay".
Every PNG is recorded with `visualcheck.py record --what "DIAGNOSTIC: scenerun ..."` and a result (pass = P1 within and
regression equal or within; fail = either differs; info otherwise), with the stock shot (or else the baseline) as the
reference, marked `same` or `approximate`, and the difference image as the comparison. Then `visualcheck.py list
--issue I --result undescribed` must be empty, else exit 8.

**The copy to a test machine [C, L].** `scenerun.py copy --host oldpc` checks the bundles on F:, writes the expected
hashes as an index file, stages the stock shots with their camera and state files, and ships everything through
`tools/testbed/tb.py submit --file`. The transfer is content-addressed: only files the host has not seen travel. The
host job `scenerun.py receive` checks each shipped bundle against the workstation's hashes, copies it into
`<testbed root>/scene-library/bundles/` through a `.incoming` folder, checks the copy again, checks the stock files,
and writes the host library's `index.json`. 2026-11-15 03:05-03:11 EST: 5 bundles of 1668 MB were sent as a 463 MB
compressed upload at about 3 MB/s (about 3 min), and receive took 10 s (job 20261115-031021-3ce8); the sixth bundle
followed later in 20 s (job 20261115-032437-b3b9). The negative: the same copy with one scene.json hash changed in the
expectation (`copy --corrupt-expect`, into a scratch library) was refused on oldpc with `scenerun: FAILED 4 ShaMismatch:
C5H-0301-HUGE_0350_... scene.json: SHA-256 4125000a..., the index says 0000...` (host exit 4), and the client ended
with `FAILED 10 HostFailed` (job 20261115-031107-747b). The first try failed on quoting (job 20261115-030545-686f):
tb.py joins the command with `list2cmdline`, so a `"%TESTBED_PYTHON%"` written with its own quotes arrived as
`\"...\"`. The command now carries no quotes of its own.

**oldpc facts [L, 2026-11-15].** The runner's Python 3.8.5 has numpy 1.24.3 and Pillow 8.0.1, so the whole run runs
there, fidelity included (C5H_PYTHON 3.12 has no numpy). The player draws on the RTX 2080 from the runner's session 0,
so no headed job is needed. The install is the testbed's `pure_vp` base, `E:/c5-testbed/base/pure_vp/Sid Meier's
Civilization V`. Its EXE is 04cfdc71... (the bundles' pin) and its VT folder equals the workstation's, so the player
needs neither `--allow-other-exe` nor `--allow-other-assets`. It runs with `--adapter hw`, because the bundle's adapter
is the workstation's (so not P1).

**The runs [L].** Player `sceneshot.exe` SHA-256 46878b42..., built from this branch (3905's c59d2aac merged). Library:
`C5H-0301-TINY_0009_..013528` (3904, Tiny), `C5H-0301-HUGE_0350_..014344` (3904, Huge) and 3908's four full scenes
`C5H-0301-SCNB_0009_..023727` (coast), `..025156` (river), `..025751` (city), `..030936` (highlights). Each has 8
frames and one camera (the camera stays still through every span). Workstation: ws-run1 (03:12, baseline, 5 bundles),
ws-run2 (03:14), ws-run3 (03:33, 6 bundles), ws-run4 (03:34, final). oldpc: jobs 20261115-031145-a240 (baseline),
-031347-916c, -032558-ae8e and -033713-a8cb (final). Outputs are under `F:/c5-64bit-work/0301/3906/` (`ws-run*/report.md`,
`oldpc-<job>/out/run/report.md`). Every run exited 0 except the first oldpc timing pass (exit 7, below).

| Bundle (camera key) | P1, workstation, final run | Regression, workstation, runs 2-4 against 1 | Regression, oldpc, runs 2-4 against 1 | Stock shot, match (translation, world units) | Fidelity, compared region: share of frame; median / p95 / max; within 4 | Whole frame median / p95 | Cloud-like share |
|---|---|---|---|---|---|---|---|
| Tiny (6f65304224) | 8 byte-equal | 24 equal | 24 equal | none at this camera | - | - | - |
| Huge (a38b39ccc1) | 5 equal; 1109, 1118, 1135 one pixel off by 1 | 22 equal; run 2 1135, 1142 within | 23 equal; run 4 1142 within | none | - | - | - |
| coast (90dce84520) | 8 equal (1711 one pixel in ws-run1, the baseline) | 21 equal; 1711 within in each run | 24 equal | coast_c1, same (0.0023) | 20.3 %; 11 / 92 / 246; 26.7 % | 116 / 197 | 79.7 % |
| river (e70be82f3b) | 6 equal; 1813, 1818 one pixel | 20 equal; 1818 within three times, 1813 once | 24 equal | river_c1, approximate (0.115; stock turn 9) | 33.6 %; 11 / 102 / 248; 24.0 % | 98 / 192 | 66.4 % |
| city (fa578b7965) | 8 equal | 24 equal | 24 equal | city_c1, approximate (0.081; stock turn 9) | 16.0 %; 8 / 100 / 246; 35.2 % | 109 / 193 | 84.0 % |
| highlights (aec09b21ef) | 8 equal | 8 new (run 3), 8 equal (run 4) | 8 new, 8 equal | highlights_c1, approximate (0.090) | 26.0 %; 9 / 97 / 244; 33.8 % | 106 / 197 | 74.0 % |

Every P1 comparison in every workstation run was within 1:1. The fidelity numbers are the same in every frame of a span
to the precision shown, and the same on both machines.

**Findings [L].**
- **The two adapters draw the same bytes.** ws-run4 (RTX 4080, driver 32.0.16.1088) against oldpc's final run (RTX 2080,
  driver 31.0.15.4592): 45 of 48 shots are byte-equal in colour and 48 of 48 in depth. The three others (Huge 1142,
  river 1813 and 1818) are frames of section 16's water-over-overlay variation. So a picture made on oldpc stands for
  the workstation's, except in those frames [L for these two cards; other vendors not tried].
- **The water-over-overlay variation is not specific to the RTX 4080.** On oldpc, Huge 1142 and coast 1711 changed their
  colour hash between runs (the first timing pass, job 20261115-031535-4296), and Huge 1142 was 1 pixel off by 1 in the
  final run. On the workstation the same frames varied, plus river 1813 and 1818. It was always one pixel off by 1 with
  depth equal, so inside 1:1.
- **Fidelity is dominated by what the bundle does not draw.** The fog's clouds cover 66-84 % of the stock shots (format 1
  has no fog member; `version_notes` names the gap). The stock also shows units, cities, trees and improvements that
  this arm's server target does not hold (`scene-catalogue.md` 9). The whole-frame median is 98-116. On the compared
  region (drawn, not dark, not cloud-like: 16-34 % of the frame) the median is 8-11, the p95 92-102, and 24-35 % of the
  pixels are within 4. The p95 and the maximum come from the stock's models standing on the terrain. These are the
  numbers COMMON 25 asks for. They show where the server stands against the stock picture with this arm, and are not a
  fault of the harness. (These are the version-1 bundles, without the server's fog. With the format-2 bundles, which
  draw the fog, the whole-frame median is 14-17: section 18, task 0353, 2026-11-15.)
- **The stock record's turn is not the bundle's.** river_c1 and city_c1 were shot at turn 9, while the bundles say turn 8
  (the load's `Game.GetGameTurn`). The first matcher required equal turns and found no stock shot for them (ws-run1-3,
  oldpc runs 1-3). Changed: the camera decides, and a different turn makes the reference `approximate`.

**Times:** see the "Measured times" lines after section 0. Visual Checks: after `visualcheck.py pull --host oldpc`
(03:45; 176 entries from oldpc) issue 3906 has 357 entries, none undescribed. Four of them are the live cycle's shots,
and one is a side by side looked at during the first run.

**Tests [C, 2026-11-15].** `python -I -m pytest server/tests/render/scenebundle/test_scenerun.py -q -p no:cacheprovider`:
11 passed, 0 skipped. They use synthetic bundles (camera blocks, members with hashes), a stand-in player with the real
command line and sceneshot.json, stock files and a temporary Visual Checks store. Positive tests: the run with its
report and records (DIAGNOSTIC, references); the regression baseline written and then equal; within and beyond the
tolerance; `--set-baseline`; no pictures = not judged; a named camera; another adapter = no P1; the stock match same,
approximate (translation), none (rotation) and approximate (turn); `--repeat` times; receive by hash, then verify.
Negatives, each asserting the exit code and the last line:
- a named bundle missing, an index entry missing, an empty library, no library: exit 3, nothing written;
- a member changed, an unlisted file, a short member, scene.json against the index: exit 4, nothing written;
- an undescribed picture of the issue: exit 8, with the id named;
- the player's exit 10: exit 6; the player's exit 8: exit 5;
- regression beyond the tolerance: exit 7;
- usage: exit 2;
- receive: a wrong expected hash or a member changed on the way: exit 4, nothing kept; a shipped bundle missing: exit 3.
There is no native code in this task, so no ASan run.

**Not verified.**
- The laptop was not run (oldpc covers the second machine). The laptop's testbed base carries VT too (quiet-testbed.md
  21.4).
- Per-draw fidelity: no terrainreplay replay of these cameras exists.
- The cloud-like mask is a heuristic, because there is no fog member to mask by.
- `index.json` was not written on F:. 3908 was still writing bundles there during the runs, and an index that lists a
  bundle later replaced would fail every run (exit 3). The workstation run uses the folder; oldpc's library has its index
  from `receive`.
- Named cameras (`cameras/<name>.vpcam`, `refs/`) are tested only on synthetic bundles: no library bundle has one yet.

## 18. The library recorded again at format version 2 (task 0353, 2026-11-15)

(Superseded 2026-11-15 23:00 by task 0364: the seven version-2 bundles below are in `retired/` with a `RETIRED.log` line
each; the library is format version 3, section 19. The numbers below stay as the record of version 2.)

Written 2026-11-15 by the implementer of task 0353 (account B, branch `task-0353-scene-library-v2` at `main` 975a36c9).
Rule B of "Commands and rules": a bundle of an older format is recorded again, never skipped. 3911 raised the format to 2
and moved every version-1 bundle to `retired/` (2.11); this task records 3904's Tiny and huge and 3908's four full
scenes again with the 3911 recorder as merged on `main`.

**The build (product path) [L, 2026-11-15 06:55-07:02 EST].** `build.sh` (scratch, not committed; it, the drivers `live.sh`, `series.sh`, `scn_live.py`, `saves_put.py`, `check.sh`, the run records and logs are kept in `F:/c5-64bit-work/0353/work-archive/`, the acceptance output in `F:/c5-64bit-work/0353/acceptance.txt`):
`server/build_server.py --src <copy of the pin's fork source 04b1211b8> --deps <copy of 9045's build_all output, DLL
74588135...> --config release --offline` (OK in 129 s); the layout = `x64-04b1211b-pa803776/bin` with this server.exe;
the composite `shell\build.cmd`; the checker `build_scenebundle_tests.py --only recorder_test`; the player
`build_sceneshot.py --smoke`. SHA-256: server.exe 6497f38c..., proxy ea5f76df..., composite f034061a...,
recorder_test.exe 3026f016..., sceneshot.exe 309929e2...; the DLL is the pin's 74588135.... The source and deps were
copied from `C:/c5-64bit-work/0349/` (that ticket's copies of 9045's build, hashes equal to the pin), so no LTO link ran.

**The drivers [C].** 3904's `recorder_live.py` (prep, run, restore) with `C5_RECORDER_OWNER=0353`: `--save tiny` and
`--save huge` as 3904 (`--after 900 --frames 8 --dump all`, `--settle-load 120`); the full scenes as 3908 did with its
`bundle_live.py`: a wrapper that adds the save `scn` (the copy `C5H-0301-SCNB_0009` of `C5H-0301-SCN_0009`), `--after
1500`, `--settle-load 200`, and `tools/scenario/runner.py camera tools/scenario/scenarios/scenes_tiny.json --frame
<coast_c1|river_c1|city_c1|highlights_c1>` beside the run, which sets the stock camera (and the highlights' stimulus,
with the interface hidden first) before the span starts. One game process per bundle, instance c5h-f. c5h-f holds the
huge source; the Tiny source (b2ceb663..., the same bytes as `c5h-9028-tiny-t0008` and `C5H-0301-SCN_0009`) was copied
in from c5h-a's `C5H-0167-TINY_0008` (read only there) under the two names the drivers expect, and removed by hash after.

**The recordings [L, 2026-11-15 07:03-07:47 EST, workstation, c5h-f claimed by 0353].** Product path as 3904 and 3911
(ServerRenderer = 1, the composite with its manifest, D33 with Overlay on, ring + fence, lockstep, `render_tiles=bake`
with the pipeline baker, `render_fow` at its default 1, FOWLevel 3 from c5h-f's `GraphicsSettingsDX11.ini`, asset set VT
present 45b3b433...); keys `_frames=8 _dump=all`; no AI turn, no reload, no picture taken in game. Every run: guard checks
0 / 0 / 0, `startup_watch_ok` true, stop rc 0, every backed-up file equal or absent by hash, the save copy removed,
`provision.py verify c5h-f` rc 0 (and `--quick` once more after the series, rc 0). Launch to in game 1:48-2:37 on Tiny, 4:05 on
huge. Every bundle: format version 2, `render_fow` 1 and `render_fow_level` 3 in `render_config`,
`fog_clock.bin` present, the only version note the bake-cache gap, every counter 0, 0 record errors; stamped (save, its
SHA-256, the load's game turn, the pins, `repo_commit`, release) and `scenebundle: OK`.

| Bundle (`F:/c5-64bit-work/scene-library/bundles/`) | Replaces (in `retired/`) | Save (copy of), turn | Camera | Span (published 899 or 1499 at its first frame) | Fog | Feed | Members, size |
|---|---|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261115-070309_workstation` | `..._TINY_0009_20261115-013528_...` | C5H-0301-TINY_0009 (b2ceb663...), 8 | as loaded | frames 1794-1872, 8 dumped, 2554 x 1411 | installed at frame 95; T = 57.092 s at the span's start | 7 drains, 8 tables | 31, 437,577,232 B (8 overlay files, 0 deduplicated) |
| `C5H-0301-HUGE_0350_20261115-071151_workstation` | `..._HUGE_0350_20261115-014344_...` | C5H-0301-HUGE_0350 (c499d3c6...), 350 | as loaded | 1838-1911 | frame 151; T = 57.093 s | 13 drains, 16 tables | 31, 544,071,920 B (8 overlay files) |
| `C5H-0301-SCNB_0009_20261115-072017_workstation` (coast) | `..._SCNB_0009_20261115-023727_...` | C5H-0301-SCNB_0009 (b2ceb663...), 8 | coast_c1 set 07:23:12, level 15.0 | 1870-1921 | frame 81; T = 59.717 s | 5 drains, 7 tables | 24, 280,988,856 B (1 overlay file, 7 deduplicated) |
| `C5H-0301-SCNB_0009_20261115-072718_workstation` (river) | `..._025156_...` | the same, 8 | river_c1 07:30:00, 15.0 | 1995-2047 | frame 78; T = 64.069 s | 6 drains, 7 tables | 24, 280,988,908 B (1, 7) |
| `C5H-0301-SCNB_0009_20261115-073355_workstation` (city) | `..._025751_...` | the same, 8 | city_c1 07:36:50, 17.0 | 1876-1926 | frame 81; T = 60.299 s | 6 drains, 7 tables | 24, 280,988,908 B (1, 7) |
| `C5H-0301-SCNB_0009_20261115-074048_workstation` (highlights) | `..._030936_...` | the same, 8 | highlights_c1 07:43:30, 15.0; warrior 1002 selected in move mode, the range highlight sent (6), interface hidden first | 1744-1789 | frame 81; T = 55.815 s | 6 drains, 7 tables | 24, 280,988,908 B (1, 7) |

- The water clock held 533 waves (Tiny saves) and 1702 (huge) at the span's start, as in 3904's bundles.
- `runner.py camera` read game turn 8 at the coast's camera set and 9 at the other three (auto end turn plays turn 8 out
  while the game waits, `scene-catalogue.md` 9); the stamp's `save.turn` is the load's, 8. No popup was open before or
  after the camera set (the runner's `popups` record empty).
- The Tiny bundle with the camera as loaded has a new overlay texture in every frame (8 files), as 3911's fog bundle on
  the same save; 3904's version-1 Tiny bundle had one (7 deduplicated). The full scenes, with the interface hidden, have
  one [L; what animates in the loaded view is not looked at, I: a UI element over the map that animates while shown].
- `repo_commit` in the stamps is the worktree's HEAD at the stamp (8d098e6f for Tiny, 83216136 for huge, coast and
  river, 6cff96ef for city and highlights); those commits differ from the build's 8d098e6f only in `research/` and
  `tracker/`, so every bundle was recorded by the same server.exe 6497f38c....

**The checks on the library [L, 2026-11-15 07:48-07:59 EST, workstation, RTX 4080 driver 32.0.16.1088].** `bundles/`
holds seven bundles, all format version 2: the six above and 3911's fog bundle. `check.sh` (same archive):
- the checker (`recorder_test.exe --check`) on each of the seven: `scenebundle: OK`;
- the standing check, `C5_SCENESHOT_EXE=<0353 build>/sceneshot.exe python -m pytest
  server/tests/render/scenebundle/test_offline_equals_live.py` (tolerance 1:1): `8 passed in 75.17s` (the seven bundles
  and the missing-bundle negative), 0 skipped;
- `scenerun.py run --player <0353 build>/sceneshot.exe --out F:/c5-64bit-work/0353/run1 --issue 0353 --tolerance 1:1`
  (through `quietlock.py run`), then the same into `run2`: both `scenerun: OK 7 bundle(s), 56 shot(s), 56 picture(s)
  recorded`, exit 0. P1: in both runs every one of the 56 shots is byte-equal to the server's dump (0 colour pixels
  differ, depth equal), so the 1:1 tolerance was not used. Regression: run 1 made the baselines
  (`baselines/workstation__nvidia-geforce-rtx-4080/<bundle>/`, all 56 `new`), run 2 is `equal` in 56 of 56. Reports:
  `F:/c5-64bit-work/0353/run{1,2}/report.md`. `quietlock.py check --quiet` exit 1 before each pass (other teams' jobs
  registered): the times in the reports are from a busy machine.
- [L] Not one of the 112 player shots and 56 standing-check shots showed section 16's water-over-overlay variation (one
  pixel off by 1), which 3905 and 3906 saw in frames of the version-1 huge, coast and river bundles. Follow-up 0336 is
  still open; this is one more data point, not a fix [I: the format-2 bundles draw the fog, which may change what the
  water draws over].
- Visual Checks: 112 DIAGNOSTIC entries for issue 0353 (scenerun's own records), `visualcheck.py list --issue 0353
  --result undescribed` empty.

**Fidelity against the stock shots, with the server's fog [L, run 1; run 2 the same].** The four full scenes match their
stock cameras (`coast_c1` same, 0.0005 world units; `river_c1` approximate, 0.115 and stock turn 9; `city_c1`
approximate, 0.020, stock turn 9; `highlights_c1` approximate, 0.088). Tiny, huge and the fog bundle have no stock shot at
their camera. Per-pixel largest channel difference, the same in every frame of a span to the precision shown:

| Scene | Whole frame median / p95 (version 1, section 17) | Whole frame within 4 / 16 | Compared region: share; median / p95 / max; within 4 (version 1) | Stock cloud-like region: share; median / p95; within 16 |
|---|---|---|---|---|
| coast | **17 / 60-61** (116 / 197) | 12.1 % / 48.1 % | 20.3 %; 10 / 82 / 246; 27.9 % (11 / 92 / 246; 26.7 %) | 79.7 %; 19 / 56; 44.4 % |
| river | **14-15 / 76-77** (98 / 192) | 15.8 % / 54.8 % | 33.6 %; 10 / 95 / 248; 25.0 % (11 / 102 / 248; 24.0 %) | 66.4 %; 16 / 56; 51.5 % |
| city | **16 / 59** (109 / 193) | 13.9 % / 50.1 % | 16.0 %; 7 / 89 / 246; 36.6 % (8 / 100 / 246; 35.2 %) | 84.0 %; 17 / 54; 47.5 % |
| highlights | **16 / 63-64** (106 / 197) | 16.5 % / 51.1 % | 26.0 %; 8 / 85 / 244; 35.6 % (9 / 97 / 244; 33.8 %) | 74.0 %; 18 / 54; 46.7 % |

- The fog moved the whole-frame median from 98-116 to 14-17 and the p95 from 192-197 to 59-77: the stock shots are 66-84 %
  clouds, and the version-1 bundles drew bare terrain there. In the cloud-like region the median is now 16-19.
- The compared region (drawn, not dark, not cloud-like) changed little (median 1 lower, p95 7-12 lower): it excludes the
  clouds by the stock's colour. Its p95 and maximum still come from what this arm's server target does not hold (units,
  cities, trees, improvements; section 17).
- The cloud-like mask is still the heuristic of section 17. The fog member now exists, so a mask from the server's own
  fog (the plots drawn under clouds) is possible; not done here [I].

**Version-1 bundles: kept.** Section 2.11 moved them to `retired/` with a line each in `retired/RETIRED.log`; this task
leaves them there (nothing deleted; 3911's first fog bundle is there too). `index.json` is still not written (section
17): the run takes every bundle of the folder.

**Not verified (0353).** The new bundles were not copied to oldpc and not run there (the ticket names the workstation).
Per-draw fidelity: still no terrainreplay of these cameras. Whether anything moved into a full scene's view between
the camera set and the span (auto end turn) was not checked, as in 3908. The recorder's `keys absent = bit for bit`
compare of 3904 / 3911 was not run again (no recorder code changed).

## 19. Format version 3 live: the decal span offline equals live, the library recorded again (task 0364, 2026-11-15)

Written 2026-11-15 by the implementer of task 0364 (account B, branch `task-0364-bundle-v3-members`). The forms are 2.10
(decals) and 2.12 (shadow, ice); this section is the live record.

**The build (product path) [L, 2026-11-15 22:43-22:50 EST].** `build.sh` (scratch, after 0353's
`build.sh`; kept with the drivers `live.sh`, `series.sh`, `scn_live.py`, `check.sh`, `nowalk.py`, `retire_v2.py`, the logs and the acceptance output (`F:/c5-64bit-work/0364/acceptance.txt`) in `F:/c5-64bit-work/0364/work-archive/`; the C: scratch was removed at the hand-back): `server/build_server.py --src <copy of the
pin's fork source 04b1211b8> --deps <copy of 9045's build_all release output, DLL 74588135...> --config release
--offline` (OK in 139 s; the source and deps copied from `C:/c5-64bit-work/0365/`, so no LTO link ran); the layout =
the pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with this server.exe; the composite `shell\build.cmd`; the
checker `build_scenebundle_tests.py --only recorder_test`; the player `build_sceneshot.py --smoke`. SHA-256: server.exe
66909542..., proxy 449da65f..., composite 7b50cfee..., recorder_test.exe 4793d1d7..., sceneshot.exe 0f1d8fa2....

**The decal span, offline equals live [L, 2026-11-15 22:51-22:58 EST, workstation, c5h-a claimed by 0364].** 3904's
`recorder_live.py` (through `scn_live.py` of the archive, owner 0364), `--save tiny --after 900 --frames 8 --dump
all`: the product path (ServerRenderer = 1, the composite with `decal_tap=1 shadow_tap=1 ice_tap=1`, D33 with Overlay
on, so the EXE draws no decals and the server does), save copy C5H-0301-TINY_0009 of c5h-a's C5H-0167-TINY_0008
(b2ceb663..., game turn 8), `render_decals`, `render_shadow`, `render_ice`, `render_fow` at their defaults 1. Bundle
`C5H-0301-TINY_0009_20261115-225124_workstation`: format version 3, 35 members, 349,474,735 B; frames 1077-1113, 8
dumped, 2554 x 1411; `decals/load0003.dtap` 294,832 B (decal set generation 5, load 3, 314 decals, 28 names);
`shadow.bin` 8 records with 8 `.shtx` files; `ice/gen0000000001.ice` 1,070,310 B; `render_decal_mods` = c5h-a's own
`Documents\My Games\Sid Meier's Civilization 5\MODS`; the only version note the bake-cache gap; every counter 0; stamped
and `scenebundle: OK`. Played with this branch's sceneshot (`--dumped --compare --tolerance 1:1 --diff`, the RTX 4080 of
the recording): **colour and depth byte-equal in 8 of 8 frames** (0 pixels differ; the 1:1 tolerance not used), no
MISMATCH; the player's lines: the recorded mods' folder, `decals/load0003.dtap: decal set generation 5 (load 3, 314
decals, 28 names) applied before the span`, `shadow.bin: 8 records, 8 shadow map file(s), 1 ice mesh(es)`. Teeth: the
same bundle with `--set render_decals=0` differs in 392,808-393,197 pixels per frame (largest channel difference 217,
median over the differing pixels 10-11, p95 82), depth equal, exit 10. This closes epic 0166's done item I
(`terrain-decals.md` 16.6) and 0169's open item: 4308's live replay was equal but in the decal tiles because its bundle
had no decal input. Picture: Visual Checks `vc-20261115-225857-workstation-4dc3` (DIAGNOSTIC, frame 1077, pass).

**Found on the way: an instance left with a patched front end [L, 2026-11-15 22:56-23:05].** c5h-a's front-end state
(`tools/harness/frontend.py`) said prepared at 06:07 (by an earlier run) and never restored: MainMenu.lua was a patched
copy (df892e9e...) instead of the manifest's original (5c525c54...). `run_3308.py`'s own backup (taken at `prep`, before
the run) holds that patched copy and restores it after `frontend.restore`, so the instance stays patched and the next
`prepare` refuses ("does not match install.manifest.tsv ... re-provision or restore first"); `provision.py verify` rc 1
("install: size Assets\UI\FrontEnd\MainMenu.lua"). Fixed by `frontend.restore(Instance.load("c5h-a"))` (restores the
06:07 backup of the originals; verify rc 0 after). A driver that backs up the front-end files itself should run
`provision.py verify` before its first run [I: a prep-time check would have caught it].

**The library recorded again at format version 3 [L, 2026-11-15 22:59-23:42 EST, workstation, c5h-a claimed by 0364].**
At 22:59 the seven version-2 bundles of `bundles/` (0353's six and 3911's fog bundle) were moved, not deleted, to
`retired/`, one `RETIRED.log` line each; the Tiny acceptance bundle above was copied in; then `series.sh` recorded the other
six as 0353 did (`live.sh`: `fog` and `huge` with `--after 900`, the four full scenes with `--after 1500` and
`tools/scenario/runner.py camera ... scenes_tiny.json --frame <coast_c1|river_c1|city_c1|highlights_c1>` beside the run;
the scenes' save copy C5H-0301-SCNB_0009 taken from c5h-a's C5H-0167-TINY_0008, the same bytes as 3908's
C5H-0301-SCN_0009). One game process per bundle; every run: stamp and checker `scenebundle: OK`, the camera set
128-148 s after the start (before the span), `provision_verify_rc` 0, the save copy removed; `provision.py verify c5h-a
--quick` rc 0 after the series. Every bundle: format version 3, `render_decals` / `render_shadow` / `render_ice` /
`render_fow` 1, `render_fow_level` 3, 8 frames all dumped at 2554 x 1411, 8 `.shtx` files (no two maps equal), one
`ice/gen0000000001.ice`, the only version note the bake-cache gap, every counter 0.

| Bundle (`F:/c5-64bit-work/scene-library/bundles/`) | Replaces (in `retired/`) | Span | Decal member | Members, bytes | Offline equals live (1:1) |
|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261115-225124_workstation` | `..._TINY_0009_20261115-070309_...` | 1077-1113 | load0003, generation 5, 314 decals, 28 names, 294,832 B | 35, 349,465,942 | byte-equal 8/8 |
| `C5H-0301-FOG_0008_20261115-230406_workstation` | `..._FOG_0008_20261115-052907_...` | 1345-1415 | generation 5, 318 decals, 295,216 B | 35, 349,466,414 | byte-equal 8/8 |
| `C5H-0301-HUGE_0350_20261115-230946_workstation` | `..._HUGE_0350_20261115-071151_...` | 1138-1212 | generation 5, 17,756 decals, 256 names, 7,663,360 B | 42, 623,612,716 (8 overlay files) | **differs: 5,649-5,701 px per frame** (below) |
| `C5H-0301-SCNB_0009_20261115-231635_workstation` (coast) | `..._072017_...` | 1799-1855 | generation 6, 314 decals, 293,104 B | 35, 349,464,214 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261115-232315_workstation` (river) | `..._072718_...` | 1585-1635 | generation 5, 314, 293,040 B | 35, 349,464,098 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261115-232929_workstation` (city) | `..._073355_...` | 1622-1670 | generation 5, 314, 295,952 B | 35, 349,467,062 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261115-233551_workstation` (highlights) | `..._074048_...` | 1675-1727 | generation 6, 314, 293,168 B | 35, 349,464,278 | byte-equal 8/8 |

(The Tiny saves' 314 or 318 decals at the same save: the load's own randomness of `terrain-decals.md` 3.1 [I].)

**The checks [L, 2026-11-15 23:43-23:56 EST, RTX 4080 driver 32.0.16.1088] (`check.sh`).** The checker on all seven:
`scenebundle: OK`. The standing check (`test_offline_equals_live.py`, 1:1): **7 passed, 1 failed**: the huge bundle.
`scenerun.py run` twice (`F:/c5-64bit-work/0364/run1`, `run2`): 48 of 56 shots byte-equal to the live dump (0 pixels),
exit 6 for the huge bundle's P1; regression run 2 against run 1 `equal` in 56 of 56 (the replay itself is stable, the huge
residue included). Fidelity against the stock shots (compared region median / p95 / max (share); whole frame median /
p95): coast 8 / 75 / 226 (20.3 %); 17 / 58; river 8 / 90 / 225 (33.6 %); 14 / 69-70; city 6 / 83 / 229 (16.0 %); 16 / 57;
highlights 6 / 77 / 221 (26.0 %); 15 / 61 (against 0353's version 2: compared-region median 10 / 10 / 7 / 8, p95 82 / 95
/ 89 / 85: the decals, shadow and ice lowered both). Visual Checks: 56 entries of issue 0364 by scenerun, undescribed
none. Times (indications, a busy machine, `quietlock.py check` exit 1): later shots 692-734 ms on the Tiny scenes, 1,342
ms on huge; first shot 3.7-5.2 s (Tiny), 39.3 s (huge: its decal set's bake-ahead).

**The huge residue, named and its cause shown [L, 2026-11-15 23:43 - 2026-11-16 00:10 EST].** Every frame of the huge span
differs in 5,649-5,701 pixels (largest channel difference 48; median over the differing pixels 4, p95 18), depth equal,
no MISMATCH line; the difference image (`F:/c5-64bit-work/0364/run1/rep01/C5H-0301-HUGE_0350_20261115-230946_workstation/
frames/1138.diff.png`) puts all of it in one horizontal strip about one tile high across the city ground at the screen's
centre. The live run's `render.log`: the huge load published five decal generations, 1, 4 and 5 with the walk block
(`tie order the EXE's walk`, ticket 0366) and **2 and 3 without it (`the 4111 rule`)**; generations 4 and 5 changed only
161 and 168 rectangles (24 and 28 tiles raised). The six Tiny bundles, whose every generation carried the walk block, are
byte-equal. **Cause, shown:** the same bundle replayed with its decal member's walk block switched off
(`walkOffset` = 0, so the player orders ties by the 4111 rule; a diagnostic copy made by `nowalk.py` of the archive,
scene.json's entry updated, the checker OK, never in the library) is **byte-equal to the live dump in 8 of 8 frames**. So
every tile the live server drew in that view was baked under generation 2 or 3's tie rule and kept it: the walk block
appearing in generation 4 raised no tile, because `DecalChange` raises the changed rectangles, the added and removed
decals and the tiles whose area state changed (`areaTiles`, 4111), but nothing when the tie rule changes [C:
`srv_decal_tap.h`]. A second recording with the span at published frame 2400 instead of 900
(`C5H-0301-HUGE_0350_20261115-235721_workstation`, scratch, not kept; its generations 2-3 again without the walk block)
differs the same way (6,380-6,401 px, max 61, the same strip) and is byte-equal 8/8 with the walk block off: waiting
does not heal it. **The live server's picture is the stale one** (the EXE's order for ties of equal priority, 0366, is
lost on those tiles until something else re-bakes them); the replay is right. Proposed follow-up for the decal owners
(epic 0166 / ticket 0366, the MC): raise every tile with decals when the walk block appears or disappears between two
applied sets (as 4111 does for the area rule). Until then the huge bundle fails the standing check by this residue (exit
10 / scenerun exit 6) and is kept in the library as the product's honest picture; 0364 does not change the tolerance.
*(Corrected 2026-11-16 04:41 by ticket 0393: the server now raises the tiles whose draw list changes when the walk block
appears or goes (`terrain-decals.md` 20); the huge bundle was recorded again with that server
(`C5H-0301-HUGE_0350_20261116-042357_workstation`, byte-equal 8/8 at 0:0) and this one moved to `retired/`; the
standing check on the library passes 7 of 7.)*

**Suites [C, 2026-11-15 22:24 - 2026-11-16 00:51 EST, branch `task-0364-bundle-v3-members`].** `build_scenebundle_tests.py
--run`, plain (clang-cl) and `--asan` (MSVC): bundle_format_test 527, recorder_test 159, sceneshot_test 163 (WARP and
the RTX 4080), sources_test 24, all passed, 0 skipped, 0 ASan reports. **Under ASan sceneshot_test needs about 16 min**,
above the script's default `--timeout 600` (the first ASan run failed by that timeout alone; rerun with `--timeout
2700`: passed) [L]. `build_render_tests.py` plain and `--asan`, then `test_recorder_host.py` 7, `test_sources.py` 3,
`test_scenerun.py` 11 and `server/tests/render/test_decal_wiring.py` 15 passed on each. `render/build.py --target
c5r_units_test_placement_catalogue --test` (its default height field now the format-3 Tiny bundle's, MC #2109): 1/1
passed. Not run: the other render suites (`render/` is unchanged).

**Notes [C].** The recorder's check of the decal image (`ParseDecalTap` -> `DecalTapReader(0).loadImage`) writes one
more `decals: generation N applied: ...` line into the server's `render.log` at the span's first frame (a reader without
a tap; the scene's own reader is not touched); read it as the recorder's, not a new set. The players of `main` before
0364's merge refuse the format-3 library as `NewerVersion`.

**Not verified (0364).** The library was not copied to oldpc or run there. The huge residue's fix (raising tiles when
the tie rule changes) is not made here. A decal generation applied inside a span (the version note) did not occur in
any of the eight recordings, so that note was exercised by `recorder_test` only. `--mods` was tested in `sceneshot_test`
only; every live bundle used its recorded `render_decal_mods`.

## 20. Format version 4: the unit feed's records in feed.bin (task 4457, epic 0375, 2026-11-16)

Written 2026-11-16 by worker W-4457 (account A, branch `task-4457-records` from `epic-0375-units-scene` 3e56d803) on
the MC's grant of decision 11 (queue #2174: 0364's bundle files, a raise to format version 4, older bundles refused with
both versions named, the library recorded again once with the old bundles moved to `retired/`, every bundle's
byte-equal replay shown after). The record layouts and the unit tool are in `unit-server-scene.md` 11.1; this section
is what changed in the bundle library and its suite.

**Forms [C].** `kFormatVersion` 4; `kFeedFormVersion` 2 (feed.bin's header version and its member `version`; every other
member stays 1). DRAIN: `unitTableGen` u32 and `drainMs` f64 after `dropped`. Records 3 UNITS, 4 PATHS, 5 COMBATS,
6 PLAYERS through `srvunits::rec` (`server/src/units/srv_units_records.h`, inline: no new source in any build list).
Placement: 4, 5 and 6-of-caller-2 right after their DRAIN (same loopIter, one of each at most), folded into
`FeedRecord::batch`; 3 and 6-of-caller-1 after the newest DRAIN of their loopIter (PLOTS reads may stand between),
folded into `FeedRecord::unitCopies` / `playerCopies`. `ParseFeed` yields DRAIN and PLOTS records only, so sceneshot,
which walks its own parsed record list and the file feed in step, needs no change (its 163 checks pass unchanged on
the new library code). `FeedEncoder::addDrain` returns a Status now and takes `drainMs`; `addUnits`, `addPlayers`
(caller 1) are new. `FeedSource::copyUnits` / `copyPlayers` (live defaults), `FeedRecordSink::onUnits` / `onPlayers`
(no-op defaults), the recording feed passes them through, the file feed serves them per DRAIN
(`FileFeedControl::lastDrainMs`, `unitCopiesLeft`), `SceneRecorder` records them.

**Refusals added (each a child-process negative: exit code, output line, nothing written) [C, `bundle_format_test.cpp`
TestUnitRecords].** feed.bin form 1 (BadVersion, exit 32); a feed member listed with version 1 (BadVersion); format
version 3 (OlderVersion, exit 26, "format_version 3 is older than 4"); a record 3-6 the codec refuses (WrongSize 34 for
Truncated / WrongSize, else BadRecord 41, the codec's code in the why); PATHS not right after its DRAIN, of another
loopIter, before any DRAIN, twice, after the copies; a PLAYERS copy without a DRAIN of its loopIter; DRAIN drainMs NaN;
type 7. The encoder refuses a batch the codec refuses and writes nothing (the recorder then counts a record error and
the bundle is incomplete).

**Suite (2026-11-16) [run].** `build_scenebundle_tests.py --run` with `C5R_EXE` = the Ghidra copy of the EXE and
`C5_GAME_DIR` = c5h-b's install (read only): plain (clang-cl) bundle_format_test 626 passed, recorder_test 159,
sceneshot_test 163, sources_test 24, 0 failed, 0 skipped; MSVC `/fsanitize=address`: bundle_format_test 626, recorder_test
159, sources_test 24, 0 AddressSanitizer reports; sceneshot_test under ASan needs `--timeout 2700` (0364's note above: the
default 600 s ran out, exit "timeout after 600 s") and was rerun alone: 163 passed, 0 failed, 0 reports (2026-11-16 02:14). Logs: `F:/c5-64bit-work/0375/4457/logs/`. The units suite
(`build_units_tests.py`, test_scene_units.exe) builds with the changed headers; its case `feed_out_of_order` fails (21 of
22) on the team branch's base 3e56d803 as well (built and run there, 2026-11-16 02:10), so not from 4457.

**Not done (named for lead-0375 and the MC).**
- **The library is not recorded again yet.** *(Done 2026-11-16 by issue 4467, section 21: recorded at format
  version 4 into staging folders, units off for the drop-in library and units on beside it.)* The MC's grant asks for one re-recording at format version 4 (old bundles
  to `retired/`, byte-equal replay of each after). Recording now would hold no UNITS / PLAYERS(1) records: the host
  still gives the unit scene `LiveFeedPort()`, not `srvunits::FeedSourcePort(*h.feed)` (`srv_units_scene.cpp`, W-4460's),
  and `render_units` defaults to 0. Recommended: re-record once, after W-4460's integration carries that one-line
  wiring, with `render_units=1`, so that the library is raised once and holds the unit feed. Until then every
  version-3 bundle in `F:/c5-64bit-work/scene-library` is refused by this branch's players as OlderVersion.
- No live bundle with the unit records was made (no integrated server.exe on the team branch at 01:50 EST; the
  synthetic bundles of `server/tests/units/unitfeed_fixture.cpp` stand in, `unit-server-scene.md` 11.1).
  *(2026-11-16, issue 4456: the wiring is done: the host hands the recording feed to the unit scene
  (`SceneUnits::useFeedSource` in `StartRecorder`) and the drain clock through `SceneRecorder::lastDrainMs()` (new, MC
  grant to 0375: the value `addDrain` wrote, -1 for a drain not recorded; recorder_test checks every DRAIN's `drainMs`
  against it and the -1 after the finish: 162 checks, 2026-11-16). Live bundles with units: `unit-server-scene.md` 11.2.
  **A span ends at the server's first redraw of a frame id** (the recorder's rule: "frame N drawn after frame N (a
  redraw or a new composite): the span ends there"); with the unit scene on, a feed batch between two EXE frames makes
  the server draw the same frame id again, which happens within seconds of an AI turn. With
  `render_test_record_dump=first,last` the last dump is then never taken and the bundle is INCOMPLETE (live run 1 of
  4456, 03:58:35 EST: span ended at frame 1759, 1 of 2 dumps); `every:N` keeps it complete. The library re-record at
  format version 4 is the MERGE-REQUEST's, not 4456's (0364's grant condition).)*

## 21. The library recorded again at format version 4 (task 4467, epic 0375, 2026-11-16)

Written 2026-11-16 by worker W-4467 (account A, branch `task-4467-library-v4` from `epic-0375-units-scene` 66891da6) on
the MC's grant #2174 (section 20: one re-recording at format version 4, the old bundles moved to `retired/`, every
bundle's byte-equal replay shown after). Main's sceneshot reads format 3 until 0375 merges, so the new bundles are
**staged** beside the library and the MC swaps them at the merge (commands below); `bundles/` and `retired/` were not
touched.

**The build (product path) [L, 2026-11-16 07:08-07:22 EST].** `server/evidence/0375/4467/build.sh` (after 0364's):
`server/build_server.py --src <copy of the pin's fork source 04b1211b8> --deps <copy of 9045's build_all release
output, DLL 74588135...> --config release --offline` (OK in 284 s; the copies taken from `C:/c5-64bit-work/0410/pp/`;
server build id `repo_commit=66891da69, repo_dirty=0`, proxy `240a6043e`, which differs only in `server/evidence/` and
`tracker/`); the layout = the pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with this server.exe; the composite
`shell\build.cmd`; `render/build.py` targets c5render, c5r_models, unitscene, unitanim; the checker
`build_scenebundle_tests.py --only recorder_test`; the player `build_sceneshot.py --smoke`; `build_unitfeed.py`.
SHA-256: server.exe e0916388..., proxy 2b5d8837..., composite b5e4cdbb..., recorder_test.exe 1c1d24bf..., sceneshot.exe
25599a9f..., unitfeed.exe dc33f2c6..., unitscene.exe 4ca4617b..., unitanim.exe 7f41cd3b.... Scratch (removed at the hand-back):
`C:/c5-64bit-work/4467/`.

**The drivers [C] (`server/evidence/0375/4467/`).** `rec_live.py` = 3904's `recorder_live.py` imported unchanged, plus
0364's `scn` save (the copy C5H-0301-SCNB_0009 of c5h-a's C5H-0167-TINY_0008, b2ceb663...) and, unless
`C5_4467_UNITS=0`, the unit keys appended to the run's `extra.cfg` (TEST ONLY, `server_cfg_extra`): `render_units=1`,
`render_test_units_dump` (max 20000), `render_test_units_table`, `render_test_units_anim`; after `run` it copies the
server's `db/units-*.db` (unitfeed's `--db`) and `render.log`. `live.sh` and `series.sh` / `series_off.sh` are 0364's
(one game process per bundle; `tiny` and `fog` and `huge` with `--after 900 --settle-load 120`, the four full scenes
with `--after 1500 --settle-load 200` and `tools/scenario/runner.py camera ... scenes_tiny.json --frame
<coast_c1|river_c1|city_c1|highlights_c1>` beside the run), plus `dialogwatch.py marker` and `df` after every item;
`--frames 8 --dump all`. Instance c5h-a (Goldberg, C:, Vibrant Textures present), claimed by W-4467 (ticket 0375: the
pool refuses 4467, which is not a tracker file on `main`). Every run: `scenebundle: OK` by stamp and checker,
`provision_verify_rc` 0, the save copy removed, the D155 marker `no box`; `provision.py verify c5h-a --quick` rc 0
after each series.

**Found: a units-on bundle cannot be replayed by sceneshot [L, 2026-11-16 07:45].** The branch's player does not link
the unit scene (`server/src/units/`), and the bundle's `render_config` carries `render_units=1`:
`sceneshot: FAILED 13 Unsupported: render_config refused by the server's own parser: render_units=1: this program does
not link the unit scene (server/src/units/)`. With `--set render_units=0` it replays and differs only where the live
server drew units (Tiny, frame 1862: 23,354 of 3,603,694 pixels, colour and depth, largest channel difference 224,
exit 10). Two more gaps stand behind that one: the units' database is not a bundle member (the server builds
`units-1.1.db` in its run folder at the load; unitfeed takes it by `--db`), and the controller's state at the span's first
frame is not either (the controller starts 730-801 published frames before a library span, so a replay's clip times
cannot equal the live ones; 4456 recorded from frame 1 to cover it, 40 GB). The recorder names none of the three as a
`gap:` in `version_notes` (rule A). So a library recorded with units on would fail the standing check at the swap.
**Decision (W-4467, 07:50, brief rule 8: decide, note, go on):** two sets.
- `F:/c5-64bit-work/scene-library/staging-v4/`: **units off** (`render_units` at its default 0), format 4: the drop-in
  library, every frame byte-equal by the standing check. Its feed holds the DRAIN form 2 and PLOTS; no record 3 or 6
  of caller 1 (no unit scene), so unitfeed refuses these bundles (below).
- `F:/c5-64bit-work/scene-library/staging-v4-units/`: **units on** (`render_units=1`), format 4 with the records 3-6:
  unitfeed's input (D137's offline harness for the unit scene), checked against the live server's own unit dump. Not
  for `bundles/` until sceneshot links the unit scene and the two members above exist (proposed follow-up, named for
  lead-0375 and the MC).

| Bundle (`F:/c5-64bit-work/scene-library/staging-v4/`, units off) | Replaces at the swap (to `retired/`) | Recorded (EST) | Span (8 frames, all dumped, 2554 x 1411) | feed.bin records (1 DRAIN / 2 PLOTS) | Members, bytes | Offline equals live (0:0) |
|---|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261116-081845_workstation` | `..._TINY_0009_20261115-225124_...` | 08:18-08:26 | 2136-2216 | 4 / 6 | 35, 349,477,510 | byte-equal 8/8 |
| `C5H-0301-FOG_0008_20261116-082633_workstation` | `..._FOG_0008_20261115-230406_...` | 08:26-08:33 | 1282-1349 | 7 / 8 | 35, 349,479,103 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261116-083247_workstation` (coast) | `..._SCNB_0009_20261115-231635_...` | 08:32-08:40; camera set 08:36:23, span 08:37:40 | 2701-2787 | 7 / 8 | 35, 349,474,499 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261116-084042_workstation` (river) | `..._232315_...` | 08:40-08:48; 08:44:04, 08:44:59 | 2116-2187 | 6 / 7 | 35, 349,474,239 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261116-084818_workstation` (city) | `..._232929_...` | 08:48-08:55; 08:51:15, 08:52:06 | 1879-1935 | 6 / 7 | 35, 349,475,551 | byte-equal 8/8 |
| `C5H-0301-SCNB_0009_20261116-085529_workstation` (highlights) | `..._233551_...` | 08:55-09:02; 08:58:22, 08:59:02 | 1796-1857 | 6 / 7 | 35, 349,476,095 | byte-equal 8/8 |
| `C5H-0301-HUGE_0350_20261116-090229_workstation` | `..._HUGE_0350_20261116-042357_...` | 09:02-09:09 | 1440-1520 | 11 / 14 | 42, 623,670,208 (8 overlay files) | byte-equal 8/8 |

Every bundle: format version 4, `render_units` 0 in `render_config`, `render_decals` / `render_shadow` / `render_ice` /
`render_fow` 1, the only version note the bake-cache gap, every counter 0, 0 record errors; stamped (save, SHA-256, the
load's turn: 8 for the Tiny saves, the coast's stamp 9, huge 350; pins, `repo_commit`, release). The six Tiny-save
bundles share the save bytes b2ceb663...; the four cameras were set 40-77 s before their span (runner `camera`
164-205 s after the start). Total 7 bundles, 2,720,527,205 B (2.5 GiB).

| Bundle (`F:/c5-64bit-work/scene-library/staging-v4-units/`, `render_units=1`) | Recorded (EST) | Span | feed.bin records (1 / 2 / 3 UNITS / 6 PLAYERS) | Members, bytes | Controller start / span start (frame) | unitfeed replay vs the live unit dump |
|---|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261116-072146_workstation` | 07:21-07:29 | 1705-1862 | 13 / 7 / 13 / 2 | 35, 349,483,190 | 801 / 1705 | EQUAL 8/8 (2 units per frame) |
| `C5H-0301-FOG_0008_20261116-072902_workstation` | 07:29-07:36 | 1577-1662 | 16 / 8 / 15 / 2 | 35, 349,479,423 | 790 / 1577 | EQUAL 8/8 |
| `C5H-0301-SCNB_0009_20261116-073601_workstation` (coast) | 07:36-07:44; camera 07:39:31, span 07:40:45 | 2500-2576 | 12 / 8 / 11 / 2 | 35, 349,479,731 | 731 / 2500 | EQUAL 8/8 |
| `C5H-0301-SCNB_0009_20261116-074400_workstation` (river) | 07:44-07:51; 07:47:27, 07:48:58 | 3083-3170 | 17 / 8 / 17 / 3 | 35, 349,481,315 | 738 / 3083 | EQUAL 8/8 |
| `C5H-0301-SCNB_0009_20261116-075153_workstation` (city) | 07:51-08:00; 07:55:50, 07:57:06 | 2574-2658 | 11 / 8 / 9 / 2 | 35, 349,480,075 | 730 / 2574 | EQUAL 8/8 |
| `C5H-0301-SCNB_0009_20261116-080018_workstation` (highlights) | 08:00-08:08; 08:03:56, 08:05:27 | 3096-3183 | 13 / 8 / 12 / 2 | 35, 349,478,743 | 753 / 3096 | EQUAL 8/8 |
| `C5H-0301-HUGE_0350_20261116-090943_workstation` | 09:09-09:17 (second try; the first, 08:08-08:16, stopped: `unit-server-scene.md` 11.2) | 2756-2856 | 9 / 12 / 4 / 4 | 42, 623,940,562 | 142 / 2756 | **29 units equal in every frame; 6 per frame only in the replay** (below) |

No PATHS (4) or COMBATS (5) record in any of the seven: no unit moved or fought inside an 8-frame span with no AI turn.

**The checks [L, 2026-11-16 09:09-09:23 EST, RTX 4080] (`check.sh`).**
- The checker (`recorder_test.exe --check`) on all fourteen staged bundles: `scenebundle: OK`, rc 0.
- **The standing check on `staging-v4/`** (`C5_SCENE_LIBRARY=F:/c5-64bit-work/scene-library/staging-v4`, this branch's
  sceneshot, tolerance 1:1): `8 passed in 173.78s` (the seven bundles and the missing-bundle negative), 0 skipped
  (`server/evidence/0375/4467/oel_staging_v4.txt`). **And at tolerance 0:0** (`exact.sh`: every bundle played with
  `--dumped --compare --tolerance 0:0`): exit 0, colour 0 px and depth equal in 8 of 8 frames of all seven: byte-equal
  (`exact_staging_v4.txt`).
- **unitfeed's one command on `staging-v4-units/`** (`check.py`: `server/tools/unitfeed/replay.py --bundle B --db <the
  server's units-1.1.db of that run> --install <c5h-a's install> --exe <its CivilizationV_DX11.exe> --mods <the three
  folders the server activated: (1) Community Patch; (2) Vox Populi; (3a) VP - EUI Compatibility Files> --unitanim
  <unitanim.exe>`): exit 0 on all seven: unitfeed OK (8 frames; the Tiny-save scenes 11-17 drains, 138-144 unit events,
  16 units listed, 0 statuses, 0 LOUD; huge 9 drains, 2933 unit events, 280 listed, 853 seeds, `AnimFailed 65` with the
  same two LOUD lines as the live run), `unitscene scene` 8 of 8 read back and written equal, `unitscene prepare` all
  `Ok` (16; huge 280), `unitanim play` Ok (huge: exit 1 UnitsFailed 65, the naval units of the `BadLogicFile` line, which
  replay.py reports and passes); `.c5units` per frame and `timeline.c5anim` written. Then `compare_c5units.py
  <render_test_units_dump> <replay>/scenes --only-common --no-figures`: the six Tiny-save scenes **EQUAL in 8 of 8
  frames**; huge: the 29 units of the server's dump equal in every field in all 8 frames, and **6 units per frame only in
  the replay** (players 4 and 38: (4, 14551), (4, 14965), (4, 15696), (4, 15894), (38, 14657), (38, 17524)). The live
  server's `units 1s` line says `fog_hidden=6`: other teams' units on plots the active team does not see, which 4463's
  fog filter drops in `SceneUnits` (`srv_units_scene.cpp`) at draw time; unitfeed links `UnitsCore` only, so it applies
  no fog filter [L for the count, I for the identity of the six]. A gap of unitfeed, named for lead-0375 (the fog
  member is in the bundle). Figure lines are not compared in any bundle: the controller starts before every library
  span (table).
- **Refusals, read only.** A real v3 bundle (`bundles/C5H-0301-TINY_0009_20261115-225124_workstation`): the checker exit
  26 `scenebundle: REFUSED OlderVersion scene.json: format_version 3 is older than 4 (refused, not read)`, the player exit
  26 `sceneshot: FAILED 26 OlderVersion: the checker refused the bundle`. A units-on bundle by the player: exit 13 (the
  line above). A units-off bundle by unitfeed: exit 1 `unitfeed: REFUSED feed.bin: no UNITS record (the scene's table
  copies are missing: a recording without render_units=1, ...)`.
- `swap_v4.py --dry` (09:23): the seven v3 bundles to `retired/` and the seven of `staging-v4/` into `bundles/`, rc 0;
  with two bundles staged (08:33) it refused, rc 1 (`staging-v4 holds 2 bundles, not 7`).
- The instance: 15 live runs on c5h-a (07:21-09:17), each restored by SHA-256 (225 entries over the 15 runs: 135 equal,
  90 absent as they were before, 0 differing), the save copy removed, `provision_verify_rc` 0, no dialog box; the claim
  released 09:18.

**The swap at 0375's merge (for the MC).** Read only until then. `python -I server/evidence/0375/4467/swap_v4.py --dry`
prints the moves; without `--dry` it moves the seven format-3 bundles of `bundles/` to `retired/` with one
`RETIRED.log` line each (never deleted), moves the seven bundles of `staging-v4/` into `bundles/` and removes the
empty `staging-v4/`; it refuses and moves nothing when a source is not the expected version, a target exists or
`staging-v4/` does not hold exactly seven. `staging-v4-units/` stays where it is. By hand the same is:
```
cd F:/c5-64bit-work/scene-library
for d in bundles/*/; do mv "$d" retired/; echo "$(date -Iseconds) $(basename $d) format_version 3 -> retired/ (task 4467, epic 0375: format version 4 refuses it as older; recorded again with the 0375 recorder, offline-scene-harness.md 21)" >> retired/RETIRED.log; done
mv staging-v4/* bundles/ && rmdir staging-v4
```
After the swap, the standing check on `main` (`test_offline_equals_live.py` with main's sceneshot of the merged code)
must pass 8 of 8 (seven bundles and the missing-bundle negative); `scenerun.py run` makes new baselines (run 1 `new`).

**Not verified (4467).** The staged bundles were not copied to oldpc or run there. `scenerun.py` was not run on the
staging folders (no baselines made; the first run after the swap makes them). The units-on huge failure of 08:14 was
seen once in two runs; its 21 s stall is not profiled. The units' figure lines in a library bundle are compared with
nothing (the controller's history before the span is not in a bundle). Fidelity against the stock shots was not
measured again. *(2026-11-16, issue 0440: the controller's history before the span and the unit database are bundle
members from format version 5, section 22.)*

## 22. Format version 5: the unit scene offline (issue 0440, 2026-11-16)

Written 2026-11-16 by the implementer of issue 0440 (account B, branch `task-0440-sceneshot-units` from `main` 0f4241d4)
on the MC's grant #2934 (queue question #2931): the bundle's member kinds `units_db` and `units_history`, format 4 -> 5,
the recorder writing both or a `gap:` line for each, and the recorder's lines in `srv_render_host.cpp` (shared with epic
0431). Section 21's three gaps (sceneshot did not link the unit scene; the unit database and the controller's state at the
span's first frame were not members) are closed here.

**Why a history and not a state snapshot [C].** The unit scene's state at a frame (`server/src/units/`) is: its table
copies (feed.bin records 3 and 6, every drain since the server's start), the seed allocator (advanced by
`UnitsCore::build` at each built frame), the fades, and 0374's animation controller (`render/src/unitanim/`, every held
unit's clock and machine, stepped once per built frame by the frame clock's step, 1/30 s steps at most 6 per frame).
The controller has no serialisable state (read only here: `server/src/units/` and `render/` are not in 0440's touches), so
the bundle carries its exact input history instead, which the player replays through the scene's own public calls:
`SceneUnits::feed` per drain (already in feed.bin), `poll`, `setPlotStates` (the fog scene's states, as `Scene::draw`),
`beginFrame` and `drawScene` per recorded frame, with the recorded clock and camera. The controller starts in the first
`beginFrame` after its pre-check passed (`UnitsHost::animFrame`), and frames before that change nothing of the scene's
state (`beginFrame` returns at once), so the history starts at that frame.

**Which frame starts the history [C].** The recorder sees the unit scene only through `SceneUnits::ready()` (the
pre-check passed, the drawing renderer open, the table taken, the last frame Ok). The drawing renderer opens in the first
`beginFrame` after the pre-check passed, so `ready()` is false before the controller's first frame F0 and turns true once
F0 drew: the history is the frame before the first frame drawn with `ready()` true, and every frame after it up to the
span. Each record keeps `ready()` before its draw; the player checks its own `ready()` against it before each replayed
frame and before the span's first frame (a difference fails the replay: the scene's state differs). A frame the live
server drew without reaching the unit scene (`Scene::draw` failing before `beginFrame`) would be in the history and
replayed: not seen in the library runs [I].

**The clock [C].** Before 0440 the host passed the recorder's clock to `Scene::draw` for the span's frames only; every
other frame read the clock inside the draw, and the unit scene read its own (`UnitsHost::now()`). From 0440, while the
unit scene is recorded (`SceneRecorder::setUnits`), `beforeDraw` gives the clock for every frame before the span too, the
host draws that frame with it (`SceneFrame::nowMs`), and the history records it: the controller's steps replay exactly.
With no recording (`h.rec` null) the host draws `fr` as before; with a recording of a units-off server the frames outside
the span keep `nowMs` -1 (recorder_test `units_off`).

**Forms [C] (`srv_scene_bundle.h`, the block at `UnitsHistoryRecord`).** `kFormatVersion` 5. `units/gen<the database
generation, 10 digits>.db` (kind `units_db`, form version 1): the SQLite file the unit scene opened (SrvFeed's
`GetUnitDatabase`, or `render_test_units_db`), read whole at the bundle's finish (the generation unchanged since the
span's first frame, else the gap note); refused shorter than 100 bytes (Truncated) or without the `SQLite format 3` header
(BadMagic). `units_history.bin` (kind `units_history`, magic `C5UH`, form version 1): the 16-byte header, u32 recordBytes
744, u32 dbGeneration, u32 flags (bit 0 `ready()` before the span's first frame), u32 reserved 0, then records {i64
frameId, u64 loopIter, f64 nowMs, u32 flags (bit 0 `ready()` before this frame's draw), u32 reserved, VPShellCamera (712
bytes)}. Refused: another record size, a partial record, reserved or flags out of range, no record, a loop iteration or
clock that goes back, a clock not finite, the first record ready, the state after the first record (the second record's
bit, else the head's) not ready (BadRecord). Cross-checks: `render_units` "1" needs both members or the notes
`gap: units: no unit database...` / `gap: units: no controller state...` (MissingMember); either member while
`render_units` is not "1", a second `units_db`, a database generation that is not the history's (BadValue); the
history's last loop iteration after the span's first (BadRecord). A format-4 bundle is refused as OlderVersion
("format_version 4 is older than 5"). At most 100,000 history records (74 MB); a longer history is the gap note.
`render_config` gains `render_units_anim` (with `render_units=1` only: the server's parser refuses it without).

**The recorder [C] (`srv_scene_recorder.{h,cpp}`).** `setUnits({scene, database})` (the host's `StartRecorder`, with the
unit scene; `database` = `render_test_units_db` if given, else `SrvFeed::GetUnitDatabase`); `beforeDraw(..., &frame)` keeps
the history from the controller's first frame; at the span's first frame it notes `ready()` and the database path and
generation; at the finish it writes `units/gen<...>.db` and `units_history.bin`, or `gap: units: no unit database: <why>`
/ `gap: units: no controller state: <why>` (the unit scene not ready at the span, ready before the recorder's first
frame, a history over the limit, no frame blocks). render.log: `recorder: units at the span's start: ready R, history N
frames, database P (generation G)` and one line per member.

**The host [C] (`srv_render_host.cpp`, the recorder's lines only).** `DrawFrame`: `beforeDraw(..., &now, &fr)`; the frame
is drawn as `recFrame` (`fr` with the recorder's clock, -1 when it gave none) whenever a recorder exists, as `fr` without
one. `StartRecorder`: with the unit scene, `setUnits` and `setRenderConfig("render_units_anim", ...)`. **The MC's
condition, by test [run, 2026-11-16 19:53-19:56 EST]:** `server/tests/render/scenebundle/test_host_unchanged.py` builds
nothing itself; with `render_host_test.exe` A (a copy of this branch's tree with main's five server/src/render files) and
B (this branch), no recording key, a static camera over renderfx's synthetic field, each run twice: the host's own dump
(colour BMP and depth) equal in all four runs (colour 43d2e196..., depth e494f95e...): 2 passed
(`server/evidence/0440/host_unchanged.txt`). On build B the host's suites pass as well (2026-11-16 19:56-19:58 EST):
`test_recorder_host.py` 7 passed (recording through the changed lines, fake composite), `test_render_host.py` 47 passed.

**The player [C] (`server/tools/sceneshot/`).** Links the unit scene: `build_scenebundle_tests.py`'s common units gain
`server/src/units/*.cpp` and `models/include`, and every scenebundle test program and sceneshot link `c5r_models.lib`
(`<render build>/src/units/c5r_models.lib`, `models_lib()`). With `render_units=1`: `render_config: units 1 (passes P,
anim A)`; a `gap: units:` note is Unsupported (exit 13, the note named, nothing written); without `render_decals` the
units' mods folder must come from `--mods` (else Unsupported). The unit scene is made as server.exe makes it
(`MakeSceneUnitsWith` with `LiveEnvOpener`) but with its table copies from the file feed (`FeedSourcePort`, feed.bin's
DRAIN `drainMs` and unit copies now passed into the file feed's calls) and its database = the `units_db` member written to
`--out/units-gen<...>.db` (`dbOverride`; SQLite never opens a file inside the bundle). Each drain also goes to
`Scene::unitFeed` (as the host's Loop); a drain whose recorded table copies the scene did not take fails the replay.
Before the span: the history, each frame after the feed up to its loop iteration (`waitPrecheck` before the first),
then the span as before; `units: the controller's history replayed: N frames (F..L) in T ms`. `--set render_units=0`
draws the bundle without units (a diagnostic; `render_units_anim` dropped with it).

**The FOWLevel-3 particles: recorded off, and a gap line when drawn [L, 2026-11-16 18:19-18:28 EST; MC #3003].** The
first units-on Tiny bundle of format 5 (c5h-e, FOWLevel 3 from its GraphicsSettingsDX11.ini, `render_fow_level` 3)
replayed with depth byte-equal in 8 of 8 frames (the units at their places) but colour off in 2.45 M of 3.60 M pixels
(median 8, largest 206), and as much with `--set render_units=0` (2.47 M; depth then off in the 21,698 unit pixels only):
the fog particles of 0340 (`srv_fow_scene.cpp`, on at FOWLevel 3 since 0340 reached `main`), which the player never
draws (`FowScene::setClockState`: no particle state in a bundle, 0403, backlog), over the whole unexplored area. 4467's
library of format 4 was recorded on a server without 0340, so it has no particles. So 0440's library is recorded with the
server's own switch `C5_RENDER_FOW_PARTICLES=0` (TEST ONLY, `server/evidence/0440/live.sh`; render.log `particles off
(C5_RENDER_FOW_PARTICLES=0)`): FOWLevel 3 without particles, the picture of the library it replaces; it is recorded again
when 0403 makes the particles' state a member. The particles-on bundle is kept as evidence in
`F:/c5-64bit-work/0440/particles-on/C5H-0301-TINY_0009_20261116-181942_workstation`. **And from 0440 the recorder writes
`gap: fog particles: the server drew the FOWLevel-3 particles (N particle frames from the span's start, on at its first
frame B); their state is not a bundle member (0403): the player draws none`** whenever the fog scene had them on at the
span's first frame or drew a particle frame after it (`FowScene::particlesOn()`, `particleFrames`), so such a bundle never
reads as complete; the checker accepts it (a gap, not "incomplete"); sceneshot prints `sceneshot: NOTE: the bundle notes
'gap: fog particles...'` before it draws (MC #3003, inside the recorder grant). (Corrected 2026-11-16 by ticket 0403,
section 23: from format version 6 the particles' state is the member `particles.bin` and the library is recorded with
the particles on; the gap line is written only when the particles drew in the span and no state could be taken.)

**The first byte-equal units-on replay, and the history's teeth [L, 2026-11-16, RTX 4080, c5h-e].** Tiny, recorded
18:30-18:37 EST with the first 0440 build (server.exe 886267e2..., particles off): the unit scene's pre-check passed and
the controller started at frame 703; the history is 419 frames (703..1366, loop iterations from 2089), the span 1367..1440,
the database 6,806,528 bytes. sceneshot at tolerance 0:0: the history replayed in 33.6 s, then **colour 0 pixels and depth
equal in 8 of 8 frames**. The same bundle with `--units-no-history` (the controller started at the span's first frame):
20,930-30,213 pixels differ per frame in colour and depth (the units' poses and clip times), so the history is what makes
the units equal. (That bundle was moved to `F:/c5-64bit-work/0440/first-build/`; the library was recorded again with the
final build, below.)

**The units-on library at format 5, staged [L, 2026-11-16 18:48-19:43 EST, RTX 4080].** Build
`server/evidence/0440/build.sh` (18:41-18:47 EST): server.exe eb07c6b8... (`repo_commit=2c1a35abd, repo_dirty=0`, pin
04b1211b8 from a copy of 0325's `src-x64f` and 9045's build_all deps, DLL 74588135...), proxy a32ea852..., the layout =
the pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with that server.exe, composite 291b937b..., recorder_test
154cacf5..., sceneshot f1ab590b.... Drivers `server/evidence/0440/` (`rec_live.py` = 3904's `recorder_live.py` imported
unchanged plus the `scn` save and `render_units=1` in the run's `extra.cfg`; `live.sh` with `C5_RENDER_FOW_PARTICLES=0`;
`series.sh`), one game process per bundle, `--frames 8 --dump all`, `--after 900` (Tiny, fog, huge) or 1500 (the four
scenes, with `tools/scenario/runner.py camera ... scenes_tiny.json`). Instance **c5h-e** (F:, Goldberg), claimed by
impl-B-0440, MC #2549's `provision.py align548 c5h-e` and `verify c5h-e` rc 0 first (18:05-18:06); the Tiny source save
C5H-0167-TINY_0008 (b2ceb663...) copied in from c5h-a for the runs and removed after (c5h-e has the huge one); every run
`scenebundle: OK` by stamp and checker, `provision_verify_rc` 0, no dialog box; `verify c5h-e` rc 0 at the end; the claim
released 19:44.

| Bundle (`F:/c5-64bit-work/scene-library/staging-v5/`) | Span (8 frames, all dumped, 2554 x 1411) | History (frames, first..last, loop iterations) | Members, bytes |
|---|---|---|---|
| `C5H-0301-TINY_0009_20261116-184814_workstation` | 1240.. | 404, 702..1239, 2047..2455 | 44, 513,168,550 |
| `C5H-0301-FOG_0008_20261116-185334_workstation` | 1139.. | 251, 826..1138, 2238..2494 | 44, 513,060,834 |
| `C5H-0301-SCNB_0009_20261116-185927_workstation` (coast) | 2232.. | 1088, 763..2231, 2021..3144 | 37, 357,093,466 |
| `C5H-0301-SCNB_0009_20261116-190651_workstation` (river) | 1850.. | 899, 729..1848, 2254..3235 | 37, 356,951,838 |
| `C5H-0301-SCNB_0009_20261116-191343_workstation` (city) | 1833.. | 1038, 704..1832, 2754..3877 | 37, 357,060,394 |
| `C5H-0301-SCNB_0009_20261116-192059_workstation` (highlights) | 2305.. | 914, 873..2304, 2346..3295 | 37, 356,958,030 |
| `C5H-0301-HUGE_0350_20261116-192756_workstation` | 1001.. | 899, 46..1000, 2566..3554 | 44, 631,201,988 |

Every bundle: format version 5, `render_units` 1, `render_units_anim` 1, `render_fow` 1 at level 3 (particles off by the
switch), `units/gen0000000001.db` of 6,806,528 bytes, `ready()` set at the span, the only version note the bake-cache gap.
Total 3,085,495,100 B (2.9 GiB). The Tiny bundle is 513 MB against 4467's 349 MB: the database (6.8 MB), the history
(0.3 MB), and 8 overlay files of 22.4 MB where 4467's Tiny span had 1 (the EXE's overlay changed in every frame of this
span; the cause not looked at [I]); the fog bundle likewise. The huge bundle's history starts at frame 46 at loop iteration 2566: the frame
ids restarted with a new composite after the load (`composite pid ... frame ids restart`), which the history keeps as
recorded [I for the cause]. Facts: `server/evidence/0440/staged_bundles.txt` (`facts.py`).

**The checks [L, 2026-11-16 19:34-19:43 EST] (`server/evidence/0440/check.sh`, `check_staging_v5.txt`).**
- The checker on all seven: `scenebundle: OK`, rc 0.
- The standing check on `staging-v5/` (`C5_SCENE_LIBRARY=F:/c5-64bit-work/scene-library/staging-v5`, this branch's
  sceneshot, its default tolerance 1:1): `8 passed in 250.61s` (the seven bundles and the missing-bundle negative), 0
  skipped.
- **At tolerance 0:0, each bundle: colour 0 pixels and depth equal in 8 of 8 frames, all seven: byte-equal with the unit
  scene drawing**; the history replayed in 22.6-32.1 s per bundle (251-1088 frames).
- Refusals: the format-4 Tiny bundle of `bundles/` by this branch's checker exit 26 `scenebundle: REFUSED OlderVersion
  scene.json: format_version 4 is older than 5 (refused, not read)`, by the player exit 26 `sceneshot: FAILED 26
  OlderVersion: the checker refused the bundle`, `--out` not created. The staged Tiny bundle with `--set render_units=0`:
  exit 10 Differs, 21,877-21,888 pixels per frame (the units).
- `swap_v5.py --dry` (19:44): the seven format-4 bundles of `bundles/` and 4467's `staging-v4-units/` to `retired/`, the
  seven of `staging-v5/` into `bundles/`, rc 0 (`swap_dry.txt`).

**The swap (for the MC, at 0440's merge).** `python -I server/evidence/0440/swap_v5.py --dry` prints the moves; without
`--dry` it moves every format-4 bundle of `bundles/` to `retired/` (one `RETIRED.log` line each, never deleted), 4467's
`staging-v4-units/` to `retired/staging-v4-units/` (one line), the seven bundles of `staging-v5/` into `bundles/`, and
removes the empty `staging-v5/`; it refuses and moves nothing when a source is not the expected version, a staged bundle is
not units-on with both members, a target exists, or `staging-v5/` does not hold exactly seven. After the swap the
standing check on `main` (main's sceneshot of the merged code) must pass 8 of 8; `scenerun.py run` makes new baselines
(run 1 `new`).

**Not verified (0440).** The library was not copied to oldpc or run there, and `scenerun.py` was not run on it (no
baselines; fidelity against the stock shots not measured again). The FOWLevel-3 particles are off in every bundle (0403).
`render_units_passes=1` (the units in the water's reflection) was not recorded: the replay calls no `drawReflection` in the
history (it holds no state [I]). A frame the live server drew without reaching the unit scene would break the history
[I, not seen]. The suites were run plain only (no ASan run of the changed suites).

**Tests (plain, final run 2026-11-16 19:45-19:53 EST on the branch's last code, `build_scenebundle_tests.py --run`,
C5R_EXE the Ghidra copy, C5_GAME_DIR c5h-b's install read only) [run].** bundle_format_test 714 passed
(TestUnitsMembers: names, paths, the round trip, 15 history refusals in process, two database refusals, three accepted
bundles, eight refusals through the check child, the writer's three path refusals); recorder_test 206 passed (TestUnits
with a fake unit scene: ready from frame 4 -> history 3, 4, 5 with the clocks `beforeDraw` gave and the camera blocks, the
database copied; ready only at the span -> one record; never ready -> the controller-state gap; no database -> its gap;
units not recorded -> no clock outside the span, no member, no note; TestFog: particle frames in the span -> the
`gap: fog particles` line, none -> no line); sceneshot_test 171 passed (WARP and hardware; the format-4 refusal
"format_version 4 is older than 5"; a units-on bundle with the gap notes: checker OK, player exit 13 naming the note,
`--out` not created, and with `--set render_units=0` exit 0); sources_test 24 passed. All four 0 failed, 0 skipped. An
earlier run (17:58-18:20) on the code before MC #3003: 714 / 203 / 163 / 24.

## 23. Format version 6: the FOWLevel-3 fog particles offline (ticket 0403, 2026-11-16)

Written 2026-11-16 by the implementer of ticket 0403 (account B, branch `task-0403-fog-particles-bundle` from `main`
d5c941ce4) on the MC's grant #3092: the bundle member `particles.bin`, format 5 -> 6, the recorder writing it, the player
replaying it with the particles drawn, and (granted beside the touches) `FowScene::particleState` / `setParticleState` in
`server/src/render/srv_fow_scene.{h,cpp}`. `srv_render_host.cpp` is not changed: the recorder already holds the fog scene
(`setFog`, section 2.11) and takes everything at the span's first frame itself.

**Why a state and not a history [C].** Unlike the unit controller (section 22), `c5r::fow::ParticleSim` has a byte-exact
`saveState` / `loadState` (0340, `render/src/fow/particles.cpp`): settings, the flow field's size and hash (the field
itself is the install's), the grid, the generator (`CrtRand`), the lost and step counts, every particle (44 bytes). The
simulation steps once per drawn frame (`ParticleRenderer::frame`: emit, draw, then the step on a worker thread; `sim()`
joins it), so the state before the span's first draw plus the span's frames gives every particle of every frame.

**Forms [C] (`srv_scene_bundle.h`, the block at `FogParticlesFile`).** `kFormatVersion` 6. `particles.bin` (kind
`fog_particles`, magic `C5PS`, form version 1): the 16-byte header, u32 flags (bit 0 `kFogParticlesSpawned`), u32 seed,
f32 worldW, f32 worldH, u32 stateBytes, u32 reserved 0 (40 bytes), then `ParticleSim::saveState()` (`C5FP` version 1).
Not spawned (the particles on but their first frame not drawn yet): no state; the player's first particle frame spawns
with the seed, as the live one did. Refused: a short head (Truncated), flags above bit 0 or reserved not 0, a world not
finite or (spawned) not > 0, spawned without a state or not spawned with one (BadRecord), stateBytes not the rest of the
file (WrongSize); inside the state: not `C5FP` (BadMagic), version not 1 (BadVersion), fields, a cell count or particles
past the end (Truncated), bytes after the last cell (WrongSize), a grid outside 1..4096 a side or a cell above 32
particles (BadRecord). The flow field's hash is the player's check (`loadState` against the install's field: "another
flow field than the install's"). `render_config` gains `render_fow_particles` ("1": the particles were on at the span's
first frame). Cross-checks: the key, when present, "0" or "1" (BadValue, scene.json); "1" needs `particles.bin` or a
note starting `gap: fog particles` (MissingMember); `particles.bin` while the key is not "1" (BadValue). A format-5
bundle is refused as OlderVersion ("format_version 5 is older than 6").

**The fog scene's two calls [C] (`srv_fow_scene.{h,cpp}`, MC #3092).** `particleState(out, why) const`: false when the
particles are not on (`FOWLevel below 3`, the switch, a module that did not start, `the offline player`) or the module
has no simulation; else the seed, whether they are spawned, the world they were spawned for and `saveState()`.
`setParticleState(s, why)` (the player, after `setClockState`): needs the particle module started at init (false with
the reason otherwise: FOWLevel below 3, `C5_RENDER_FOW_PARTICLES=0` in the player's environment, the module's error);
spawned: `resetParticles(worldW, worldH, seed)` then `loadState`, a refusal leaves the particles off; not spawned: the
next particle frame spawns with the seed. The particles are then on again. The one change to the live path: the spawn's
seed is read once at `init` (`m_partSeed = RenderFowParticleSeed()`) instead of at each spawn, so that the recorder can
name it; the environment variable does not change in a run, so the particles are the same (tested against main below).

**The recorder [C] (`srv_scene_recorder.{h,cpp}`).** At the span's first frame, before its draw (beside the fog's
clock): `particleState`; `render_config` `render_fow_particles` = whether the particles were on there; render.log
`recorder: fog particles at the span's start: spawned, seed S, world W x H, state N bytes (F particle frames so far)`.
At the finish `particles.bin`, or, when the particles drew in the span but no state was taken (on without a simulation,
or switched on after the span's first frame), 0440's `gap: fog particles: ...` note with the reason. The particles off
(FOWLevel below 3, the switch): the key "0", no member, no note.

**The player [C] (`server/tools/sceneshot/`).** `render_config: fog particles 1|0` (render_fow and render_fow_particles
"1"); after `fog_clock.bin`, `particles.bin` through `setParticleState`:
`sceneshot: particles.bin in use: spawned, seed 1, world W x H, N particles in a X x Y grid`; a refused state fails the
replay (exit 8 ReplayFailed, nothing written). `--set render_fow_particles=0` draws the bundle without particles (a
diagnostic, `particles.bin not used`); `render_fow_particles=1` with the gap note plays without particles and says so.

**Tests [run, plain, 2026-11-16; C5R_EXE the Ghidra copy, C5_GAME_DIR c5h-b's install read only].**
- `bundle_format_test` (TestParticlesMember): a real `ParticleSim` state (4 x 4 flow field, 2048 x 1024 world, seed 5,
  three steps) round trips and loads back byte-equal; 18 refusals in process; positives (spawned, not spawned, the gap
  note, the key 0 or absent without a member); five refusals through the check child (exit code, line, nothing
  written): no member and no note (MissingMember), the member with the key 0 or absent, the key 2 (BadValue scene.json),
  a state without `C5FP` (BadMagic); the writer refuses another path; format 5 refused as older.
- `recorder_test` (TestFogParticles, stand-in modules with a real simulation): spawned (the state set through
  `setParticleState`) -> `particles.bin` byte-equal to it, key 1, no note; on, not spawned -> not-spawned member with seed
  1; the switch off and FOWLevel 2 -> key 0, no member, no note; on without a simulation -> key 1, no member, the gap
  note naming "no simulation".
- `sceneshot_test` (CaseFogParticles, WARP and the RTX 4080): a live scene at FOWLevel 3 with the real modules spawns the
  particles in three frames before the span and draws them in all six; the player with `particles.bin` draws the three
  dumped frames byte-equal (colour and depth); `--set render_fow_particles=0` differs; no member and no note: the
  checker's MissingMember (exit 28, nothing written); the gap note: plays, says so, differs; a state of another flow
  field: exit 8 "another flow field", nothing written; the format-5 refusal.
- `test_fow_scene` `particle_state`: a live rig's state after 8 frames given to a fresh rig (the player) draws frames
  9-12 byte-equal; without it they differ in all four; a not-spawned state from before frame 1 replays frames 1-4
  byte-equal; refusals (FOWLevel 2, the switch, a damaged magic, a cut state, a world not finite) leave the particles off.
- **The live particles equal main's (MC #3092):** `test_fow_scene` `particles_sequence` (24 frames, seed 7 through the
  run, per frame the SHA-256 of the pixels and of `saveState`) built from this branch and from a copy with main's
  `srv_fow_scene.{h,cpp}` (and main's bundle and recorder files): `test_particles_unchanged.py` [run, 2026-11-16
  21:08-21:08 EST, `server/evidence/0403/main_equal.sh` steps 1-3, main c84de3de1's six files]: **main = branch in 24 of
  24 frames**, each build twice equal, 24 distinct states (frame 0 pixels d5750385..., state 427a0f00...; frame 23
  4abe930f... / 304b549c...), 2 passed. The pictures repeat with the rig's five camera places (5 distinct of 24): the
  particles' moves over 24 frames stay under a pixel of the rig's small target [I]; the state is the strong check.
- **The host unchanged (the ticket's test_host_unchanged):** `render_host_test.exe` from the same copy (A) and this
  branch (B), no recording key, each twice [run, 2026-11-16 21:08-21:09 EST]: colour 43d2e196..., depth e494f95e... in all
  four runs (the hashes 0440 measured), 2 passed. A first run at 21:01 failed in A's first run before any comparison: "the
  render thread did not stop within 5 s", so no totals line (main's code; not seen again) [L, a flake of the test host
  under load, cause not looked at].
- Results, plain [run, 2026-11-16 20:36-20:48 EST, `build_scenebundle_tests.py --run`]: bundle_format_test 786 passed,
  recorder_test 247, sceneshot_test 193, sources_test 24, all 0 failed, 0 skipped (`server/evidence/0403/suite_plain.txt`);
  `test_fow_scene.py` 3 passed (21 cases, `particle_state` and `particles_sequence` among them; `fow_suite_plain.txt`);
  `test_recorder_host.py` 7 passed and `test_render_host.py` 47 passed on build B.
- Under MSVC ASan [run, 2026-11-16 22:20-23:10 EST, `--asan`]: bundle_format_test 786, recorder_test 247, sources_test 24,
  sceneshot_test 193 passed, 0 failed, 0 skipped, no AddressSanitizer report (`suite_asan.txt`). sceneshot_test needs
  about 22 minutes under ASan (peak about 1.3 GB): the default `--timeout 600` of `build_scenebundle_tests.py` stops it
  (`timeout after 600 s`), so it was run alone with `--only sceneshot_test --timeout 3600`. `test_fow_scene.py` under ASan
  (`C5_FOW_SCENE_ASAN=1`): 3 passed in 345 s, no report.

**The library at format 6, the particles on, staged [L, 2026-11-16 21:21-22:07 EST, RTX 4080].** Build
`server/evidence/0403/build.sh` (21:12-21:20 EST): server.exe 5a62a8e7... (`repo_commit=661a51491, repo_dirty=1`: only this
file's text was uncommitted at the build; the code is 661a51491's), pin 04b1211b8 from a copy of 4555's `src-x64f` and
deps (DLL 74588135...), the layout = the pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with that server.exe, proxy
4a6e0664..., composite 55966015..., recorder_test bf76cb53..., sceneshot a005be6d... (`build_hashes.txt`). Drivers
`server/evidence/0403/` (`rec_live.py` = 0440's: 3904's `recorder_live.py` imported unchanged, the `scn` save and
`render_units=1`; `live.sh` without 0440's `C5_RENDER_FOW_PARTICLES=0`: the server's default, the particles on;
`series.sh`), one game process per bundle, `--frames 8 --dump all`, `--after 900` (Tiny, fog, huge) or 1500 (the four
scenes, `tools/scenario/runner.py camera ... scenes_tiny.json`). Instance **c5h-e** (F:, Goldberg, FOWLevel 3 in its
GraphicsSettingsDX11.ini), claimed by impl-B-0403, `verify c5h-e --quick` rc 0 before; the Tiny source save
C5H-0167-TINY_0008 (b2ceb663...) copied in from c5h-a for the runs and removed after; every run `scenebundle: OK` by stamp
and checker, `provision_verify_rc` 0, no dialog box (the watcher's marker after each item); `verify c5h-e` rc 0 at the
end (22:18); the claim released.

| Bundle (`F:/c5-64bit-work/scene-library/staging-v6/`) | Span (8 frames, all dumped, 2554 x 1411) | particles.bin (state at the span) | History (frames, first..last) | Members, bytes |
|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261116-212120_workstation` | 1248.. | 1,160,912 B, 899 steps, 26,080 particles, world 3583 x 2216 | 325, 788..1247 | 45, 514,273,086 |
| `C5H-0301-FOG_0008_20261116-212744_workstation` | 956.. | 1,160,912 B, 899 steps | 336, 598..955 | 45, 514,277,122 |
| `C5H-0301-SCNB_0009_20261116-213248_workstation` (coast) | 1872.. | 1,160,912 B, 1499 steps | 911, 730..1871 | 38, 358,122,162 |
| `C5H-0301-SCNB_0009_20261116-213935_workstation` (river) | 1943.. | 1,160,912 B, 1499 steps | 900, 811..1942 | 38, 358,111,310 |
| `C5H-0301-SCNB_0009_20261116-214657_workstation` (city) | 2501.. | 1,160,912 B, 1499 steps | 1005, 847..2499 | 38, 358,189,206 |
| `C5H-0301-SCNB_0009_20261116-215404_workstation` (highlights) | 1809.. | 1,160,912 B, 1499 steps | 923, 643..1808 | 38, 358,132,722 |
| `C5H-0301-HUGE_0350_20261116-220048_workstation` | 1318.. | 4,497,220 B, 899 steps, 101,083 particles, world 8191 x 4655 | 899, 47..1316 | 45, 635,745,812 |

Every bundle: format version 6, `render_units` 1, `render_fow` 1 at level 3, `render_fow_particles` 1 with
`particles.bin` spawned (seed 1), the only version note the bake-cache gap (no `gap: fog particles`). The step count is
the particle frames the server drew before the span: 899 = `--after 900` - 1, 1499 = 1500 - 1 (one step per drawn frame
since the spawn at the first fog frame) [L]. Tiny's state is 1.16 MB (0340's "about 190 KB on a 1000 x 900 test world"
scales with the world), huge's 4.50 MB (0340 estimated 4.3 MB). Total 3,096,851,420 B (2.9 GiB). Facts:
`server/evidence/0403/staged_bundles.txt` (`facts.py`); logs `series_logs.txt`.

**The checks [L, 2026-11-16 22:07-22:18 EST] (`server/evidence/0403/check.sh`, `check_staging_v6.txt`).**
- The checker on all seven: `scenebundle: OK`, rc 0.
- The standing check on `staging-v6/` (`C5_SCENE_LIBRARY=F:/c5-64bit-work/scene-library/staging-v6`, this branch's
  sceneshot, its default tolerance 1:1): `8 passed in 276.94s`, 0 skipped.
- **At tolerance 0:0, each bundle: colour 0 pixels and depth equal in 8 of 8 frames, all seven: byte-equal with the
  particles drawing** (`particles.bin in use: spawned, seed 1, ...` in each) and the unit scene drawing.
- Refusals: the format-5 Tiny bundle of `bundles/` by this branch's checker exit 26 `scenebundle: REFUSED OlderVersion
  scene.json: format_version 5 is older than 6 (refused, not read)`, by the player exit 26. The staged Tiny bundle with
  `--set render_fow_particles=0`: exit 10 Differs, 2,450,096 of 3,603,694 pixels in frame 1248 (largest 203), as in 0440's
  first units-on run with the particles live and none replayed (2.45 M): the particles are what the member restores.
- `swap_v6.py --dry` (22:18): the seven format-5 bundles of `bundles/` to `retired/`, the seven of `staging-v6/` into
  `bundles/`, rc 0 (`swap_dry.txt`).

**The swap (for the MC, at 0403's merge).** `python -I server/evidence/0403/swap_v6.py --dry` prints the moves; without
`--dry` it moves every format-5 bundle of `bundles/` to `retired/` (one `RETIRED.log` line each, never deleted), the seven
bundles of `staging-v6/` into `bundles/`, and removes the empty `staging-v6/`; it refuses and moves nothing when a source
is not the expected version, a staged bundle is not units-on with both members and particles-on with `particles.bin` (or
carries a particle gap note), a target exists, or `staging-v6/` does not hold exactly seven. After the swap the standing
check on `main` must pass 8 of 8; `scenerun.py run` makes new baselines.

**Not verified (0403).** The library was not copied to oldpc or run there, and `scenerun.py` was not run on it (no
baselines; fidelity against the stock shots not measured again: the particles now drawn change the fog's picture). A
span during which the particles are spawned again (the world box changing inside the span: a new map) would be replayed
by the player's own spawn for the new world [I, not recorded]. `C5_RENDER_FOW_PARTICLE_SEED` other than 1 in a live
recording was not tried (the stand-in tests cover the seed: `particle_state`, recorder_test variant 0 with seed 9).

## 24. Format version 7: the feed's read order, and the library recorded again (ticket 0493, 2026-11-17)

Written 2026-11-17 by the implementer of ticket 0493 (account B, branch `task-0493-scene-library-rerecord` from `main`
62b8dd670).

**The cause [C, code reading; L for the failure, section 16's "P1 fails on main" paragraph].** f916762a5 (4555's
follow-up, 2026-11-16 23:21, MC #3242) made `Scene::fowFeed` (`srv_render_scene.h`) call
`ResidencySource::onFeedGeneration` (`srv_residency_source.cpp`) before the fog's `FowScene::onBatch`. At each new
snapshot generation that the residency has not taken while its art types are still stale, it reads the plot table
through `copyPlots(..., kPlotsChanged)`, which is PLOTS caller 2. The fog then reads the same generation as caller 3.
`FileFeed::copyPlots` (`srv_scene_sources.cpp`) replays the records strictly in the recorded order and checks each
caller. The seven format-6 bundles were recorded 2026-11-16 21:21-22:00, before f916762a5. They hold the fog's caller-3
read where the current server first asks for caller 2, so every replay failed `sceneshot: FAILED 8 ReplayFailed: feed:
out of order: copyPlots (caller 2) called, the next record ... is PLOTS of caller 3`. No file form changed, so nothing
refused these bundles at load.

**Why not serve the added read from the recording [I, from the code].** The ticket allowed it only where it is provably
equal. It is not. Both reads take the live table at the moment of the call (section 0, P2: `CopyPlots` returns the table
at that moment, which can be newer than the batch). The residency's read and the fog's read are two calls, and the
simulation thread can publish a new table between them. Serving caller 2 from the next caller-3 record would hand the
residency a table it may never have seen live. Old bundles also lack the caller-2 reads at the generations where the
fog does not read again. So the library was recorded again.

**Format version 7 [C] (`srv_scene_bundle.{h,cpp}`).** `kFormatVersion` 7. No member changed and no file form
changed. Version 7 marks the read order. `FormatVersionChange(v)` gives one line per version 2..7. The OlderVersion
refusal now names every change between the bundle's version and the reader's:
`scenebundle: REFUSED OlderVersion scene.json: format_version 6 is older than 7 (refused, not read; changed since:
version 7: the feed's read order: the residency reads the plot table through copyPlots (caller 2) at each new snapshot
generation (f916762a5, ticket 4555; Scene::fowFeed), which an older feed.bin does not hold (ticket 0493); record the
bundle again)`. The checker exits 26 and the player exits 26 at load, before any replay. The text before " (refused"
is unchanged, so `test_unitfeed.py`'s match still holds. `FileFeed`'s "out of order" reasons, in `copyPlots` and in
the unit copies, end with a hint: if the server's feed reads changed since the recording, that change must raise
`format_version` and the library must be recorded again.

**The rule (added to rule A, Commands and rules).** A change to the render thread's feed reads raises `kFormatVersion`
with its `FormatVersionChange` line in the same change, and the library is recorded again (rule B). This covers a new
`copyPlots`, `copyUnits`, `copyPlayers` or `drain` call, a call moved or dropped, and another caller id.
`bundle_format_test` fails when a version from 2 to `kFormatVersion` has no line, so a raise without its line is caught.
A read-order change that does NOT raise the version is still caught only at replay ("out of order" with the hint): no
test fingerprints the live read order [I: proposed follow-up below].

**Tests [C].** `bundle_format_test`: every version 2..7 has a line, and 0, 1 and 8 have none. A version-6 manifest is
refused as OlderVersion with exactly the version-7 line and nothing older. A version-1 manifest names versions 2..7 in
order, and version 0 names them too. The case table has a version-6 case, and the newer case is now version 8. Through
the check child, a version-6 bundle exits 26 with the read-order line and nothing is written. `recorder_test`: the
recorder writes format 7. `sceneshot_test`: a version-6 copy of a live bundle is refused at load (exit 26, the
read-order line, no "out of order"). Results, plain [run, 2026-11-17 10:31-10:52 EST, `run_all.py --only
scenebundle-programs --only scenebundle-py`, C5R_EXE and C5R_GAME_DIR of c5h-a's install, read only]:
bundle_format_test 812 passed, recorder_test 247, sceneshot_test 193, sources_test 24, all 0 failed, 0 skipped
(`SUITE scenebundle-programs PASS 4 passed`); `SUITE scenebundle-py PASS 21 passed, 0 failed, 0 skipped`;
`run_all: OK: 2 suite(s) passed (plain)`. The sceneshot change for `render_bake_near` left every sceneshot_test case
byte-equal. The unitfeed merge check (`server/evidence/0493/unitfeed_check.sh`, 10:55-11:00: `build_unitfeed.py`, unitscene and
unitanim, then `test_unitfeed.py` with UNITFEED_BIN, C5_UNITS_DB = the 0327 database copy, C5R_GAME_DIR = c5h-a's
install): `13 passed`; its older-bundle case (format 7 less one) matches the refusal's unchanged prefix. Under MSVC
ASan [run, 2026-11-17 10:58-11:43 EST]: `run_all.py --asan --only scenebundle-programs`. bundle_format_test (812)
and recorder_test (247) passed, 0 failed, 0 skipped. The session's pause around 11:05 interrupted that run during
sceneshot_test. sceneshot_test (193) and sources_test (24) were then run again on the same ASan binaries
(`build_scenebundle_tests.py --asan --run-only --only sceneshot_test --only sources_test --timeout 3600`, 11:14-11:43):
both passed, 0 failed, 0 skipped. There was no AddressSanitizer report.

**The build (product path) [L, 2026-11-17 08:03-08:10 EST].** `server/evidence/0493/build.sh`: `build_server.py --src
<copy of 4662's git archive of the pin 04b1211b8> --deps <copy of 4662's build_all release output of the pin> --config
release --offline` (OK in 155 s; build id `repo_commit=a962dca01, repo_dirty=0, dll_pin=04b1211b8`). The layout is the
pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with this server.exe. Then the composite, `render/build.py --target
c5render`, the checker (`build_scenebundle_tests.py --only recorder_test`) and the player (`build_sceneshot.py
--smoke`). SHA-256: server.exe b2869cb8..., proxy 38be8074..., composite 43dbe5ef..., recorder_test.exe ec80e4f5...,
sceneshot.exe 663646a6.... Scratch: `C:/c5-64bit-work/0493/`.

**A second cause: the trees of epic 0431 [L, 2026-11-17 08:58-09:16 EST; C for the code].** Epic 0431 merged at
2026-11-17 02:35 (e58e1218d). Since then server.exe draws forests and jungles by default: `render_forest` is 1 wherever
the program links `server/src/forest/` (`srv_render_host.cpp`, `g_forest`), and its cut-tap reader is on as well. 0431
added no bundle member, no `render_config` key and no `gap:` note, which rule A requires. `sceneshot` does not link
`server/src/forest/`. So a bundle recorded on the current server replays without any feed error, but it cannot equal
live. The first series below (trees on, now in `F:/c5-64bit-work/scene-library/staging-v7-trees/`) shows this:
- the checker said OK on all seven;
- `run_all.py --only offline-equals-live` gave 1 passed and 7 failed (exit 10 Differs on every frame of every bundle);
- the Tiny-save scenes differed in 396,583-612,207 colour pixels and 151,130-314,668 depth values per frame, and huge in
  about 448,000 colour pixels and 148,000 depth values;
- the live render.log showed `forest: render_forest=1: 180 trees per plot ...` and `forest 1s: state=ACTIVE drawn=29`
  in the span.

Evidence: `server/evidence/0493/check_trees_on.txt` and `facts_trees_on.txt`. **MC decision (2026-11-17, answer to
0493's question):** the drop-in library is recorded with `render_forest=0` (the picture before 0431, as 4467 left the
units off), the trees-on recordings are kept in `staging-v7-trees/`, and the forest member is ticket 0495. The 0493
driver `server/evidence/0493/rec_live.py` appends `render_forest=0` to the run's `extra.cfg` (`C5_0493_FOREST=1` leaves
the trees on).

**A third cause: issue 0371's near pairs [L, 2026-11-17 09:17-09:35 EST; C for the code].** Since ce2e535d3
(2026-11-17 03:55), `render_bake_near` is 1 by default. With it, the terrain draw takes the EXE's near-camera RGBA8
pairs in states 2 and 5. The recorder writes the key into `render_config`, but sceneshot never set
`ResidencyOptions::nearCaches`, so the player drew every tile from its BC1 pair.
- Tiny recorded with the trees off and the near pairs on (`staging-v7-near/C5H-0301-TINY_0009_20261117-091713_workstation`):
  depth equal in every frame, colour different in 121,914-122,126 px per frame (largest channel difference 25, over the
  differing pixels median 2, p95 6). The difference is a strip of tiles along the top of the view (diagnostic picture
  vc-20261117-092954-workstation-9329).
- With the player reading the key (this branch: `o.bake.res.nearCaches = render_bake_near == "1"` and the server's
  `nearOptions.minClass`; the line `sceneshot: render_config: near pairs 0|1`): still 119,776-119,901 px (largest 29).
  The near pool's up-res state at the span's first frame is not in the bundle (the live pool fills over settled frames;
  the server logs `up-res 2 settled / 1 moving`) [I for the cause of the rest].
- Recorded with `render_forest=0` and `render_bake_near=0` (the driver's `C5_0493_NEAR=0`), Tiny replays **byte-equal in
  8 of 8 frames** at tolerance 0:0 (09:35, `logs/exact3.txt`).

**MC decision (2026-11-17, second answer to 0493):** the drop-in library is recorded with `render_forest=0` AND
`render_bake_near=0`; the near-on Tiny is kept in `staging-v7-near/`; ticket 0495 is widened to both default-drawn
features (bundle members, replay, the library recorded again with both on). Until 0495 lands, the library shows the
picture before 0431 and 0371 in these two respects, and the standing check does not cover the trees or the near pairs.
Evidence excerpts: `server/evidence/0493/exact_tiny_near_on_player_off.txt`, `exact_tiny_near_on_player_near.txt`,
`exact_tiny_near_off.txt`.

**The first recordings, trees on (kept in `staging-v7-trees/`, not for `bundles/`) [L, 2026-11-17 08:10-08:56 EST, RTX 4080].** 0403's recipe, unchanged except for the build:
`server/evidence/0493/series.sh c5h-a` runs `live.sh` once per bundle, one game process each. The driver is 0403's
`rec_live.py` (3904's `recorder_live.py`, the `scn` save, `render_units=1`), with the particles at the server's
default (on), `--frames 8 --dump all`, `--after 900` for Tiny, fog and huge and 1500 for the four scenes (camera by
`tools/scenario/runner.py camera ... scenes_tiny.json`, set 146-151 s after the start). Instance **c5h-a** (C:,
Goldberg, FOWLevel 3, Vibrant Textures present; it holds both source saves, so nothing was copied in), claimed by
impl-B-0493 for 0493. On every run: `scenebundle: OK` by stamp and by checker, `provision_verify_rc` 0, and the D155
marker `no box` afterwards. `provision.py verify c5h-a --quick` rc 0 at the end, then the claim was released (08:57).

| Bundle (`F:/c5-64bit-work/scene-library/staging-v7-trees/`, trees on) | Format-6 bundle of the same scene | Span (8 frames, all dumped) | feed.bin: records; PLOTS by caller 1/2/3 | Members, bytes |
|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261117-081040_workstation` | `..._TINY_0009_20261116-212120_...` | 924.. | DRAIN 14, PLOTS 9, UNITS 13, PLAYERS 2; 1/2/6 | 38, 357,748,222 |
| `C5H-0301-FOG_0008_20261117-081531_workstation` | `..._FOG_0008_20261116-212744_...` | 1248.. | 17, 8, 16, 2; 1/1/6 | 38, 357,762,518 |
| `C5H-0301-SCNB_0009_20261117-082101_workstation` (coast) | `..._SCNB_0009_20261116-213248_...` | 1746.. | 17, 8, 16, 2; 1/1/6 | 38, 358,212,014 |
| `C5H-0301-SCNB_0009_20261117-082808_workstation` (river) | `..._213935_...` | 1844.. | 16, 8, 15, 2; 1/1/6 | 38, 358,205,034 |
| `C5H-0301-SCNB_0009_20261117-083518_workstation` (city) | `..._214657_...` | 1823.. | 16, 8, 15, 2; 1/1/6 | 38, 358,228,466 |
| `C5H-0301-SCNB_0009_20261117-084227_workstation` (highlights) | `..._215404_...` | 1840.. | 16, 9, 15, 2; 1/2/6 | 38, 358,241,238 |
| `C5H-0301-HUGE_0350_20261117-084926_workstation` | `..._HUGE_0350_20261116-220048_...` | 1188.. | 9, 13, 4, 4; 1/2/10 | 45, 635,722,276 |

Every bundle: format version 7, `render_units` 1, `render_fow` 1, `render_fow_particles` 1 with `particles.bin`
spawned (seed 1; Tiny-save world 3583 x 2216, huge 8191 x 4655). The only version note is the bake-cache gap. Total
2,784,119,768 B (2.6 GiB). The Tiny-save bundles have 38 members, where the format-6 ones had 45: their overlay did not
change inside the span (1 overlay file, 7 deduplicated). Facts are in `server/evidence/0493/facts_trees_on.txt` (`facts.py`).
**The added read in the records [L]:** in Tiny, highlights and huge the order begins `P3 D P3 D ... P1 D P2 P3`: the
baseline (caller 1) at `onField`, then at the next drain `onFeedGeneration`'s caller 2 before the fog's caller 3. The
format-6 Tiny bundle has one caller-2 record (`onPlotsChanged`) where the format-7 one has two. In fog, coast, river and
city the generation the residency had not taken did not occur in the recorded drains, so they hold a single caller 2
[L for the counts; I for the reason: `onFeedGeneration` reads only while the art types are stale].

**The drop-in library at format 7, trees and near pairs off (`staging-v7/`) [L, 2026-11-17 09:30-10:15 EST, RTX
4080].** The recipe and instance are the same as above (c5h-a, claimed by impl-B-0493 09:15-10:15, released after
`verify c5h-a --quick` rc 0). The driver is `rec_live.py` with `C5_0493_NEAR=0`, so the run's `extra.cfg` carries
`render_units=1`, `render_forest=0` and `render_bake_near=0`. The live render.log says `terrain scene:
render_forest=0: no trees` and `residency: near pairs off`. Tiny was recorded first (`series.sh c5h-a 1 1`, 09:30)
and the other six after it (`series.sh c5h-a 2 7`, 09:36-10:15). On every run: stamp and checker `scenebundle: OK`,
`provision_verify_rc` 0, the D155 marker `no box`. Cameras were set 143.6-146.5 s after the start.

| Bundle (`F:/c5-64bit-work/scene-library/staging-v7/`) | Replaces at the swap (to `retired/`) | Span (8 frames, all dumped) | feed.bin: records; PLOTS by caller 1/2/3 | Members, bytes |
|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261117-093020_workstation` | `..._TINY_0009_20261116-212120_...` | 941.. | DRAIN 15, PLOTS 8, UNITS 14, PLAYERS 2; 1/1/6 | 38, 357,757,750 |
| `C5H-0301-FOG_0008_20261117-093633_workstation` | `..._FOG_0008_20261116-212744_...` | 965.. | 16, 9, 16, 3; 1/2/6 | 38, 357,787,406 |
| `C5H-0301-SCNB_0009_20261117-094134_workstation` (coast) | `..._SCNB_0009_20261116-213248_...` | 1608.. | 15, 8, 14, 2; 1/1/6 | 38, 358,191,886 |
| `C5H-0301-SCNB_0009_20261117-094820_workstation` (river) | `..._213935_...` | 1787.. | 15, 8, 14, 2; 1/1/6 | 38, 358,209,350 |
| `C5H-0301-SCNB_0009_20261117-095520_workstation` (city) | `..._214657_...` | 1611.. | 17, 8, 16, 2; 1/1/6 | 38, 358,200,654 |
| `C5H-0301-SCNB_0009_20261117-100210_workstation` (highlights) | `..._215404_...` | 1778.. | 18, 8, 16, 2; 1/1/6 | 38, 358,246,102 |
| `C5H-0301-HUGE_0350_20261117-100847_workstation` | `..._HUGE_0350_20261116-220048_...` | 980.. | 8, 12, 3, 3; 1/2/9 | 45, 635,745,604 |

Every bundle: format version 7, `render_bake_near` "0", `render_units` 1, `render_fow` 1, `render_fow_particles` 1
with `particles.bin` spawned (seed 1). The only version note is the bake-cache gap. Total 2,784,138,752 B (2.6 GiB).
`render_config` has no key for the trees: the recording's `extra.cfg` is the record that they were off (rule A gap,
ticket 0495). Facts: `server/evidence/0493/facts.txt`.

**The checks on `staging-v7/` [L, 2026-11-17 10:15-10:31 EST, RTX 4080] (`server/evidence/0493/check.sh`,
`check_staging_v7.txt`).**
- The checker on all seven: `scenebundle: OK`, rc 0.
- **The standing check through the runner:** `C5_SCENE_LIBRARY=F:/c5-64bit-work/scene-library/staging-v7 python -I
  server/tests/render/run_all.py --only offline-equals-live` (this branch's sceneshot, default tolerance 1:1):
  `run_all: SUITE offline-equals-live PASS 8 passed, 0 failed, 0 skipped 316 s`, `run_all: OK: 1 suite(s) passed
  (plain)`.
- **At tolerance 0:0**, each bundle played with `--dumped --compare --tolerance 0:0`: exit 0, colour 0 px and depth
  equal in 8 of 8 frames for all seven. All seven are byte-equal, with the particles and the unit scene drawing.
- **The refusal (the ticket's part 2):** the format-6 Tiny bundle of `bundles/`, read only. The checker gives rc 26 and
  the player rc 26, both with the `OlderVersion ... version 7: the feed's read order ...` line quoted above. The player
  writes nothing and never reaches a replay, so there is no "out of order".
- `swap_v7.py --dry` (10:31): the seven format-6 bundles of `bundles/` go to `retired/` and the seven of `staging-v7/`
  go into `bundles/`, rc 0 (`swap_dry.txt`).

**The swap (for the MC, at 0493's merge).** Main's sceneshot reads format 6 until this branch merges, so `bundles/`
was not touched. `python -I server/evidence/0493/swap_v7.py --dry` prints the moves. Without `--dry` it:
- moves every format-6 bundle of `bundles/` to `retired/`, with one `RETIRED.log` line each (never deleted);
- moves the seven bundles of `staging-v7/` into `bundles/`;
- removes the empty `staging-v7/`.

It refuses and moves nothing when:
- a source is not the expected version;
- a staged bundle is not units-on with both members, particles-on with `particles.bin`, and `render_bake_near` "0";
- a target exists;
- `staging-v7/` does not hold exactly seven.

`staging-v7-trees/` (seven, trees and near pairs on) and `staging-v7-near/` (Tiny, trees off and near pairs on) stay
for ticket 0495. After the swap, the standing check on `main` must pass 8 of 8, and `scenerun.py run` makes new
baselines.

**Not verified (0493).**
- The library was not copied to oldpc or run there, and no `scenerun.py` baselines were made.
- Nothing fingerprints the live feed read order. A future read-order change that does not raise `format_version` is
  caught only at replay, by "out of order" with its hint. A test that compares the (type, caller) sequence of a
  deterministic live rig with a golden list tied to `kFormatVersion` is proposed. It would catch f916762a5's kind of
  change only if the rig's residency has a field and a late table.
- Why `onFeedGeneration` read in some recordings and not in others was not traced [I above].
- The sceneshot change for `render_bake_near` is a partial reproduction. A near-on bundle still differs in about
  120,000 px of the Tiny view, which is ticket 0495's to close. (Closed 2026-11-17 by 0495, section 25: `near.bin`.)

## 25. Format version 8: the trees and the near-camera pairs offline (ticket 0495, 2026-11-17)

Written 2026-11-17 by the implementer of ticket 0495 (account B, branch `task-0495-scene-members` from `main` 1db8e2aa3).
The ticket's touches were widened by the MC (#4250) to the forest scene, the scene sources, the recorder, the render
host, the residency (header, source, test), the residency source and the forest placement header; `srv_render_scene.*`
stayed with 0491 and was not needed: every change below goes through the forest scene's interface, a table slot in the
forest header and the residency source.

**What the trees read that a bundle did not hold [C, code reading of `server/src/forest/srv_forest_scene.*` at
1db8e2aa3].**
- **The plot table, from the live feed directly.** `ForestScene::init` made its table with `MakeFeedPlotTable()`
  (`SrvFeed::CopyPlots`), not through the host's `FeedSource`. So its reads were never in `feed.bin`, and in the player
  `SrvFeed::CopyPlots` returns an empty table: no records, no trees. Fixed by the table slot `ForestPlotTableSlot()`
  (`srv_forest_scene.h`): server.exe with `render_test_record` and sceneshot set it before `Scene::init` to
  `MakeFeedSourcePlotTable(feed, kPlotsForest)`, so the forest's reads are `feed.bin` PLOTS of the new caller 4. Without
  a recorder the slot is empty and the forest reads the live feed as before (product behaviour unchanged).
- **The session's random stream.** The seed is `Qpc() ^ pid` per session unless `render_test_forest_seed` is set. It
  picks the cards (the renderer's `randSeed`) and, moved per load, the 20 layouts; each new record takes `rand() % 20`
  from the stream that continues after the layouts. The records' layouts therefore depend on every re-read and
  feature change since the load.
- **The feature ids** of FEATURE_FOREST and FEATURE_JUNGLE, published by the feed's DLL side in a process table
  (`srv_forest_ids.h`). The player has no DLL side.
- **The plots built so far and their order.** A plot's blocks are built at its first visibility. The built plot's
  Tree_Shadow decal is merged into the residency's decal set oldest first (`composeDecals`), and the load's tile marks
  grow with every dropped decal. Both depend on the camera's history, which a bundle holds only for its span.
- **The cut mask** from the cut tap's reader (ticket 4553; the EXE's grid). The player has no EXE pid, so it used the
  empty mask.

**What the near pairs need [C, `render/src/bakeresidency/nearcache.cpp`, `residency.cpp`].** Which tiles hold an RGBA8
pair, of which size, comes from the camera's history and fixed pools: two up-res per settled frame, the farthest first.
Two things kept the player from it: the pool's state at the span's first frame was not in the bundle, and the player's
settle call runs `beforeDraw` many times per frame, so the up-res pass ran many times where the live server ran it once.

**Format version 8 [C] (`srv_scene_bundle.{h,cpp}`, the block at `ForestStateFile`).** `kFormatVersion` 8, its
`FormatVersionChange` line. `feed.bin` PLOTS caller 4 (1..4 accepted by the parser and `MakeFileFeed`). Two members:
- `forest.bin` (kind `forest_state`, magic C5TR, form 1): `srvrender::ForestSpanState` at the span's first frame,
  before its draw: the session and layout seeds, the load count, the feature ids, the layout stream's state, the table's
  generation and size, the records (x, y, kind, layout), the built plots (the decals' order first), the marks, and the
  cut mask sampled per cell at its centre through the reader's own lookup (the reader keeps no image copy;
  `spanState` checks that every cell centre maps back to its cell). Refusals and the 100-byte fixed part: the header
  block.
- `near.bin` (kind `near_pairs`, magic C5NP, form 1): `NearCaches::saveState()` (new, "C5NC" version 1: tile count,
  options, counters, the four free lists in order, the last frame's up-res tiles, every tile not in its initial state),
  taken by the recorder at the same point. `NearCaches::loadState` refuses another tile count or options, a pool entry
  held twice or neither free nor held, a state above 5, a bad size, short or trailing bytes, and leaves the machine as it
  was.
- `render_config` gets `render_forest` (the host's value, then "1"/"0" by whether the scene had a forest at the span).
  Cross-checks: `render_forest` "1" needs `forest.bin` or a note `gap: forest...`; `render_bake_near` "1" with
  `render_tiles` bake needs `near.bin` or `gap: near pairs...`; either member while its key is not "1" is BadValue.
  A cut-mask generation that moves inside the span is a `gap: forest:` note (`forest.bin` holds one mask).

**The player [C] (`server/tools/sceneshot/sceneshot.cpp`, `build_sceneshot.py`).** sceneshot now links
`server/src/forest/*.cpp` (the scenebundle test programs do not: their host keeps `render_forest` 0).
`render_forest` "1": `SceneOptions::forestSeed` = the recorded session seed (the same cards), the forest's table slot =
the file feed's caller 4, the recorded feature ids set after `Scene::init`, `forestFeed` after every replayed drain (as
the host's loop), and before the span's first `Scene::poll` `SceneForest::restoreSpanState`: the replayed table must be
the recorded one (generation and size), the layouts made again if the replay's load count gave another seed, the
records with their recorded layouts, the stream's state, the marks, the recorded cut as a `DenseCut`, and the built
plots built in the recorded order (their decals reach the residency at that poll, before the settle bakes).
`render_bake_near` "1" with `near.bin`: the settle call holds the up-res pass (`Residency::setNearHold`, set by
`ResidencySource::settle`), and after the first frame's settle `ResidencySource::restoreNear` loads the state and bakes
every pair in states 2 and 5 with the tile's current inputs, as an up-res would; each frame's `Scene::draw` then runs
the pass once, as live. Without `near.bin` (a bundle before format 8) the player says so (`NOTE ... not P1`).

**Tests [C].** `bundle_format_test`: kinds and the version-8 line; `forest.bin` round trips (with and without the cut)
and 18 refusals; `near.bin` round trip and 7 refusals; the cross-checks in process and through the check child
(member with its key, gap notes, member missing, member while off, a bad key); PLOTS caller 4 parses and is accepted,
caller 5 refused; every format-7 string moved to 8 and a version-7 bundle refused naming version 8's change.
`residency_test`: `near_state_save_load_round_trip_and_same_next_frames` (byte-equal after load, six more frames take
the same pairs and pool entries) and `near_state_load_refuses_bad_bytes_and_keeps_the_machine`. `recorder_test`,
`sceneshot_test`: format 8.

**The first recording [L, 2026-11-17 12:39-12:45 EST recorded, 12:56 replayed, RTX 4080].** Tiny (`series.sh c5h-d 1 1`,
instance c5h-d on F:, Goldberg, the Tiny source save copied in from c5h-a by SHA-256 b2ceb663...; build 1e7b6fa66). The
recorder said: `forest at the span's start: 180 records, 34 built, 430 marks, cut 1792x1280 generation 4, seeds 93f4fdb3
/ 29061b83 (17.0 ms)`, `near pairs at the span's start: 56x40 tiles, state 76744 bytes`; `forest.bin` 2,300,452 bytes,
`near.bin` 76,776 bytes. The checker refused it: the first form required the marks to be multiples of 64, but
`ForestScene::marks()` holds tile coordinates (mark 0 was (36, 7)). With the form corrected (ffb54304d) the checker said
OK and the player (built from ffb54304d, main a78de4589 merged) replayed it **byte-equal in 8 of 8 frames at tolerance
0:0**, colour and depth, with the trees drawing and 52 near pairs baked from `near.bin`
(`server/evidence/0495/exact_trial_tiny.txt`). The player's own forest read gave 433 marks at its load; the restore
set the recorded 430, so the live marks are not what a fresh load gives, which is why they are recorded. That bundle
was recorded by the build before the merge and is kept outside the library (`F:/c5-64bit-work/0495/trial/`).

**Pairs whose content is behind their tile [L, 2026-11-17 13:36-14:00 EST; C for the cause].** The first series at
ffb54304d (12:55-13:42) gave six bundles byte-equal at 0:0, but huge differed in 72,136 colour pixels per frame (depth
equal, largest channel difference 158; `server/evidence/0495/exact_huge_first_stale_pairs.txt`). The difference is
terrain near the top of the view: live has no city ground or farm decals on some tiles that the replay draws with them.
`near.bin` of that bundle held 40 pairs, and 30 of them (size 512, state 5, request 2) had `bakedGen < gen`: their tile's
content had moved on (the decals arrived after their up-res) and their rebake needs a new 512 texture, but the 512 pool
had 0 free of 64, so the rebake never ran (`upresFailed` 1,738 in 899 frames). Live keeps drawing the old content. The
replay cannot bake inputs it does not have, so `near.bin` form 1 gained the flag `kNearHasImages`: the recorder reads
back the whole chain of every pair for which `NearCaches::contentBehind` is true (`Residency::nearStaleImages`, at the
span's first frame) and the player uploads those texels instead of baking (`Residency::restoreNear`'s images, checked
against the loaded state before it is taken). The six Tiny-save bundles had no such pair (0 of 52-129 held), so their
`near.bin` has no image and is byte-identical in form; only huge was recorded again. [I] Any long session on a big map
reaches this: the 512 pool fills, and every pair whose tile changes afterwards stays stale until it leaves the view or
a texture frees.

**The library at format 8, trees and near pairs on (`staging-v8/`) [L, 2026-11-17 12:55-14:16 EST recorded, RTX 4080].**
`server/evidence/0495/series.sh c5h-d` (0493's recipe: one game process per bundle, 0403's driver unchanged, so every
renderer default of the server is on: units, particles, trees, near pairs). Instance c5h-d (F:, Goldberg, FOWLevel 3,
Vibrant Textures; claimed by impl-B-0495). Six bundles by build ffb54304d (12:55-13:36), huge again by 97bfb4d32
(14:08, the near images; the first huge is kept in `F:/c5-64bit-work/0495/trial/`). Every run: stamp and checker
`scenebundle: OK`, `provision_verify_rc` 0, the D155 marker `no box`; cameras set 147-209 s after the start;
`provision.py verify c5h-d --quick` rc 0 after each series. Facts: `server/evidence/0495/facts.txt`.

| Bundle (`staging-v8/`) | Span | `forest.bin`: records, built, marks, cut | `near.bin`: held pairs, images | PLOTS by caller 1/2/3/4 | Members, bytes |
|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261117-125552_workstation` | 1383.. | 180, 34, 430, 1792x1280 | 52, 0 (76,776 B) | 1/2/6/2 | 47, 516,771,354 |
| `C5H-0301-FOG_0008_20261117-130124_workstation` | 1157.. | 180, 34, 433, 1792x1280 | 52, 0 | 1/2/6/2 | 47, 516,828,598 |
| `C5H-0301-SCNB_0009_20261117-130658_workstation` (coast) | 1641.. | 180, 40, 429 | 86, 0 | 1/2/7/2 | 40, 360,638,138 |
| `C5H-0301-SCNB_0009_20261117-131347_workstation` (river) | 1773.. | 180, 41, 433 | 60, 0 | 1/2/6/2 | 40, 360,611,434 |
| `C5H-0301-SCNB_0009_20261117-132104_workstation` (city) | 2072.. | 180, 50, 435 | 129, 0 | 1/2/7/2 | 40, 360,488,782 |
| `C5H-0301-SCNB_0009_20261117-132847_workstation` (highlights) | 2258.. | 180, 37, 433 | 75, 0 | 1/2/7/2 | 40, 360,653,078 |
| `C5H-0301-HUGE_0350_20261117-140850_workstation` | 1234.. | 690, 2, 1706, 4096x2688 | 40, 60 textures of 30 pairs (84,086,588 B) | 1/2/10/2 | 47, 730,861,172 |

Total 3,206,852,556 B (3.0 GiB). `forest.bin` is 2.3 MB on the Tiny save and 11.0 MB on huge, almost all of it the cut
mask. The forest reads its table twice before the span in every recording (caller 4: its first table, then the
snapshot), so a format-7 `feed.bin` can never replay a format-8 server.

**The checks on `staging-v8/` [L, 2026-11-17 14:17-14:33 EST] (`server/evidence/0495/check.sh`,
`check_staging_v8.txt`).** The checker: `scenebundle: OK` on all seven. The standing check through the runner
(`C5_SCENE_LIBRARY=staging-v8 run_all.py --only offline-equals-live`, this branch's player, tolerance 1:1):
`run_all: SUITE offline-equals-live PASS 8 passed, 0 failed, 0 skipped 266 s`. **At tolerance 0:0 all seven replay
byte-equal in 8 of 8 frames**, colour and depth, with the particles, `forest.bin` and `near.bin` in use (huge with 60
recorded textures). A format-7 bundle of `bundles/` (read only): checker rc 26 and player rc 26 with `format_version 7 is
older than 8 (refused, not read; changed since: version 8: the trees and the near-camera pairs drawn by default: ...)`,
no replay. `swap_v8.py --dry` rc 0 (`swap_dry.txt`).

**The swap (for the MC, at 0495's merge).** `python -I server/evidence/0495/swap_v8.py` moves the seven format-7 bundles
of `bundles/` (0493's library, trees and near pairs off) to `archive-v7-trees-off/` (the archive set; one
`retired/RETIRED.log` line each, never deleted) and the seven of `staging-v8/` into `bundles/`; it refuses and moves
nothing when a source is not the expected version, a staged bundle lacks a member or carries a gap note, a target exists,
or `staging-v8/` does not hold seven. `staging-v7-trees/` and `staging-v7-near/` (0493's format-7 trees-on and near-on
recordings, unreadable at format 8) are left for the MC to retire. After the swap, `scenerun.py run` makes new baselines.

**Test runs [run, 2026-11-17 14:33-15:07 EST, `server/evidence/0495/suites.sh`, C5R_GAME_DIR = c5h-d's install, read
only].** `run_all.py --only render-host --only forest-scene --only scenebundle-programs --only scenebundle-py`: render-host
135 passed, forest-scene 3, scenebundle-programs (bundle_format_test 882, recorder_test 247, sceneshot_test 193,
sources_test 24), scenebundle-py 21, all 0 failed, 0 skipped. `c5r_bakeresidency_test`: 41 passed, 0 skipped. The
format-change merge rule (`unitfeed_check.sh`: `test_unitfeed.py` with UNITFEED_BIN, C5_UNITS_DB = the 0327 database
copy, C5R_GAME_DIR): 13 passed. Under MSVC ASan [run, 15:07-15:40]: `run_all.py --asan --only scenebundle-programs`
(the same four programs and counts, 0 failed, 0 skipped) and `c5r_bakeresidency_test --asan` (41 passed, 0 ASan reports;
the ASan canary is its own target, `--target c5r_asan_canary` first, or render/build.py stops with "ASan canary
missing").

**Not verified (0495).**
- The library was not copied to oldpc or run there, and no `scenerun.py` baselines were made (as 0493).
- A cut-mask change between a plot's build and the span: `forest.bin` restores every built plot with the span's mask,
  where live would rebuild it at its next visibility (its decal may then move in the merge order). Inside the span a
  cut change is a `gap: forest:` note. Neither case occurred in the seven recordings [I].
- A forest record added or changed inside the span takes its layout from the restored stream; no recording had one.
- `restoreNear` with images was exercised by the huge bundle live only (60 textures); no unit test uploads images
  (the form's round trip and refusals are in `bundle_format_test`, `contentBehind` in `residency_test`).
- `near.bin` of a huge map can be large: 84 MB here (60 textures of 1.4 MB); a session that fills both pools could
  record up to the pools' 171 MB.

**Gap note, ticket 4738 (W40, 2026-11-21; the MC's rule A decision #9667): the controller's ECComplete rule.** 4738
changes how the unit animation controller tests `ECComplete` (time in the current state above the member's clip length
for the code, the EXE's rule, `unit-controller.md` 13.5; was the walk of the current graph). No `render_config` key keeps
the old reading (it was wrong) and the library is not re-recorded before the merge. Offline, the controller's output of
nine of the 16 library bundles differs from what their live servers drew [C, `unit-combat-server.md` 18.12, rule A
section]: `C5H-4980-C1MOUNTAIN`, `-C2ANATURAL`, `-C2BWONDER`, `-C3AGARRISON`, `-C3BGARRISON2`, `-C4PORT`, `-C6CARRIER`,
`-C7INTERCEPT` (14 units play the setup Fortify clip one frame longer, at span frame 5, before the fortify loop; barbarian
63/1038 figure 7 swaps idle one frame later, at frame 7) and `C5H-0301-FOG_0008` (63/1038 figure 7 at frame 7). The
other seven (HUGE_0350, the four SCNB_0009, TINY_0009, 4980 C5AIR) are equal. A comparison of these nine against their
live dumps may differ at those frames, for this reason only; the nine are recorded again in the next recording session,
together with 0615's fresh bundles. The rule A run on 4738's merged head (`w40-4738` fa6c69901, `run_all.py` plain,
2026-11-21 18:37:11-19:26:47 EST, W43's nine suites) failed NOTHING: render-host 164, fow-scene 3, overlay-scene 3,
shadow-ice-scene 6, water-scene 29, forest-scene 3, scenebundle-programs 4, scenebundle-py 22 and offline-equals-live 17
passed (every bundle, all 8 dumped frames each, tolerance 1:1, depth equal), so no baseline changes. The nine bundles'
controller differences are in figures that the dumped pictures do not show within the tolerance `[I: culled or below one
pixel; not separated]`; the re-record still follows, so that the bundles' own controller record matches the rule.

## 26. The scenerun baselines of the format-8 library after 0502's swap (ticket 0514, 2026-11-17)

Written 2026-11-17 by the implementer of ticket 0514 (account B, branch `task-0514-scenerun-baselines` from `main`
1d502008e). Evidence: `server/evidence/0514/` (`build.sh`, `run.sh`, `inventory.py`, `driver.py`, `summary.py`,
`acceptance.txt`).

**The library before [C, 2026-11-17 20:16 EST, `inventory.py`].** `bundles/` holds seven bundles, all `format_version` 8:
0495's six Tiny-save bundles (`TINY_0009_20261117-125552`, `FOG_0008_20261117-130124`, the four `SCNB_0009_20261117-*`)
and 0502's huge `C5H-0301-HUGE_0350_20261117-195457_workstation` (swapped in at 20:10 EST; the one before it is in
`archive-0502-seeds-before/`). The baseline set `baselines/workstation__nvidia-geforce-rtx-4080/` held 21 baselines, every
one of a retired bundle (0353's, 0364's, 4467's and 0440's libraries of 2026-11-15/16); none of the seven had one, because
neither 0493 nor 0495 nor 0502 ran `scenerun.py run` after its swap. A baseline is keyed by the bundle id, which carries
the recording time, so a bundle recorded again always starts without one.

**The readers [run, 2026-11-17 20:17-20:20 EST, `build.sh`].** Built from this branch at a commit that was main 1d502008e
plus the evidence scripts only (7ee0c6fa4, squashed away before the hand-back; no source file differs from main): the harness's native tools, c5render, `recorder_test` (e730a225...) and `sceneshot`
(695db62e..., smoke OK). GPU (D175; `driver.py`, the metrics ledger's reader `perf/ledger/record.py machine_facts`):
NVIDIA GeForce RTX 4080, **driver 32.0.16.1088**, the same driver as the 2026-11-15 set of section 18.

**The baseline set `0514` [L, 2026-11-17 20:20-20:33 EST, `run.sh`, workstation, `--tolerance 1:1`].** `scenerun.py
index --out F:/c5-64bit-work/0514/index.json` (to the ticket's folder, not the library: the library keeps no
`index.json`, so a later swap needs no index change) and `verify --index`: `scenerun: OK 7 bundle(s) verified`. Then two
`scenerun.py run --index <that file>` passes through `quietlock.py run`:

| Pass | Run id | Exit line | Regression | P1 |
|---|---|---|---|---|
| 1 (20:20:54-20:26:36) | `20261117-202100_workstation` | `scenerun: OK 7 bundle(s), 56 shot(s), 56 picture(s) recorded` | `new` 56 (each becomes the baseline) | within 56; 0 colour pixels differ in every shot |
| 2 (20:26:36-20:32:53, `--no-baseline-write`) | `20261117-202645_workstation` | the same | **`equal` 56 of 56** (colour and depth hashes) | within 56; 0 pixels differ |

So every bundle, the re-recorded huge one included, replays byte-equal to its own live dumps with main's player, and
replays to the same bytes twice. The set `F:/c5-64bit-work/scene-library/baselines/workstation__nvidia-geforce-rtx-4080/`
now holds **28 baselines: the 7 of `bundles/` (8 shots each, player 695db62e) and the 21 of retired bundles**, kept
beside them (keyed by bundle id they never collide; not moved, not deleted). A new file `BASELINES.log` in the set
holds one dated line per baseline making (this one: the 7 bundles, the index hash 5336728e..., the run, the player and
the driver). A later `scenerun.py run` on the workstation compares against these. Times are of a busy machine
(`quietlock.py check --quiet` exit 1 before each pass): the player took 30-35 s per Tiny-save bundle and 53 s for huge.
Visual Checks: 112 DIAGNOSTIC entries for issue 0514, none undescribed. Evidence: `server/evidence/0514/acceptance.txt`,
`logs/` (both reports, the inventories, `BASELINES.log`).

**Fidelity [L, both passes]:** coast (`coast_c1`), river (`river_c1`) and highlights (`highlights_c1`) still match their
stock cameras, with the same numbers in both passes (compared region median / p95 / max: coast 7 / 56 / 222, 20.3 %;
river 8 / 63 / 236, 33.6 %; highlights 5 / 59 / 224, 26.0 %). **The format-8 city bundle
`C5H-0301-SCNB_0009_20261117-132104_workstation` (0495's recording) no longer matches `city_c1`:** its view translation
is [-1487.079, 198.596, -1103.252], the stock camera's [-1503.939, 147.963, -1052.616], about 51 world units apart (the
match allows 0.5; 0353's city bundle was 0.020-0.081 off). So it has no fidelity reference: its camera was not set to
`city_c1` in 0495's series [I: 0495's live logs are gone, the cause is not established]. Tiny, fog and huge have no stock
camera, as before.

**Not verified (0514).**
- No baselines on oldpc (`E:/c5-testbed/scene-library/baselines/`): the library was not copied there since 0353.
- The city bundle's camera: why 0495's recording missed `city_c1` was not looked into (a recording again with the stock
  camera would give it back its fidelity reference). (2026-11-17 21:10, ticket 0516: recorded again at city_c1, staged for
  the MC's swap, and the likely cause [I]; section 27.)

## 27. The city bundle recorded again at stock's city_c1 (ticket 0516, 2026-11-17)

Written 2026-11-17 by the implementer of ticket 0516 (account B, branch `task-0516-city-bundle-stock-camera` from `main`
0d8bdee1d). Evidence: `server/evidence/0516/` (`build.sh`, `record_city.sh`, `camcheck.py`, `check.sh`, `summary.py`,
`swap_0516.py`, `acceptance.txt`).

**Why [C, 2026-11-17 20:48 EST, `camcheck.py`].** Section 26 found the format-8 city bundle
`C5H-0301-SCNB_0009_20261117-132104_workstation` without a stock reference. `camcheck.py` reads a bundle's `cameras.bin`
with scenerun's own reader and matcher (`parse_cam_blocks`, `stock_cameras`, `match_stock`): the 8 blocks (frames
2072..2135) are one camera at view translation [-1487.079, 198.596, -1103.252], city_c1's is [-1503.939, 147.963,
-1052.616], largest difference 50.636 world units, no match (the matcher allows 0.5). The rotation rows equal the stock
camera's, so the old span shows the same tilt from another place and zoom. The coast bundle `..130658` gives 0.000 against
coast_c1 with the same tool. `camcheck.py` is the cheap first check of any recording that must sit at a stock camera.

**Build [run, 2026-11-17 20:46-20:51 EST, `build.sh all`].** 0502's product path on the current pins (COMMON 18: x64
4f4daac70, layout `x64-4f4daac7-pf8e9f42`): the composite, server.exe and proxy from a git archive of this branch
(63fbb2c2e, main plus evidence), the pin's fork source by git archive, a copy of a release build_all of the pin (from
0502's folder, read only), the pin layout's bin with this server.exe (f9363e10...), proxy (5b0958ef...) and composite
(a8301678...). Readers from the same branch: c5render, `recorder_test` (30d2d7e2...), `sceneshot` (733cd3ed..., smoke OK).

**The recording [L, 2026-11-17 20:52-20:58 EST, `record_city.sh c5h-d`, RTX 4080].** Instance c5h-d (F:, Goldberg,
FOWLevel 3, Vibrant Textures; claimed by impl-B-0516), 0403's driver `rec_live.py` unchanged (the `scn` save = the copy
C5H-0301-SCNB_0009 of c5h-d's C5H-0167-TINY_0008, `render_units=1`; every renderer default on), `--after 1500 --frames 8
--dump all`, settle 200 s; beside it `runner.py camera scenes_tiny.json --frame city_c1`. Bundle
**`C5H-0301-SCNB_0009_20261117-205203_workstation`** in `F:/c5-64bit-work/scene-library/staging-0516/`: format 8, 40
members, 360,672,674 B of members (360,682,507 B on disk), the one note `gap: bake-cache tap left out` as every bundle;
pins in scene.json: x64 DLL 2d737bd5... (4f4daac70), i0 39f17f754, server f9363e10..., proxy 5b0958ef..., composite
a8301678..., EXE 04cfdc71.... Timeline (render log and the runner's `city_c1.camera_only.json`): launched 20:52:27, server
recorder armed 20:53:20, in game 20:54:03 (114 s), **camera set 20:54:08** (wheel 8 in, 3 out, zoom level 17.0, settled
0.73 s, interface hidden, no popup before or after), **span 20:54:48-20:54:50**, frames 1526..1579 (published 1499), all 8
dumped (2554 x 1411). Recorder: units ready (history 1152 frames, database 6,806,528 B), fog particles spawned (state
1,160,872 B), forest 180 records / 47 built / 433 marks, near pairs 56x40 tiles (75,888 B, 0 images), decal set generation
5 (314 decals). Stamp and checker `scenebundle: OK`, `provision_verify_rc` 0, D155 marker `no box`, restore by hash rc 0.
**Camera [C, `camcheck.py`]: one camera in all 8 blocks at [-1503.939, 147.976, -1052.629], 0.013 world units from
city_c1: scenerun's match `city_c1 approximate (0.0129)`** (approximate because above 0.01).

**Why 0495 missed city_c1 [I].** Not the load's own camera: the Tiny and fog bundles (no camera set) sit at [-1472.41,
181.60, -781.60], 271 world units from city_c1, and the old city span at [-1487.08, 198.60, -1103.25]. That span has
city_c1's rotation and differs from it by +16.86 in x and by 50.6 along (0, 1, -1), a zoom-like step, so the likeliest
reading is that 0495's span (frames 2072..2135) was drawn while `runner.py camera` was still moving the camera (its
wheel ticks, 1.5 s apart, and the settle): 0495's cameras were set 147-209 s after the start, and `--after 1500` has
no tie to the camera. Here the span came 40 s after the camera had settled. 0495's camera logs are gone, so the cause is
not established. A recording that must sit at a stock camera should be checked with `camcheck.py` before its checks;
[I] a recorder key that starts the span on a marker the camera runner writes would remove the race.

**The checks [L, 2026-11-17 20:58-21:03 EST, `check.sh`, workstation; logs in `server/evidence/0516/logs/`].**
- Camera: `camcheck: OK 1 camera(s), 8 block(s)` (0.013 world units from city_c1).
- Checker (`recorder_test --check`): `scenebundle: OK`.
- **Offline equals live at tolerance 0:0:** `sceneshot --dumped --compare --tolerance 0:0`, **8 of 8 frames byte-equal**
  (colour 0 pixels, depth equal), with `particles.bin` (26,080 particles), `forest.bin` (180 / 47 / 433) and `near.bin`
  (159 pairs, all baked, 0 recorded textures) in use.
- The standing check through the runner (`C5_SCENE_LIBRARY=staging-0516 run_all.py --only offline-equals-live`, its
  tolerance 1:1): `run_all: SUITE offline-equals-live PASS 2 passed, 0 failed, 0 skipped 24 s`.
- scenerun on a scratch library `F:/c5-64bit-work/0516/lib` (bundles/ = a copy of the staged bundle, stock/city = the
  library's city_c1 camera, picture and state; `verify`: every member equal), `--baseline` = the library's set
  `baselines/workstation__nvidia-geforce-rtx-4080/` (keyed by bundle id: the 28 there untouched): pass 1 (run
  `20261117-210131_workstation`) `new` 8, P1 within 8 with 0 colour pixels differing; **pass 2** (run
  `20261117-210223_workstation`, `--no-baseline-write`) **`equal` 8 of 8**, P1 within 8, 0 pixels. Each pass's exit line:
  `scenerun: OK 1 bundle(s), 8 shot(s), 8 picture(s) recorded`. Player 28 s and 26 s (busy machine: `quiet check exit
  1`). The baseline set's `BASELINES.log` has one line for it. Visual Checks: 16 DIAGNOSTIC entries for issue 0516, none
  undescribed.

**Fidelity against stock's city_c1 (DIAGNOSTIC) [L, both passes, the same numbers].** Reference
`stock/city/city_c1.png`, `approximate` (view translation 0.012878 world units apart; stock shot and bundle both at turn
9). Compared region (server drawn, not stock-dark, not cloud-like): **16.039 % of the frame, median 2, p95 55, max
228-234, 57.73-57.89 % of its pixels within 4**; whole frame median 11, p95 34. Beside the format-8 library's other stock
cameras in section 26 (coast 7 / 56 / 222, river 8 / 63 / 236, highlights 5 / 59 / 224) the city has the lowest median
and the highest share within 4; against 3906's run of the version-1 city bundle (section 17 table: 8 / 100 / 246, 35.2 % within 4)
the p95 fell from 100 to 55. These are COMMON 25's numbers for the library's city scene, not an acceptance.

**The swap (for the MC, at 0516's merge).** `python -I server/evidence/0516/swap_0516.py --dry` prints the moves (rc 0 at
21:03, `swap_dry.txt`). Without `--dry` it moves `bundles/C5H-0301-SCNB_0009_20261117-132104_workstation` to
`archive-0516-city-camera-before/` (kept, never deleted; one `retired/RETIRED.log` line) and the one bundle of
`staging-0516/` into `bundles/`, then removes the empty `staging-0516/`. It refuses and moves nothing when the old city
bundle is missing or not format 8, the staged one is not a format-8 SCNB_0009 bundle with units, particles, trees and near
pairs on and no gap note, a target exists, or `staging-0516/` does not hold exactly one bundle (rc 1, checked with the
folder absent at 20:49). The new bundle's scenerun baseline is already in the set, so after the swap `scenerun.py run`
compares all seven bundles against baselines with no `new` shot (an index written by 0514 to its own folder lists the old
city bundle; make a new one).

**Not verified (0516).**
- The cause of 0495's miss (above, [I]).
- The new city bundle was recorded on the current pins (x64 4f4daac70, server built at 63fbb2c2e); the other five
  Tiny-save bundles of the library were recorded by 0495's build on 04b1211b8 and huge by 0502's on 4f4daac70. Each
  bundle carries its pins in scene.json and replays byte-equal to its own dumps, so the mix does not affect P1 or the
  regression; a fidelity comparison across bundles of different pins is not like with like [I].
- No baseline on oldpc (as 0514).

## 28. The server's ranged-attack arc and the bundle (ticket 0199 part 2, 2026-11-17): the gap note

Written 2026-11-17 21:38 EST by the 0199 part-2 implementer (branch `task-0199-ranged-arc-p2`; design
`research/05-graphics-composition/arrows-and-path-line.md` 13). Rule A applies to a draw the server makes **by default**;
the arc is behind `VP64Options.ini [Renderer] ServerArc` (server.cfg `render_arc`), **default 0**, so this is its gap
note, and the library is not recorded again for it.

- **Its inputs are in members a bundle already has [C, code]:** the feed's `kUiMirror` kind-1 records (SpawnArrowEvent
  with x0, y0, x1, y1 from the proxy's wrapper; only a session with the proxy's arrows install makes them) and kind-2
  records (RemoveAllArrowsEvent), and `kLoadBegin`, all inside `feed.bin` DRAIN records (the recorder writes every event of
  a drained batch); the frame clock (`frames.bin` nowMs); the camera block; the height field. The player gives the
  install (`ArrowSettings.xml` through the scene's shared index) and the EXE (ArrowShader) as it does for the water.
- **What 0199 added for the replay [C, code; not run on a bundle]:** the host writes `render_config` `render_arc` (0 or
  1) into every bundle; `sceneshot` reads it into `SceneOptions::arc` and calls `Scene::arcFeed` in its replay loop at
  the host's place (after `forestFeed`). `arcFeed` reads only the batch it is handed: no table read, no new caller id, no
  change of the feed's read order, so `kFormatVersion` stays 8 (section 24's rule is not touched). A bundle without the
  key plays with the arc off, which is what its server drew.
- **The gap (while the default is 0):** no bundle of the library is recorded with `ServerArc = 1`, so none has kind-1
  records with end points and none replays an arc; a sceneshot built before 0199 ignores the key and draws no arc for a
  bundle that has it [I: it reads only the keys it knows; not run]. Not verified: a bundle recorded live with
  `ServerArc = 1` and replayed by `sceneshot` to the live frame (P1). What closes it when the default becomes 1: raise
  the format to 9 with a `FormatVersionChange` line naming `render_arc` (so an older player refuses such a bundle), record
  the library again with the option on (each recording with an aimed ranged attack for at least one bundle), swap it by
  a swap script as 0495 did, and run `test_unitfeed`.

## 29. The render test builders after ticket 0300 (ticket 0536, 2026-11-18)

Written 2026-11-18 by the 0536 implementer (branch `task-0536-render-test-builders`, from main 8f6fce976).

- **Cause [C, build logs]:** since ticket 0300 (merge 302b9e3ac) `server/src/render/srv_render_scene.h` includes
  `c5r/pathmodels/pathmodels.h`, which includes `c5models/anim.h` (in `models/include`), and pathmodels' code inside
  `c5render.lib` calls into `c5r_models.lib` (`<c5render build>/src/units/c5r_models.lib`, laid out by `render/build.py`
  beside `c5render.lib`). A builder that compiles `server/src/render/*.cpp` (or any unit that includes
  `srv_render_scene.h`) needs `models/include` on its include list, and one that links `c5render.lib` with pathmodels'
  callers needs `c5r_models.lib` too. Without the include the build stops at `pathmodels.h(31): fatal error: 'c5models/anim.h'
  file not found` (clang-cl) or `fatal error C1083: Cannot open include file: 'c5models/anim.h'` (MSVC ASan).
- **The fix, everywhere it was missing:** `models/include` on the include list and `c5r_models.lib` after `c5render.lib`
  on the link line (with a "missing" check like the one for `c5render.lib`): `fow/test_fow_build.py` (0424, 99e46d88d),
  `build_render_tests.py` (MC, c003aee5d), and in 0536 `overlay/test_scene_build.py`, `shadow/build_scene_shadow.py` and
  `forest/test_forest_build.py` (include only: it linked `c5r_models.lib` already for the forest's `_2` cards). The other
  builders compile only tap or reader units that do not include `srv_render_scene.h` and built unchanged.
- **The fow-scene test list:** `fow/test_fow_scene.py` `CASES` now names 0424's four held-frame cases
  (`held_frame_prepares`, `held_frame_off`, `held_frame_waits_for_styles`, `held_frame_stale_dropped`), so
  `test_fow_scene_every_case_is_listed_here` agrees with the program's case table again.
- **Results [C, run, workstation, 2026-11-18 07:06-09:34 EST]** (`run_all.py --out F:/c5-64bit-work/0536/<run> --who 0536
  [--asan]`, C5R_EXE and C5R_GAME_DIR = instance c5h-a's install (VT present); scene library the default):

  | Builder (run_all build) | Before, main 8f6fce976, plain / ASan | After, branch, plain / ASan |
  |---|---|---|
  | render/build.py c5render | OK / OK | OK / OK |
  | build_render_tests.py (rt) | OK / OK | not touched (fixed by c003aee5d) |
  | cut_tap, decal, ice --setmesh, overlay reader, shadow reader, water | OK / OK | not touched |
  | fow/test_fow_build.py | OK / OK (fixed by 0424) | OK / OK |
  | overlay/test_scene_build.py (ovl) | FAILED / FAILED (`c5models/anim.h`) | OK / OK |
  | shadow/build_scene_shadow.py (shadow) | FAILED / FAILED (`c5models/anim.h`) | OK / OK |
  | forest/test_forest_build.py (forest) | FAILED / FAILED (`c5models/anim.h`) | OK / OK |
  | scenebundle/build_scenebundle_tests.py (sb), sceneshot --smoke | OK / OK | not touched (epic 0528's) |

  Suites before (main): render-host 143, cut-tap 1, decal-tap 13, ice-reader 11, overlay-reader 36, shadow-reader 40,
  water-scene 27 passed, plain and ASan; fow-scene 2 passed 1 failed plain and ASan
  (`test_fow_scene_every_case_is_listed_here`: `At index 3 diff: 'held_frame_off' != 'in_play_fault_never_fatal'`);
  overlay-scene, shadow-ice-scene, forest-scene not run (build failed). Scene-bundle suites (epic 0528's, not edited):
  plain scenebundle-programs 4, scenebundle-py 21, offline-equals-live 8 passed; ASan scenebundle-py 21 passed,
  offline-equals-live 8 passed (4446 s under ASan and load), and scenebundle-programs FAIL: bundle_format_test 882, recorder_test 247, sources_test 24 passed, but
  `FAIL sceneshot_test.exe: timeout after 1500 s` (its TIMEOUTS floor in `build_scenebundle_tests.py`). Run again alone on
  the same ASan binaries (`build_scenebundle_tests.py --out <before-asan>/sb --run-only --asan --only sceneshot_test
  --timeout 3600`, 08:34:52-09:05:36): `RESULT: 203 passed, 0 failed, 0 skipped`, no ASan report, in **1844 s**, more than
  the 1500 s floor. The machine was loaded both times (six game instances of two other owners, `quietlock.py check`
  08:20, and this ticket's ASan offline-equals-live beside the rerun); 0364 measured about 16 min unloaded (section 19).
  So the timeout is the floor being too short for ASan under load [I: load as the cause; not measured on a quiet
  machine], not a fault of the program; it is on main and in epic 0528's files, reported, not changed here. Plain `run_all: FAILED: 4 of 14
  suite(s) (plain): fow-scene, overlay-scene, shadow-ice-scene, forest-scene`; ASan `run_all: FAILED: 5 of 14 suite(s)
  (asan): fow-scene, overlay-scene, shadow-ice-scene, forest-scene, scenebundle-programs`.
  After (branch, `--only` the four suites whose builder or list changed; the others' sources and builders are unchanged):
  fow-scene 3, overlay-scene 3, shadow-ice-scene 6, forest-scene 3 passed, plain (`run_all: OK: 3 suite(s) passed
  (plain)` and `OK: 1 suite(s)` for fow-scene) and ASan (the same, `(asan)`; no ASan report file). So every suite of
  run_all.py passes plain and ASan with this branch except ASan scenebundle-programs, which fails on main on
  sceneshot_test's 1500 s timeout under load (above; its programs pass with a longer timeout). Not run: unchanged-vs-main
  (needs `--main`), and no full run_all.py pass of the branch in one go. Every run's lines: `render/evidence/0536/acceptance.txt`.

## 30. The server's farm fields and the bundle (ticket 4852, epic 0528, 2026-11-18): two render_config keys, format 8 unchanged

**No member.** The server's farm producer (`server/src/render/srv_farmfields.{h,cpp}`, `render_farmfields=1`;
`research/05-graphics-composition/farm-fields-layout.md` 13) records its state in `render_config`, not in a member: every
reader refuses an unknown member kind (section 2, "refuse, never skip"), so a member would have meant format 9 and a
re-recorded library, while no reader refuses a `render_config` key it does not know. The format stays 8; no bundle is
recorded again. The keys:

- `render_farmfields`: "0" or "1". The host writes it into every bundle; the recorder writes it again at the span's first
  frame ("1" when the scene has a producer). Absent in every bundle before 4852: the player draws the decal tap's farm
  fields as they are.
- `render_farmfields_state` (with "1"): the input of the farm decals in force at the span's first frame, one line,
  `c5r::farmfields::spanText`: `fv1 seed=<u32> farm=<id> wheat=<id> map=<W>,<H> world=<4 C99 hex floats>
  farms=<x>.<y>.<state>.<art>.<wheat>,...` (5,282 bytes for the huge save's 425 farms). `farm_types.xml` is not in it: the
  player reads it from its install, as live. The layout is deterministic from this input, so neither the graph nor the
  decals are stored.
- `render_farmfields_keep_tap`: "1" only when the live side kept the tap's own farm records.

**The recorder** (`SceneRecorder::takeFarm`) writes a note starting `gap: farm fields` instead of the state when the
producer had no production in force at the span's first frame, and another such note when a production was put in force
inside the span (the line holds the first frame's input). **The checker** (`CheckBundle`) refuses `render_farmfields`
other than 0 / 1, a state without "1" and a state not starting `fv1 ` (BadValue), and "1" with neither the state nor a
`gap: farm fields` note (MissingMember). **The player** (`sceneshot`) sets `SceneOptions::farmfields`, `farmKeepTap` and
`farmThreaded = false` (every production inside `poll()`, before the frame that needs it) from the keys and calls
`FarmFieldsProducer::restoreSpanInput` after `Scene::init` (the recorded input stands: no plot-table reads, the recorded
seed, ids and world); a bundle with "1" and only the note is refused as Unsupported ("record it again"). Tests:
`bundle_format_test` `TestFarmConfig` (the checker); `server/tests/render/farmfields/test_farmfields_producer.cpp`
`bundle_playback_equals_recorded` (a bundle written to disk, checked, its manifest read back and its state played back
by the player's rules: the same records, count and FNV-1a 64 hash, as recorded) and `older_player_loads_farm_bundle` (a
bundle with the farm keys and a key no reader knows passes CheckBundle and reads back; a scene without a producer hands
on the tap's set). Not verified live yet: a span recorded with `render_farmfields=1` in a game (0528 D).

## 31. Format version 9: the library recorded again with the cities and landmarks on (ticket 4883, epic 0561, 2026-11-19)

Written 2026-11-19 by worker-4883 (account B, branch `task-4883-library-format-9` from the team branch
`epic-0561-cities-and-landmarks` at ca5a6ee7c). Evidence: `render/evidence/0561/4883/` (`build_4883.sh`, `rec_4883.py`,
`live_4883.sh`, `series_4883.sh`, `copy_f.sh`, `check_4883.sh`, `facts_4883.py`, `flat_f.py`, `mods_list.py`,
`swap_4883.py`, `acceptance.txt`). Marks: [C] checked by this ticket on the data named, [L] seen in a live run of this
ticket, [I] inferred.

**What changed at format 9 (ticket 4875; city-renderer.md 11.6 and 11.12).** One member kind: `modeltap/frame<NNNNNN>.mtap`,
the compositor's model-tap frame image as the server's model module accepted it, NNNNNN = the frames.bin index from which
it is in force, written only when the image's hash differs from the last one written. `render_config` gains
`render_cities`, `render_landmarks` and `render_models_source`. A format-8 bundle is refused at load as `OlderVersion`
naming version 9's change (the team branch's player, run on the format-8 Tiny bundle [C]: exit 26, "version 9: the cities
and landmarks from the compositor's model tap: the member modeltap/frame<NNNNNN>.mtap, render_config render_cities,
render_landmarks and render_models_source (ticket 4875); record the bundle again"). Ticket 4882 changed what the replay
draws with no file-form change: the model PS inputs t2-t6 and t9 from the install, the records drawn at the frame's own
camera, and a `kLoadBegin` record in `feed.bin` when the unit feed is off. Every format-8 bundle is therefore recorded
again (rule B).

**Build (product path).** The library in `staging-v9/` was recorded by the third build [C, 2026-11-19 12:22-12:39 EST,
`build3_4883.sh`]: the x64 pin of batch 5 (MC #6989, `deps/dll.lock` 1fd51353e; COMMON 18), the fork's source at
1fd51353e (GitHub's archive of the commit, whose fingerprint `build_all` checks against the lock), its release
`build_all` (`--offline`, OK in 477 s), the team branch with 4885 and 4884 merged (`build_server.py --config release
--offline`; build id `repo_commit=7ddbcfcaf, repo_dirty=0, dll_pin=1fd51353e`), the composite (`shell/build.cmd`,
unchanged since the first build), and the layout's bin from ticket 0551's validated take of the same commit
(`F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7`, x64 DLL `78355A75...`, read only: no batch-5 layout was yet laid
out under the pins folder). SHA-256: server.exe `7cae2cc5...`, proxy `31ea1875...`, composite `65460118...`,
recorder_test.exe `bb80d748...`, sceneshot.exe `c0aefa06...`. Every bundle's scene.json carries these pins and the EXE
`04cfdc71...`. The first link of that `build_all` failed in 0.0 s with lld-link rc 3221225794 (0xC0000142,
STATUS_DLL_INIT_FAILED: the "no process could start" state of upstream-fix-batch-2.md); the repeat linked [L]. The first
build (03:17-03:36, `build_4883.sh all`, the batch-4 pin c29442e8c, layout `x64-c29442e8-p58724e0`; server.exe
`8fef58d7...`) recorded the first series, which is not the library (below).

**The recordings [L, RTX 4080].** The recipe of 0495 and 0516 (0403's `rec_live.py`: the `scn` save, `render_units=1`
in the run's `extra.cfg`; every renderer default of the server on) with one step added by `rec_4883.py` after prep:
VP64Options.ini `[Renderer] ServerRenderer = 1, ServerCities = 1, ServerLandmarks = 1` (options 12 / 13, not the release
default; backed up by SHA-256 with the driver's standard files and restored by hash). The proxy then writes
`render_cities=1` and `render_landmarks=1` into server.cfg, so every bundle has both "1" in `render_config` and the
recorder writes the member. One game process per bundle, `--frames 8 --dump all`, `--after 900` (Tiny, fog, huge) and
`1500` (the four scenes, camera by `runner.py camera scenes_tiny.json`). After each run: stamp and checker, restore by
hash, `frontend.py restore` and `status` (MC #6596), the D155 marker. **The library: all seven on instance c5h-a (C:,
Goldberg, FOWLevel 3, Vibrant Textures; MC-B's grant of 11:10, claimed 12:23-13:23) 2026-11-19 12:39-13:22 EST by the
third build** (`series_4883.sh`, `live_4883.sh`), each recorded into a C: folder, copied to F: `staging-v9/bundles/`
with every member's SHA-256 checked against scene.json (`copy_f.sh`, after F:'s read latency was logged under 50 ms) and
removed from C: (MC-B #6865, MC #6868). c5h-a's enabled mods equal an untouched instance's (MC #5862, `mods/`). In every
run the model layer opened in 15.8-17.5 s (ticket 4885's lines, below), both model bits were set 0.5-1.6 s after it
(huge: 49 ms after its first drawn frame, which came after the open) and before the span, so every recorded frame has
the server drawing the cities and landmarks; no install fatal (`install_fatal=0` on every `models` line). Every run:
stamp and checker `scenebundle: OK`, restore by hash rc 0, `provision_verify_rc` 0, front end restored to the manifest's
hashes, no box; `provision.py verify c5h-a` rc 0 after the series.

| Bundle (`F:/c5-64bit-work/scene-library/staging-v9/bundles/`) | Span | modeltap/ members (bytes) | Camera | Members, bytes | scene.json SHA-256 |
|---|---|---|---|---|---|
| `C5H-0301-TINY_0009_20261119-123934_workstation` | 893..922 | 8 (327,840) | load camera | 48, 360,514,414 | `bb640486...fad5` |
| `C5H-0301-FOG_0008_20261119-124546_workstation` | 958..1011 | 8 (293,888) | load camera | 48, 360,468,162 | `4bd8b074...f98c` |
| `C5H-0301-SCNB_0009_20261119-125241_workstation` (coast) | 1497..1530 | 8 (336,128) | coast_c1, 0.088 | 48, 360,951,230 | `cc74b427...aad3` |
| `C5H-0301-SCNB_0009_20261119-125843_workstation` (river) | 1603..1638 | 8 (327,968) | river_c1, 0.115 | 48, 361,055,950 | `1d1172f7...d539` |
| `C5H-0301-SCNB_0009_20261119-130448_workstation` (city) | 1506..1540 | 8 (328,448) | city_c1, 0.080 | 48, 360,977,774 | `a9b05fc6...dc24` |
| `C5H-0301-SCNB_0009_20261119-131111_workstation` (highlights) | 1521..1555 | 8 (332,288) | highlights_c1, 0.091 | 48, 360,975,698 | `8f5ef5c8...de69` |
| `C5H-0301-HUGE_0350_20261119-140930_workstation` (14:09-14:14, the fourth build; gap note) | 1004..1050 | 8 (1,669,152) | load camera | 55, 729,729,656 | `7dcba5cd...2acd` |

Every bundle: format version 9, `render_cities` / `render_landmarks` "1", `render_units`, `render_fow`,
`render_fow_particles`, `render_forest`, `render_bake_near` "1", `render_farmfields` "0", `render_arc` "0"; the only
version note is the bake-cache gap, and on huge also the CPU-built meshes gap (`gap: models: CPU-built meshes: ... held
4 buffer(s) at frame 1004 of the span; ...`). Total 2,894,672,884 B (2.7 GiB); `staging-v9/index.json` by scenerun's
writer (hash in `acceptance.txt`). Huge's first record (14:09-14:14, by the fourth build, server.exe `3e83a874...`, build id
`2fc000750`, the same pin and layout; the six Tiny-save bundles are the third build's, server.exe `7cae2cc5...`, and carry
no geometry, so the note code changes nothing for them). The camera column is the largest translation difference to the stock camera in world units
(`camcheck.py`: scenerun's `approximate`, under 0.5); the full scene.json hashes are in `acceptance.txt`. A model-tap
image was written for every frame of every span (8 members), also with a still camera: the tap's records carry
per-frame constants [I: the frame's time or animation rows; not traced]; at about 41 KB a frame on the Tiny save (48-59
draws) and 209 KB on huge (241 draws) the cost is small. The recorded tap is the paired one: e.g. huge's first member is
tap 1000 for server frame 1004, Tiny's tap 877 for frame 893.

**Ticket 4885's live check [L, the same runs].** Every run's render.log has `models mode: the model layer's open begins:
the install bound waits for its end (open_stall_ms=60000 open_max_ms=600000)` and `models mode: the model layer is open
after N ms (longest time without progress P ms): the install bound (30000 ms) counts from here`. Huge (the library's
run, 14:09): begins 14:11:39.113, open after 17,200 ms (longest without progress 109 ms) at 14:11:56.313, both bits
14:12:10.885 (51 ms after the load's first drawn frame; the 13:17 huge run: 16,398 ms, bits 49 ms); Tiny: 12:41:26.694,
16,397 ms (266 ms), landmarks 535 ms and cities 1,568 ms after the open; the other five 15,848-17,494 ms (longest without
progress 392-444 ms). No install fatal in any run (`install_fatal=0` on every `models` line).

**The first series (not the library).** All seven were first recorded 03:37-04:44 by the first build (batch 4, the
recorder before the fix above): Tiny on c5h-i (F:; the Tiny source save copied in by SHA-256 b2ceb663... and removed
again), the other six on c5h-a after F: was saturated from 03:48 by another ticket's three headless servers reading
huge saves from F: (diskwatch: read latency mean 120-286 ms, idle 0-30 %; MC #6578 allows an F: launch only under
50 ms). They are kept in `F:/c5-64bit-work/0561/4883/v9-first/` (2.8 GiB) for the MC to remove.

**A defect found and fixed: the player drew the span's first frame before its model layer was open [C].** The team
branch's player replayed the Tiny bundle byte-equal in frames 2-8 but not in frame 1: 46,824 colour pixels (largest
channel difference 222) and 45,929 depth values off, i.e. the models missing. Its log gives the order: the model layer
opens on a worker (the library of 624 files and its wire index: open after 18,341 ms), the residency's settle ends after
3,781 ms, frame 960 is drawn and compared, and only then "the model renderer is made on the device". Live, the span was
drawn long after the layer was open. The fix (`server/tools/sceneshot/sceneshot.cpp`, touches widened by lead-0561
#6852): before the span's first draw with the models on, the player polls the model module until its layer is open and
its renderer made (at most 120 s or the settle bound, else `SettleTimeout` naming the models, no picture), then prepares
every library file in one `idle` (the tap's records are the DROPPED ones, files the live server had made drawable), and
logs `sceneshot: models settled before frame N: layer open and renderer made after X ms (P polls), every library file
prepared in Y ms`. With it the Tiny bundle replays byte-equal in 8 of 8 frames (03:56-03:58). scenerun and
`test_offline_equals_live.py` run `sceneshot.exe` and have no settle of their own, so the one fix covers them. No stand-in
test was added (a slow-opening model layer would need one): not verified apart from the seven bundles below.

**A second defect: the recorder wrote a tap the frame did not draw [C for the code, L for the effect].** With the settle
fix, fog and coast of the first series replayed byte-equal in 8 of 8 frames, but huge in none: 12,099-12,103 colour
pixels and as many depth values off in every frame, live nearer than the replay (pixel (64, 462): live depth 0.93251,
offline 0.93359), i.e. a model drawn live that the replay lacked. The cause is in ticket 4875's recorder:
`SceneRecorder::beforeDraw` called `takeModelTap`, which wrote `ModelsScene::writeCurrentImage` = the tap reader's
NEWEST accepted frame. The live frame draws instead the tap that `ModelsScene::beginFrame(frameStamp)` pairs with the
frame's Present inside `Scene::draw` (after `beforeDraw`), and under the product's lockstep with the pair wait 0 that is
almost always the tap one Present older (city-renderer.md 11.12; the live huge run: `pair exact=73 older=3953`). On the
Tiny save consecutive taps drew the same picture; on huge the tap's records change from one Present to the next (the
recorded taps held 242, 288 and 246 draws). The fix (lead-0561 #6934; touches widened by `srv_models_scene.cpp`,
`srv_scene_recorder.cpp`, `models_test.cpp`): the module's `currentImage()` (behind `imageHash` and `writeCurrentImage`)
is the history entry whose sequence number the layer holds (the tap `beginFrame` put back; the reader's newest only when
no entry matches), and the recorder takes it at the start of `afterDraw` (the same frames.bin index). New case
`models_test scene_recorded_image`: after `beginFrame(1018)` with Presents 1017-1020 held, the written image is Present
1018's and its hash the module's `imageHash`; then 1020, the newest older one for an unpublished 1022, 1019, and no
change on `beginFrame(0)`; with the reader's newest the first check fails. `test_models.py` 26 passed (2026-11-19 11:14,
Release). The first series (all seven, recorded 03:37-04:44 by the build before this fix) is kept outside the library in
`F:/c5-64bit-work/0561/4883/v9-first/`; the library was recorded again with the fixed build (the table above).

**A third cause, on huge only: the CPU-built meshes have no member (a rule-A gap, lead-0561's decision B, #7058)
[L, counters].** With both fixes, the six Tiny-save bundles replay byte-equal in 8 of 8 frames, but huge still differed
in every frame at the same spot (10,558 colour pixels, 10,557 depth values; pixel (76, 463): live depth 0.932480, offline
0.933560). The huge run's live `models` line ends `geom open=1 gen=5 entries=4 added=4 ... cpu held=4 kb=252 keys=2
r32=2 resolved=2`: ticket 4879's CPU-built meshes (walls, the Great Wall; city-renderer.md 13) reach the layer from the
compositor's geometry section `Local\VPShellModelGeom.<pid>`, which the bundle does not record and the player does not
have; the six Tiny-save runs show `geom open=0 ... cpu held=0`. What was done instead of a member (decision B; the member
is a follow-up child of epic 0561, city-renderer.md 11.14): the recorder writes the version note
`sb::kModelGeomNote` = `gap: models: CPU-built meshes: ticket 4879's geometry section (Local\VPShellModelGeom.<pid>) held
N buffer(s) at frame F of the span; the bundle has no member for it, so the replay draws the models without them (not
P1; only the offline library is affected, the product draws them live)` whenever `ModelsScene::heldGeometryCount()` (the
layer's held buffers; new, `models_test scene_recorded_image` checks it) is not 0 in the span; `sceneshot` prints
`sceneshot: NOTE the bundle lacks the CPU-built meshes the live models drew: ...`; `test_offline_equals_live.py` reports a
bundle with that note as an expected failure through an imperative `pytest.xfail` with the note as its reason, keyed on
the exact prefix and never on the bundle's name (lead-0561 #7058 asked for a skip; `run_all.py` counts a skip as a failed
suite, COMMON 9, so the lead confirmed the xfail: it is never a pass, and the suite stays green only while the other
bundles replay equal). Options 12 / 13 are 0 in the default file and 1 in the release configuration (the 0.1b installer
turns them on, MC #6924); the gap affects only the offline library, not what the product draws. The huge bundle was
recorded again with that build so that it carries the note (below); the six Tiny-save bundles were not (no geometry,
no note: the same recorder otherwise).

**The checks of the library [C, 2026-11-19 13:28-14:25 EST, workstation RTX 4080; `check_4883.sh`, `exact_4883.sh`,
`scenerun_4883.sh`, `final_4883.sh`; lines in `acceptance.txt`].**
- facts: 7 bundles, format 9, both model keys "1", 8 modeltap/ members each (`facts_4883.py`: `facts: 7 bundle(s), 0 not
  format 9 with the models on and a modeltap/ member`).
- cameras: the four scenes at their stock cameras (`camcheck: OK 1 camera(s), 8 block(s)` each; 0.080-0.115 world units).
- checker (`recorder_test --check`): `scenebundle: OK` on all seven.
- **Offline equals live at tolerance 0:0** (`sceneshot --dumped --compare --no-pictures --tolerance 0:0`, the fourth
  build's player `3395a068...`): the six Tiny-save bundles **8 of 8 frames byte-equal** each (exit 0, `sceneshot: OK 8
  shot(s), each byte-equal to the server's own dump`); huge exit 10 with its NOTE, 10,606 colour pixels and 10,606 depth
  values per frame (the gap above).
- **The standing check** (`C5_SCENE_LIBRARY=staging-v9/bundles run_all.py --only offline-equals-live`, tolerance 1:1):
  `run_all: SUITE offline-equals-live PASS 7 passed, 0 failed, 0 skipped 248 s`, the suite's pytest line `7 passed, 1
  xfailed` (the six bundles and the missing-bundle negative; huge xfailed by its note); `run_all: OK: 1 suite(s) passed
  (plain)`. Again with `C5_P1_TOLERANCE=0:0` (14:38-14:45, lead-0561 #7058: the six at 0:0): `run_all: SUITE
  offline-equals-live PASS 7 passed, 0 failed, 0 skipped 252 s`, pytest `7 passed, 1 xfailed`.
- The staging index: `scenerun.py --library staging-v9 index` (`OK index of 7 bundle(s)`, SHA-256 `4e088903...`) and
  `verify` (`scenerun: OK 7 bundle(s) verified`: every member's SHA-256 equal to scene.json, scene.json equal to the index).
- **scenerun**, baselines into the library's set `baselines/workstation__nvidia-geforce-rtx-4080/` (keyed by bundle id;
  nothing there touched): the six Tiny-save bundles pass 1 (run `20261119-133513_workstation`, player `c0aefa06...`)
  `new` 48, P1 `within` 48; pass 2 (`20261119-134159_workstation`, `--no-baseline-write`) **`equal` 48 of 48**, P1 within
  48; exit lines `scenerun: OK 6 bundle(s), 48 shot(s), 48 picture(s) recorded`. Huge with `--p1 off` (its P1 is the
  gap): pass 1 `new` 8, pass 2 `equal` 8 of 8, `scenerun: OK 1 bundle(s), 8 shot(s), 8 picture(s) recorded`. Fidelity
  (DIAGNOSTIC, compared region median / p95 / max): coast_c1 7 / 54 / 231 (20.3 %), river_c1 8 / 63 / 225 (33.6 %),
  city_c1 2 / 53 / 234 (16.0 %), highlights_c1 5 / 57 / 227 (26.0 %), the format-8 library's numbers within a few
  levels (sections 26, 27). One dated line in `BASELINES.log`. Visual Checks: every picture DIAGNOSTIC, `visualcheck.py
  list --issue 4883 --result undescribed` empty.

**The swap (for the MC, at 0561's merge into main; lead-0561 #6851).** main's player reads format 8 and refuses 9; the
team branch's reads 9 and refuses 8; scenerun and the standing check take every folder of `bundles/`. So the format-9
bundles stay in `staging-v9/` (a library root of its own: `bundles/`, the stock references of the four scenes, and its
own `index.json` by SHA-256) until the merge. `python -I render/evidence/0561/4883/swap_4883.py --dry` prints the moves;
without `--dry` it moves the seven format-8 bundles of `bundles/` to `archive-v8-before-4883/` (kept, one
`retired/RETIRED.log` line each), the seven format-9 bundles into `bundles/`, keeps the old `index.json` as
`index-v8-before-4883.json`, writes `index.json` again from `bundles/` with scenerun's own writer, checks every entry's
scene.json SHA-256 against `staging-v9/index.json`, and removes `staging-v9/`. It refuses and moves nothing when
`bundles/` does not hold seven format-8 bundles, a staged bundle is not format 9 with units, particles, trees, near pairs
and both model keys on with their members (and no gap note but the bake-cache one and `gap: models: CPU-built meshes`,
which huge carries), its scene.json is not the staged index's, a target exists, or `staging-v9/bundles/` does not hold
seven. The baselines are already in the set (keyed by bundle id). `swap_4883.py --dry` on the final staging-v9
(2026-11-19 14:25, rc 0): seven `DRY <bundle> format_version 8 -> archive-v8-before-4883/` lines, seven `DRY
staging-v9/bundles/<bundle> -> bundles/` lines, last line `DRY index.json -> index-v8-before-4883.json; index.json written
again from bundles/`. The first series (`F:/c5-64bit-work/0561/4883/v9-first/`) and huge without the note
(`F:/c5-64bit-work/0561/4883/v9-huge-nonote/`) are outside the library, for the MC to remove.

**Not verified (4883).**
- No stand-in test of the player's model settle (above).
- The library was not copied to oldpc; no oldpc baselines (as since 0353).
- Why the model-tap image differs in every frame of a still span [I above].
- Huge's P1: the CPU-built meshes gap (above); its member is the follow-up.
- Fidelity against stock now includes the server's models in the four scenes; the numbers are DIAGNOSTIC (D112; options
  12 / 13 are 0 in the default file, 1 in the release configuration) and like with like only within this library (one
  pin; the third build, and the fourth for huge).
- No ASan run of these changes. Plain [C, 2026-11-19 14:25-14:36 EST, `run_all.py --only scenebundle-programs --only
  scenebundle-py`, C5R_EXE / C5R_GAME_DIR of c5h-a's install, read only]: bundle_format_test 1020, recorder_test 247,
  sceneshot_test 203, sources_test 24 passed, 0 failed, 0 skipped (`SUITE scenebundle-programs PASS 4 passed`);
  `SUITE scenebundle-py PASS 21 passed`; `run_all: OK: 2 suite(s) passed (plain)`. None of them has a model layer, so the
  recorder's and the player's new model paths are exercised by the seven live bundles only; `models_test` covers
  `currentImage`, `writeCurrentImage` and `heldGeometryCount` (26 passed, 13:50).

(Corrected 2026-11-19 by ticket 4888, section 32: the CPU-built meshes now have a member, `model_geom`; the gap note of
this section is written only when such a member cannot be written, and the huge bundle was recorded again with it.)

## 32. The CPU-built meshes as a bundle member: `model_geom` (ticket 4888, epic 0561, 2026-11-19)

Written 2026-11-19 by implementer-4888 (account B, Opus 5.5, branch `task-4888-scene-bundle-geometry` from main
546af91ce). Option A of 4883's FINDING 3 (thread 0561 #7057; lead-0561 chose B for 0561's merge, #7058, section 31).
Evidence: `render/evidence/0561/4888/` (`build_4888.sh`, `live_4888.sh`, `acceptance.txt`). Marks: [C] checked by this
ticket's tests or on the data named, [L] seen in this ticket's live run, [I] inferred.

**The member [C, code and tests].** Kind `model_geom`, path `modelgeom/frame<NNNNNN>.mgeo` (`srv_scene_bundle.h`, the
block at `ModelGeomFile`): the CPU-built buffers (ticket 4879's geometry section `Local\VPShellModelGeom.<pid>`: walls,
the Great Wall, the road and rail bridge; city-renderer.md 13) **as the server's model layer held them**, i.e. the
section's entries the layer accepted (each checked against its content hash on entry), with their strides and bytes. Form:
the 16-byte header (magic `C5GB`, version 1), u32 count, u32 entryBytes 32, u64 dataBytes, `count` entries {u64 hash, u32
kind (1 index R32, 2 vertex), u32 stride, u64 offset, u64 bytes} in ascending (kind, hash) order, then the data back to
back. NNNNNN = the frames.bin index from which the set is in force: the recorder writes one at the first frame of the span
where the layer holds any, and another at every later frame where the held set changed (an empty list when it then holds
none); a frame replays with the member of the largest NNNNNN <= its index; none in force = none held.
- Why the layer's store and not a second reader of the section: the store is exactly what the live frame drew from (an
  entry the layer refused, oversize or hash, was never drawn live), the bytes are already in the server (the recorder
  only copies them, nothing is mapped or acked: a passive reader in the sense of #7057), and no seqlock retry is needed.
- **Format version stays 9** (MC #6158 says a new member kind raises it; the ticket keeps the six Tiny-save bundles
  unchanged, which a raise would refuse). The member is optional: a format-9 bundle without it is one whose layer held no
  CPU-built buffer, which holds for every format-9 bundle recorded before 4888 (the six Tiny-save bundles held none,
  `geom open=0 ... cpu held=0`, section 31; huge carries `kModelGeomNote`). A player older than 4888 refuses a bundle with
  the member (`model_geom` is an unknown kind to it), so the re-recorded huge enters the library only with this change
  (the swap, below). Noted on thread 4888 for the MC (#7241).
- Checker (`ParseModelGeom`, `CheckBundle`): shorter than 32 bytes or than its entries and data (Truncated), magic
  (BadMagic), version or headerBytes (BadVersion, every header's rule), entryBytes not 32, more than 512 entries
  (`VPSHELL_MODELGEOM_MAX_ENTRIES`), a tail after the data, dataBytes not the entries' sum (WrongSize); per entry
  (BadRecord): kind 1 or 2, stride 4 (index) or 20 / 32 (vertex), bytes not 0, a multiple of the stride, an index buffer at
  least 8 bytes, at most 8 MB (`VPSHELL_MODELGEOM_MAX_BUFFER`), the offset back to back, the content hash
  (`VPModelGeomHash`) equal to the entry's, ascending without a duplicate. Cross-checks: a `model_geom` member while
  `render_cities` is not "1" (BadValue: CPU-built meshes are city art, 4879 13.1); NNNNNN not below the frame count
  (BadRecord); a listed file missing (MissingMember, as every member).
- Recorder (`SceneRecorder::takeModelGeometry`, called from `takeModelTap` at the start of `afterDraw`, i.e. after the
  frame's draw, with the frame's frames.bin index): `ModelsScene::heldSetHash()` (a hash of the held (kind, hash) list
  only) against the last member's; on a change `geometrySnapshot()` (new: the layer's held buffers with
  `ModelLayer::geometryBytes`, new, from c5r's `CpuMeshStore::get`), `EncodeModelGeom`, the member's own checks, then the
  member. One that cannot be written (the bytes of fewer buffers than held, a check, the writer): a record error and
  the version note `kModelGeomNote` ("... were not recorded (<why>) ... from frame index N"), and no more geometry
  members in that bundle; the bundle stays usable, the standing check reports it as the expected failure of section 31.
- Player (`srv_scene_sources.cpp`; `sceneshot.cpp` unchanged): `LoadModelTapMembers` lists the `model_geom` members
  beside the taps (refused while `render_cities` is not "1"); `ApplyModelTapFrame`, before each frame's draw, hands the
  geometry member in force to `ModelsScene::loadGeometry` (new: the layer then holds exactly that list, buffers not in it
  released, new ones added through the layer's own `addGeometry` checks) **before** the frame's tap, and hands the tap
  over again when the geometry changed even if its own member did not: the layer resolves a tap's R32 draws against the
  buffers it holds at the tap's `accept` (`resolveCpu`). The player's NOTE for `kModelGeomNote` stays (a bundle recorded
  before 4888, or one whose member failed).
- [I] A change of the held set inside a span replays exactly only when the live frame re-accepted its tap after the
  geometry poll (the usual case: `beginFrame` puts the paired tap back, a different frame than the reader's newest); a
  frame whose paired tap was the one the poll had just accepted resolved against the store before that poll's geometry.
  Not seen: the huge span's set did not change (below).

**Tests [C].**
- `models_test scene_geometry_member` (new; `server/tests/render/cities/models_test.cpp`, the test's `GeomLayer` with
  `geometryBytes`): nothing before the layer opens; `loadGeometry` of two buffers held in (kind, hash) order with their
  strides and bytes; the same list again adds and releases nothing and keeps `heldSetHash`; a changed list releases the
  buffer not in it and adds the new one, the set hash moves; a buffer the layer refuses (bytes changed after the hash)
  fails naming the buffer and the layer's reason; an empty list holds none (hash 0); a layer that cannot open (no
  database) refuses. `test_models.py`: **27 passed** (2026-11-19 21:05 EST, plain clang-cl build, `C:/c5-64bit-work/4888/models`).
- `bundle_format_test TestModelGeomMember` (new): kind name and path rules; the round trip (sorted, bytes and strides
  kept; the empty list is 32 bytes); a bundle with a tap at 0 and geometry members at 0 and 2 accepted, scene.json kind
  `model_geom` at format 9, the loader lists both (note "2 CPU-built geometry members"); `ApplyModelTapFrame` on a scene
  without a model module refused naming the geometry member first; a model bundle without the member accepted (the six);
  17 file-form refusals each through the checker child (exit code, line, nothing written) and in process; index 3 of 3
  frames (BadRecord); a member with `render_cities` 0 (BadValue, and the loader refuses); a listed file missing
  (MissingMember). Results: below.
- **Results [C, 2026-11-19 21:33-21:41 EST, plain clang-cl, `build_scenebundle_tests.py --run-only` on
  `C:/c5-64bit-work/4888/sb-all`, `C5_GAME_DIR` / `C5R_GAME_DIR` / `C5R_EXE` = c5h-a's install, read only]:**
  bundle_format_test **1175 passed** (1020 at 4883), recorder_test 247, sceneshot_test 203, sources_test 24 passed, 0
  failed, 0 skipped each (`sb-run.log`). Every geometry negative printed its refusal line, e.g. `negative geometry content
  differs from its hash exit 41 scenebundle: REFUSED BadRecord modelgeom/frame000000.mgeo: entry 1 (kind 2, hash
  72C1CDCCE5E3145F): the content hash is 1D09EB8310D08F28`. `run_all.py --only scenebundle-py` (22:20-22:29): `SUITE
  scenebundle-py PASS 21 passed, 0 failed, 0 skipped`.

**Build (product path) [C, 2026-11-19 21:09-21:27 EST, `build_4888.sh all`].** Pin batch 5 (COMMON 18, `deps/dll.lock`
1fd51353e): the fork's source by `git archive` of 1fd51353e from the fork clone into `C:/c5-64bit-work/4888/b5/src-x64`
(fingerprint `v1 fe2fd907...af16 474 files`, matches the lock), its release `build_all --offline` (OK in 553 s; dll
462 s), the composite (`shell_composite.dll` `bb9355a1...`), `build_server.py --config release --offline` (server.exe
`3d16a3df...`, proxy DLL `07a5deb1...`), the layout = ticket 0551's validated take
(`F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7`, its bin read only: x64 DLL `78355a75...`) with those three, repo
76d739ed9 (the code of f808b0f1e; later commits on the branch changed tests and notes only). Readers: c5render,
`recorder_test.exe` `f44fbbaa...`, `sceneshot.exe` `104f6044...`.

**The recording [L, RTX 4080].** Instance **c5h-e** (F:, Goldberg, EXE `04cfdc71...`; MC-B 21:46: every C: instance
was held; `provision.py align548` rc 0 with every file already 5.4.8 and (4a) Squads for VP present, `verify` rc 0
before and after; F:'s read latency mean 0.00 ms at 21:47, limit 50 ms) 2026-11-19 21:49-21:56 EST, `live_4888.sh c5h-e`
(4883's `rec_4883.py` recipe, read only: options 12 / 13 on, backed up and restored by SHA-256; `--frames 8 --dump
all --after 900`), recorded into a C: folder, copied to `F:/c5-64bit-work/scene-library/staging-4888/bundles/` with
every member's SHA-256 checked and removed from C: (4883's `copy_f.sh`). The model layer opened in 18,483 ms (longest
without progress 115 ms), both bits 123 ms after the load's first drawn frame; the models line at the end: `geom open=1
gen=5 entries=4 added=4 released=0 torn=0 refused=0/0/0/0 kb=252 cpu held=4 kb=252 keys=2 r32=2 resolved=2 nogeom=0`
(as 4883's run: the same four buffers). Recorder: `CPU-built meshes: 4 buffer(s), 258216 data bytes, in force from frame
index 0 (frame 1196): modelgeom/frame000000.mgeo (1 members)` (the held set did not change in the span), 8 modeltap/
members. Restore by hash rc 0, front end restored and status rc 0, no box (D155), c5h-e released 21:58.

| Bundle (`F:/c5-64bit-work/scene-library/staging-4888/bundles/`) | Span (frame ids) | model_geom | modeltap/ | Members, bytes | version_notes | scene.json SHA-256 |
|---|---|---|---|---|---|---|
| `C5H-0301-HUGE_0350_20261119-214920_workstation` | 1196..(8 frames) | 1 (`frame000000.mgeo`, 258,376 B: 4 buffers) | 8 | 56, 699,228,196 | the bake-cache gap only | `85a6633e32974b8f2d4cb0640c62eec87f9fad37961ee54d4734ee481da0ede1` |

**Offline equals live [C, 2026-11-19 21:56-21:57 EST, `check_4888.sh 1 2`].** Checker `scenebundle: OK`; `sceneshot
--dumped --compare --no-pictures --tolerance 0:0` with this branch's player: **8 of 8 frames byte-equal** (`sceneshot: OK 8
shot(s), each byte-equal to the server's own dump`; the player's line `8 model tap members, the first in force from frame
index 0; 1 CPU-built geometry members (model_geom), the first in force from frame index 0`). At 4883 the same save
replayed 0 of 8 (10,606 colour pixels and depth values per frame, section 31).

**The library's checks [C, 2026-11-19 21:59-22:19 EST, `check_4888.sh 3 8`, RTX 4080, `C5_GAME_DIR` / `C5R_EXE` =
c5h-a's install].**
- The library's six Tiny-save bundles (`bundles/`, untouched) with this branch's player at 0:0: **8 of 8 frames
  byte-equal each** (exit 0; their loader line has no geometry member: unchanged).
- The standing check (`run_all.py --only offline-equals-live`, tolerance 1:1) on `staging-4888/bundles` (the new huge):
  `run_all: SUITE offline-equals-live PASS 2 passed, 0 failed, 0 skipped 46 s` (huge and the missing-bundle negative;
  no xfail); on the library's `bundles/` (the six and the old huge with its note): `PASS 7 passed, 0 failed, 0 skipped
  305 s`, pytest `7 passed, 1 xfailed` (the old huge, as at 4883).
- `scenerun.py --library staging-4888 index` (`OK index of 1 bundle(s)`, index SHA-256 `3a85981f9771d81e...`) and
  `verify` (`OK 1 bundle(s) verified`).
- scenerun on the new huge **with P1 on** (4883 had to run huge with `--p1 off`): pass 1 run
  `20261119-221618_workstation` `new` 8 (the baselines, keyed by bundle id, in
  `baselines/workstation__nvidia-geforce-rtx-4080/`, one dated line in `BASELINES.log`), pass 2 run
  `20261119-221732_workstation` (`--no-baseline-write`): **P1 `within` 8 of 8, regression `equal` 8 of 8**; both
  `scenerun: OK 1 bundle(s), 8 shot(s), 8 picture(s) recorded`. Pictures DIAGNOSTIC (no stock reference at the load
  camera); `visualcheck.py list --issue 4888 --result undescribed` empty.
- `swap_4888.py --dry` (22:19, rc 0): `DRY ... C5H-0301-HUGE_0350_20261119-140930_workstation format_version 9 ->
  archive-huge-before-4888/`, `DRY staging-4888/bundles/C5H-0301-HUGE_0350_20261119-214920_workstation -> bundles/`, `DRY
  index.json -> index-before-4888.json; index.json written again from bundles/`.

**The swap (for the MC, at 4888's merge into main).** main's player before 4888 refuses the `model_geom` kind, so the new
huge stays in `F:/c5-64bit-work/scene-library/staging-4888/` (its own `bundles/` and `index.json`) until the merge;
then `python -I render/evidence/0561/4888/swap_4888.py` moves the old huge (the gap note) to `archive-huge-before-4888/`
(kept, a `retired/RETIRED.log` line), the new one into `bundles/`, keeps `index.json` as `index-before-4888.json`,
writes it again with scenerun's writer, checks the new entry's scene.json SHA-256 against the staging index and removes
`staging-4888/`. It refuses and moves nothing unless `bundles/` holds seven bundles with exactly one huge carrying the
gap note and the staged bundle is a format-9 huge with a `modelgeom/` member and no gap note but the bake-cache one.
After it the library has no CPU-built meshes gap and the standing check has no xfail.

**Not verified (4888).**
- A change of the held set inside a span (the [I] above): the huge span's set did not change; only `models_test` and the
  file-form tests cover a second member.
- No ASan run of these changes (plain clang-cl only).
- The recorder's failure path (`kModelGeomNote` after a member that cannot be written) has no test of its own: the
  recorder tests have no model layer (as at 4883).
- The recording ran on c5h-e (F:; Squads for VP present) and 4883's on c5h-a (C:); the bundle replays against itself, so
  this does not touch P1, but the two huge bundles are not like with like for any live comparison.

**A trap since Firaxis' update of 2026-11-19 (D200) [L, 2026-11-19 21:27-21:33 EST].** `sources_test` and
`sceneshot_test` read the game from `C5_GAME_DIR` and fall back to the Steam install when it is unset
(`sceneshot_test.cpp` main); `build_scenebundle_tests.py --run` does not set it (`run_all.py` does, from `C5R_GAME_DIR`).
With the Steam install's new EXE every scene case fails at the terrain renderer's init: `NotFound: TerrainShader blob
0x067d6490 not in the .shader section` (this ticket's first run: `sceneshot_test` 25 passed / 44 failed, `sources_test`
22 / 2, with `C5R_EXE` set to another copy but `C5_GAME_DIR` unset). Set `C5_GAME_DIR`, `C5R_GAME_DIR` and `C5R_EXE` to
a Goldberg instance's install (c5h-a, EXE SHA-256 `04cfdc71...`, the EXE every format-9 bundle's pins name); MC-B made it
an order (2026-11-19, thread 4888): never the Steam install (D200 guard) and not `deps/local-settings.ini`'s `render_exe`
copy (`b2ce96ae...`, the new EXE by MC-B; not checked here).
(Corrected 2026-11-19 by ticket 0589, section 33: the `b2ce96ae...` copy is build 4390913, not the new EXE [C, its PE
header: TimeDateStamp 0x546CD0A8, 16,982,584 bytes, read 2026-11-19 23:08]; it differs from the instances' `04cfdc71...`
because Steam makes each installed copy unique. MC-B's order to use an instance's EXE stands for the bundles' pins. The
fallback to the Steam install is gone since 0589.)

## 33. The render tests' install and EXE build: no Steam default (ticket 0589, 2026-11-19)

Written 2026-11-19 by impl-0589 (account B, branch `task-0589-render-tests-goldberg-default`). Why: section 32's trap.
Five render tests fell back to the Steam install when `C5_GAME_DIR` was unset: `sceneshot_test.cpp`, `sources_test.cpp`,
`water_scene_test.cpp` (two cases), `water/test_water_host.py` and `scenebundle/recorder_bitcompare.py` (a literal Steam
path) [C, grep of `server/tests` for `steamapps` at d74e7212e]. The other render suites already read `C5R_GAME_DIR` or
the setting `render_game_dir` with no default.

**The rule, one implementation per language [C, code and tests].**
- C++: `server/tests/render/test_install.h` (header only; included as `"../test_install.h"`, no build script change).
  `c5test::GameDir()` gives the install: `C5_GAME_DIR`, else `C5R_GAME_DIR`, else `C5_RENDER_GAME_DIR`, else
  `render_game_dir = ...` in the settings file (`C5_SETTINGS_FILE`, else `deps/local-settings.ini` of the checkout the
  header was compiled from, found through `__FILE__`; `%VAR%`, a leading `~` and a relative value expanded as
  `tools/release/settings.py` does, `$VAR` not). With none, a placeholder path that names the variables, so the existence
  check fails loud. `c5test::ExeBuildWhy(exe)` is `exe_build.py`'s rule on the file: PE TimeDateStamp 0x546CD0A8 and a
  size of 16,982,584 or 16,982,840 bytes; "" when accepted, else the reason (missing, not PE, another stamp, the stamp at
  an unknown size). `C5_ALLOW_UNKNOWN_EXE_BUILD=1` (exactly `1`) lets an unknown build through and prints a line.
- Python: `server/tests/render/gameinstall.py`: `game_dir()` (same order, the settings through `tools/release/settings.py`
  itself), `exe_refusal()` / `require_exe()` / `require_install()` through `tools/harness/exe_build.py` `check_install`
  (the file's folder and name), with the same override variable.
- Callers: `sceneshot_test` prints `installed game <dir> (from <variable>)` and runs no case when the install's EXE is
  missing or refused (one failed check names why); `sources_test` checks `C5R_EXE` and the install's EXE in
  `NeedInputs`; `water_scene_test`'s two real-module cases; `test_water_host.py` `need_game()`; `recorder_bitcompare.py`
  (EXE and install at start); the render `conftest.py` `exe` fixture (every pytest render suite's `C5R_EXE`);
  `run_all.py` refuses at the start (argparse exit 2) when `--exe`, `--game`'s EXE or an inherited `C5_GAME_DIR`'s EXE
  is not build 4390913.
- Negative tests: `water_scene_test install_rules` (C++) and `server/tests/render/test_gameinstall.py` (Python, in
  run_all's `render-host` suite), both on made-up files only: the order, the placeholder with nothing set (never a
  `steamapps` path), every refusal kind, both known sizes accepted, the override at `0` and `1`.

**Which copies are build 4390913 [C, PE headers read 2026-11-19 23:05-23:10 EST].** `pure_vp`'s
`CivilizationV_DX11.exe`: 0x546CD0A8, 16,982,584 bytes, SHA-256 `04cfdc71...` (the same file as c5h-a's, the EXE the
format-9 bundles' pins name, section 32). `deps/local-settings.ini`'s `render_exe` copy: 0x546CD0A8, 16,982,584 bytes,
SHA-256 `b2ce96ae...` (build 4390913 too; correction above). The golden copy on F: (section 3.8a of
`headless-and-parallel-testing.md`): 0x546CD0A8, 16,982,584 bytes. The workstation's `render_game_dir` setting names
`pure_vp`, so a test run with nothing set reads `pure_vp`.

**Runs [run, workstation, plain clang-cl, 2026-11-19; inputs `pure_vp` read only for `C5R_EXE` and `C5R_GAME_DIR`,
`C5_GAME_DIR` unset].**
- `run_all.py --only render-host --only water-scene --only scenebundle-programs --only scenebundle-py` (23:18-23:58 EST,
  `C:/c5-64bit-work/0589/ra`): `run_all: OK: 4 suite(s) passed (plain)`. render-host 147 passed (with
  `test_water_host.py` and `test_gameinstall.py`), water-scene 29 (with `install_rules`), scenebundle-programs 4 programs
  (bundle_format_test 1175, recorder_test 247, sceneshot_test 204, sources_test 24 passed, 0 failed each;
  `installed game ...pure_vp... (from C5_GAME_DIR)`), scenebundle-py 21 passed. Before 0589 with `C5_GAME_DIR` unset:
  sceneshot_test 25 passed / 44 failed, sources_test 22 / 2 (section 32's trap).
- Direct, 23:15: `water_scene_test install_rules real_module_draws real_module_shared_index` with no install variable
  set: all PASS through the settings file (`deps/local-settings.ini` -> `pure_vp`, found through `__FILE__`). With
  `C5_SETTINGS_FILE` pointing at an absent file: `real_module_draws` FAIL naming the variables, `sources_test` prints
  `installed game <no install: set C5_GAME_DIR or C5R_GAME_DIR ...> (from nothing set)` and fails its game cases (exit
  12). A 5-byte fake EXE as the install: `refusing an EXE that is not build 4390913 ... is not a PE file`. `run_all.py
  --exe <a .py file>`: argparse exit 2 with exe_build.py's refusal.

**Not verified (0589).** No ASan run (plain only). Not run against an instance copy (c5h-*): `pure_vp`'s EXE is the
same file (`04cfdc71...`). `recorder_bitcompare.py` was not run (a two-tree compare; only its start-up path changed).
The C++ settings-file fallback needs `__FILE__` to be absolute (it is with the build scripts' absolute source paths);
otherwise it falls to the placeholder, which fails loud.

## 34. The players read a live instance's mods database: the '.fxsxml not found' exit 8 (ticket 0614, 2026-11-20)

Written 2026-11-20 from 13:21 EST by the implementer of ticket 0614 (account B, branch
`task-0614-scene-players-asset-source`). Reported by epic 0455's workers W11 and W12 (queue #8298, #8312).

**Where the players take the game's and the mods' files from [C, `server/tools/sceneshot/sceneshot.cpp`
`OptionsFromBundle` and `srv_residency_source.cpp` `ModFolders` at main 779320efe].**
- The install: `--install DIR`, else the bundle's `scene.json` `asset_set.install` (the install of the instance that
  recorded it). The EXE: `--exe PATH`, else `<install>\CivilizationV_DX11.exe`. The terrain, water, models, farm fields,
  arrows and the units' base art come from that install.
- The mods' folder: `--mods DIR`, else `render_config` `render_decal_mods` (the recording instance's
  `Documents\My Games\Sid Meier's Civilization 5\MODS`). The player sets `C5_DOCUMENTS` to the folder above
  `My Games`; the residency's decal pass and the unit scene's pre-check each call `srvrender::ModFolders()`, which reads
  `<C5_DOCUMENTS>\My Games\Sid Meier's Civilization 5\cache\Civ5ModsDatabase.db` **at that moment** and puts the
  activated mods' folders above the install. With a `-journal` or `-wal` file beside the database, or an unreadable
  one, every folder of MODS counts (the rule before issue 0348: a silent widening of the mod set).
- No instance is picked: neither `sceneshot.exe` nor `scenerun.py` nor `test_offline_equals_live.py` asks the pool.
  `scenerun.py` passes `--install` only when given one and never `--mods`. So by default every player reads the
  recording instance's own install, MODS folder and mods database. The 16 bundles of the library on 2026-11-20 name
  three pool instances [C, each bundle's `scene.json`]: c5h-a (the six 0301 bundles but HUGE), c5h-e (HUGE) and c5h-c
  (the nine 4980 bundles).

**The cause [C, from the players' logs and the instances' harness logs, 2026-11-20].** A live run on the recording
instance rewrites its mods database while the player reads it: the database said "3 activated" before the launch,
"0 activated" within a minute after it, and "3 activated" again once the front end had run. Which step writes the zero
is [I]: the game's own deactivation on the way to its main menu (the boot's `BeforeDeactivateMods` steps,
crash-ledger-audit.md), then the vp-dll-dev skill's patched MainMenu.lua that `frontend.prepare` lays in
(`Modding.EnableMod` ... `Modding.ActivateEnabledMods()`) about two minutes after the launch. The player reads the database twice (decal set-up, then the units' pre-check
on its worker thread); a change between the two reads gives a unit scene without the mods' folders, and every VP unit
model is missing. W11's C4PORT log (`F:/c5-64bit-work/0455/4982/w11/p1/C5H-4980-C4PORT_0009_20261120-034655_workstation.log`,
written 12:56:28): line 21 `decals: mods: 3 activated in '...c5h-c\...\Civ5ModsDatabase.db'`, line 64 `decals: mods: 0
activated` (same file), then `units: pre-check: ... .fxsxml not found` for 21 units, exit 8. c5h-c's `run/harness.log`:
`launch_start` 12:55:30 (w23-0042, ticket 0042), game pid 51184 from 12:55:32; the database's modified time afterwards
12:57:25, "3 activated of 17" (the front end's activation). All four player logs under `F:/c5-64bit-work/0455` with a "0
activated" read fall inside a game's lifetime on the instance they read:

| player log written | instance | game on that instance (harness.log) | reads |
|---|---|---|---|
| 11:53:35 `4986/r0-c5hc-busy/c7-on-a.log` | c5h-c | pid 64300, 11:52:21-11:55:37 (w21-0042) | 0 only |
| 12:31:31 `4986/dbg/d4-r128-yaw.log` | c5h-a | pid 37880, 12:30:22-12:36:14 (impl-0609) | 0 only |
| 12:37:14 `landing/ruleA/with_C5H-4980-C7INTERCEPT...log` | c5h-a | pid 46724, 12:36:21-12:41:47 (impl-0609); database written 12:38:10 | 3, then 0; 21 `.fxsxml` |
| 12:56:28 `4982/w11/p1/C5H-4980-C4PORT...log` | c5h-c | pid 51184, from 12:55:32 (w23-0042) | 3, then 0; 21 `.fxsxml` |

So the reported failures are not a torn read of a half-written file (no `-journal` was seen; the "0 activated" state is
a committed state of the game's own) but a read of a state that only exists while a game is starting on the instance.
A reproduction by a deliberate launch was not made: the four logs and the launch times already pin it, and a launch
needs a granted instance place.

**The worse case the ticket names [I, from the code].** Three more writes by live runs widen or change the set without
a failure: `tools/release/stages/instance.py fix_modinfo` removes `Civ5ModsDatabase.db` (until the game writes it
again every folder of MODS counts: c5h-a's database lists 18 mods, 3 activated); a `-journal` beside it during a game's
write does the same; `provision.py set-dll` / `set-proxy` replace files under `MODS\(1) Community Patch`. A P1 compare
that ran in such a window would draw a different mod set and could still pass where the extra folders hold no art the
scene names.

**The rule (ticket 0614, from branch `task-0614-scene-players-asset-source`) [C, code and tests].**
1. *No read of a claimed instance.* Before it reads anything the player checks every source: the install, the EXE, the
   mods' Documents folder (`C5_DOCUMENTS`), an asset root and its install. A source inside a pool instance (the nearest
   folder at or above it holding `instance.json`) whose claim file exists is refused with exit 9 (Install). The claim
   file is `<C5_POOL_DIR, else <C5_DEFAULT_INSTANCES_ROOT, else %USERPROFILE%\c5-instances>\_pool>\claims\<name>.json`.
   An unreadable or stale claim counts too. The line names the instance, the claim file, its owner and ticket, and
   `--asset-root`. The check runs again before the OK line, so a claim made during the replay fails the run.
   `tools/harness/pool.py holder PATH` is the same rule as a read-only query (JSON; exit 4 claimed, 0 otherwise); it
   writes, renews and removes nothing. An unclaimed instance is read, with a `NOTE: the sources are pool instance X's
   own` line.
2. *A frozen source.* `server/tools/sceneshot/assetroot.py make --from <instance> --out DIR` copies the instance's mods
   database and its activated mods' folders, nothing else, into
   `DIR/Documents/My Games/Sid Meier's Civilization 5/{cache,MODS}`. It names an install that is never launched: by
   default the setting `render_game_dir`, which on the workstation is pure_vp, whose EXE is the bundles' pin. It refuses
   a claimed or running source instance (before and after the copy), a `-journal`/`-wal`, a database that changed during
   the copy, 0 activated mods, an activated mod without a folder, and an install inside a pool instance. It writes
   `asset-root.txt` with the database's SHA-256, the MODS listing hash (SHA-256 over the sorted lines `<relative
   path>\t<size>\n`) and a content hash for `verify --full`. `sceneshot --asset-root DIR` (or `C5_SCENE_ASSET_ROOT`;
   `scenerun.py run --asset-root DIR`) checks the manifest before anything else and refuses any difference with exit 9.
   It then requires the database to read with the recorded number of activated mods and each mod's folder present.
   `--install` or `--mods` together with an asset root is a usage error (exit 2). No source falls back to another
   (D118).
3. *What the old fallback still does.* `ModFolders`' "every folder of MODS" rule for an unreadable database is
   unchanged in the server. In the player, an asset root can no longer reach that rule (point 2). An instance's own
   database still can, while it is unclaimed.

**Runs [run, workstation, 2026-11-20 14:19-14:25 EST, MC-B's grant 14:19; the player built from this branch with
`build_sceneshot.py --smoke`; bundle C5H-4980-C4PORT from the checked copy
`C:/c5-64bit-work/scene-library/bundles`, `--dumped --compare --tolerance 1:1 --no-pictures`].**
- (a) `--asset-root` made from c5h-c at 14:15 (2131 files under MODS, 3 activated mods, install pure_vp): exit 0,
  8 of 8 frames colour and depth EQUAL. The manifest check took 35 ms. The EXE matched `pins.exe_sha256`, and the VT
  asset set was equal (`45b3b433...`).
- (b) c5h-c's own files, with `pool.py list` showing it free just before: exit 0, 8 of 8 EQUAL, and the NOTE line.
- (c) c5h-c shown as claimed through a private pool dir (`C5_POOL_DIR` with a hand-written claim file; no real claim):
  exit 9 with the claimed line. `--out` was not created.
- Tests: `test_assetroot.py` 22 passed (with `C5_SCENESHOT_EXE` = this build); `tools/harness/tests/test_pool_holder.py`
  8 passed; `test_scenerun.py` with the new `--asset-root` case.
- `run_all.py --only scenebundle-programs --only scenebundle-py` [run, 14:29-14:59 EST, plain clang-cl, pure_vp read
  only for `--exe`/`--game`, `C:/c5-64bit-work/0614/ra`]: `run_all: OK: 2 suite(s) passed (plain)`.
  - scenebundle-programs: bundle_format_test 1175, recorder_test 247, sceneshot_test 212 (204 before 0614: the 4
    checks of `CaseClaimedInstance` on WARP and on hw), sources_test 24 passed, 0 failed each.
  - scenebundle-py: 22 passed.
- ASan [run, 14:59-16:02 EST, MSVC `/fsanitize=address`, the same inputs]. In `run_all.py --asan --only
  scenebundle-programs`, bundle_format_test 1175, recorder_test 247 and sources_test 24 passed. sceneshot_test hit the
  suite's 1500 s timeout, a known limit (sections 19 and 23: it needs 16-22 min under ASan). Run alone on the same ASan
  binaries (`build_scenebundle_tests.py --run-only --asan --only sceneshot_test --timeout 3600`), it passed 212, failed 0,
  with WARP and hw PASS and no AddressSanitizer report.
- `tools/harness/tests/test_safety_static.py` (pytest, whole): 55 passed.

**The workstation's shared asset root (MC #8530, 2026-11-20 16:28) [C].** One root lives at
`C:/c5-64bit-work/scene-asset-root/mods548-vt`.
- It was made by `assetroot.py make --from c5h-c` while c5h-c was free, with pure_vp as the install. It holds 2131
  files under MODS (CP, VP and the EUI compatibility files of VP 5.4.8): 253 MB, 2133 files in all, each set read-only.
- `asset-root.txt`: `mods_db_sha256=d040b32e...`, `mods_tree_sha256=9748711c...`. `verify --full` OK at 16:30.
- One root serves all 16 library bundles. The activated mods' files of c5h-a, c5h-b, c5h-c and c5h-e are byte-equal
  (2131 files each) except `(1) Community Patch/CvGameCore_Expansion2.dll`, which the renderer does not read. All 16
  bundles carry the same `pins.exe_sha256` (`04cfdc71...`, pure_vp's EXE) and the same VT asset-set hash
  (`45b3b433...`, pure_vp's) [C, scene.json of each bundle in the C: working copy, 16:26].
- Whoever changes the mods (a new VP version or profile) or the asset set makes a new root under a new name with
  `assetroot.py make` from a free instance of that profile, and points `C5_SCENE_ASSET_ROOT` at it. A root is never
  edited in place. A changed root shows itself: `assetroot.py verify` and every player run compare the database's
  SHA-256 and the MODS listing hash with `asset-root.txt` and stop with exit 9 on a difference. A root of other mods
  than a bundle's draws other art and fails P1: make one root per mod set. The bundle's EXE pin and VT hash are still
  checked against the root's install (exit 9).
- The check runs inside the player only (about 35 ms per run). `run_all.py` and `scenerun.py` do not verify the root at
  their start the way `scenelib.py check` does for the library, so a bad root fails the first bundle with exit 9.
- `--install` with a never-launched install alone does not get past the refusal: the mods' folder still comes from the
  bundle's `render_decal_mods`, the recording instance. That instance is refused while claimed, which is right, because
  its database is what a live run rewrites. Only `--asset-root`, or `--mods` pointing at a private copy, avoids the
  instance. An `--install` outside any pool instance is never refused by itself.

**Rule A with the shared asset root [run, workstation, 2026-11-20 16:36:26-16:59:15 EST, MC-B's slot].**
- Setup: `run_all.py --only offline-equals-live`, branch head d836f8080 (contains main 571c54e46: combat stage 3 and
  the 0455 landing), `C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt` (verified at the start),
  `C5_SCENE_LIBRARY=C:/c5-64bit-work/scene-library/bundles` (the scenelib check at the start), pure_vp as
  `--exe`/`--game`, build trees in `C:/c5-64bit-work/0614/rulea`.
- Result: `run_all: SUITE offline-equals-live PASS 17 passed, 0 failed, 0 skipped 796 s` (16 bundles and the no-bundle
  negative); `run_all: OK`.
- Every one of the 16 `sceneshot.json` names pure_vp as the install: no player read an instance.
- C: disk window (`tools/scenelib/diskwindow.py`, 274 records): idle mean 95.9 %, busy 0 %, read latency p95 1.7 ms,
  max 3.8 ms.

**Interim practice until this is on main (the workaround of epic 0455's workers, MC-B's briefs on 2026-11-20).** A
player run on the workstation reads a private copy of the recording instance's MODS folder and
`cache/Civ5ModsDatabase.db`, never a claimed instance's own folders. The copy is taken while the instance is free and
passed with `--mods <copy>/Documents/My Games/Sid Meier's Civilization 5/MODS`. W12 kept its copy on F:, as of
2026-11-20 12:50. *Superseded once this branch is on main:* make an asset root and pass `--asset-root`. The brief line
"read a private MODS and mods-DB copy" then goes out of the worker briefs; MC-B's briefs are messages, and no file in
the repository carries the line [C, grep of `docs/` and `tracker/` at 175a7edc7].

**The rule for teams (ticket 0625, 2026-11-20): export `C5_SCENE_ASSET_ROOT` globally; the suites handle it.** (This
replaces MC-B's interim rule of 2026-11-20 18:50 EST, "set the variable only for scenerun and offline-equals-live".)

- *The fault [C, MC-B's rule A run of epic 0042's stage 4, queue #8664, 2026-11-20 18:50 EST; code at main d3b8fe6d7].*
  With the variable in the environment, `run_all.py`'s suite scenebundle-programs failed: 5 checks of `sceneshot_test`
  ended `sceneshot: FAILED 2 Usage: C5_SCENE_ASSET_ROOT '...' names the install and the mods' folders: not with
  --install`. `sceneshot_test` compiles the player in (`sceneshot::Run`), so its "child runs" read the test's own process
  environment, and every case passes `--install` (its `Args()`), some also `--mods`. run_all passed its whole
  environment to every build and suite. The other three programs of the suite do not start a player.
- *The fix (branch `task-0625-asset-root-env-leak`) [C, code].* Both places, because each covers runs the other does not:
  1. `sceneshot_test.cpp` `main()` unsets an inherited `C5_SCENE_ASSET_ROOT` before the cases and prints
     `sceneshot_test: C5_SCENE_ASSET_ROOT=... inherited: unset for the cases (they name their own sources)`. This covers a
     direct run (`build_scenebundle_tests.py --run-only`, a run by hand) that run_all never sees. `CaseRefusals` sets the
     variable on purpose and checks that the player's own rule still holds: with `--install`, and with `--mods`, exit 2
     and the line naming `unset C5_SCENE_ASSET_ROOT` (2 checks per adapter). `sceneshot.cpp` is unchanged.
  2. `run_all.py` takes the variable out of the environment of every build and suite and gives it back only to
     `ASSET_ROOT_SUITES` (today offline-equals-live, the suite whose player reads the library's bundles); it prints
     `run_all: C5_SCENE_ASSET_ROOT=...: passed only to offline-equals-live`. This covers any other suite that starts a
     player with its own sources (none today besides sceneshot_test; `test_assetroot.py` already clears it with
     `monkeypatch.delenv`, and `test_scenerun.py` uses a stand-in player).
- *Runs [run, workstation, 2026-11-20, plain clang-cl, pure_vp (build 4390913) read-only as `--exe`/`--game`,
  `C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt` exported globally for every run, trees in
  `C:/c5-64bit-work/0625`].*
  - offline-equals-live (rule A, MC-B's GO 19:26): 19:31:03-19:46:04 EST, the branch's `run_all.py` at 92d1b0970,
    `--no-build` on c5render and the sb objects built from main d3b8fe6d7 (the branch changes neither) and a sceneshot
    built from them; `C5_SCENE_LIBRARY` the checked C: copy (scenelib OK, 16 bundles, 7.81 GB). It printed
    `run_all: C5_SCENE_ASSET_ROOT=...: passed only to offline-equals-live`; result `SUITE offline-equals-live PASS 17
    passed, 0 failed, 0 skipped 900 s` (16 bundles and the no-bundle negative), `run_all: OK`.
  - scenebundle-programs without the fix (the test that fails without it): 19:46:30-19:54:40 EST, `run_all.py` of main
    d3b8fe6d7 (a `git archive` copy) on its own builds: `SUITE scenebundle-programs FAIL ... (exit 1)`.
    bundle_format_test 1175, recorder_test 247 and sources_test 24 passed; sceneshot_test `RESULT: 76 passed, 136
    failed, 0 skipped`, every failure's last line `sceneshot: FAILED 2 Usage: C5_SCENE_ASSET_ROOT '...' names the install
    and the mods' folders: not with --install (unset C5_SCENE_ASSET_ROOT to name them yourself)`. (#8664's "5 cases"
    counts cases; this count is of checks.)
  - scenebundle-programs with the fix: 19:54:40-20:12:16 EST, the branch's `run_all.py` at a34262c33 on its own builds
    (c5render 49 s, sb 241 s): `SUITE scenebundle-programs PASS 4 passed, 0 failed, 0 skipped 766 s`, `run_all: OK`.
    bundle_format_test 1175, recorder_test 247 and sources_test 24 passed; sceneshot_test `RESULT: 216 passed, 0 failed,
    0 skipped` (212 before, plus the 2 new checks of `CaseRefusals` on WARP and on hw), both adapters PASS. run_all kept
    the variable from the suite, so this run tests fix 2; the direct ASan run below tests fix 1 (its "inherited" line).
  - ASan, sceneshot_test alone and direct: 20:13:02-20:43:45 EST (build included; the run about 26 min), branch at
    a34262c33, `build_scenebundle_tests.py --out C:/c5-64bit-work/0625/asan-sb --asan --run --only sceneshot_test
    --timeout 3600` through `quietlock.py run`, the variable exported and not removed by any runner. It printed
    `sceneshot_test: C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt inherited: unset for the cases (they
    name their own sources)`, then WARP PASS, hw PASS, `RESULT: 216 passed, 0 failed, 0 skipped`, no AddressSanitizer
    report, `build_scenebundle_tests: OK: 1 test program(s) passed`.

## 35. The fights' approach rule and the tier gate (ticket 4737, epic 0042, 2026-11-21): two render_config keys, format 9 unchanged

Written 2026-11-21 by W43 of team-0042 (account B, branch `w43-4737` from W39's `w39-4737` 84727bfb8). Ticket 4737 turned
on by default two route S rules (`research/05-graphics-composition/unit-combat-server.md` 10.10): P1, the trigger
emitter's tier gate (`EmitterParams::tierGate`, host key `render_test_units_tier_gate`), and P2 (a), the choreography's
`CombatParams::charge_any` (every approaching melee attacker whose member enables RunCharge charges with 1120 / 1140; off:
the 4775 rule, only a rush formation member charges). Rule A (section 25's addition: a default-drawn change ships a member,
a `render_config` key or a `gap:` note) is answered with keys, as the farm fields did (section 30).

- **Which bundles change [C, 2026-11-21 15:48 EST].** None today. `C:/c5-64bit-work/0042/w43/scan_combats.py` (feed.bin
  record 5, COMBATS: u64 loopIter, u32 nCombats) over the 16 bundles of `C:/c5-64bit-work/scene-library/bundles` (the
  seven 0301 scenes and the nine 4980 scenes): every feed.bin parsed to its end, **0 combats in each**, as W20's scan of
  2026-11-20 10:52 (unit-combat-server.md 12.2). The feed holds every drain from the render thread's start, so no frame of
  any `units_history.bin` or span plays a fight, and charge_any acts only in `ControllerFightPlayer::start`
  (`srv_units_anim.cpp`). P1 draws nothing: it drops Sound triggers only (effects are not gated) and the player opens no
  trigger stream. So offline-equals-live is not expected to change with 4737.
- **Why a key anyway [I].** The first bundle with a fight in its history or span would replay every fight frame
  differently if the player's rule were not the recorder's: the charge (1120, then 1140) instead of run (1400), combat
  ready (1600) and attack (1100), the fights 0.1-1.0 s shorter (W39's 19 trigger scenes, unit-combat-server.md 10.10), so
  every later frame of the history differs too.
- **The keys [C, code].** Host key `render_test_units_charge_any` (TEST ONLY; "1" default, "0" the 4775 rule; other values
  refused: `render_test_units_charge_any=<v>: 0 or 1`) -> `UnitsOptions::chargeAny` -> `AnimRunner::setChargeAny` ->
  `CombatParams::charge_any` of every fight the runner starts. `RecorderRenderConfig` (`srv_render_host.cpp`) writes
  `render_test_units_charge_any` and `render_test_units_tier_gate` ("0" / "1") into every bundle. The player
  (`sceneshot.cpp`, units on) sets `chargeAny` only when the key is "1": **a bundle without the key (recorded before 4737)
  replays with the rush-only rule its server ran**; it prints `sceneshot: render_config: units charge_any C, tier gate G`
  (G "absent" without the key; the tier gate is printed only, the player sends no triggers). No member, no
  `FormatVersionChange` (no reader refuses an unknown `render_config` key), no `gap:` note (the player reproduces both
  rules). `unitfeed` keeps the build's default (no recorded unitfeed input with a fight is in any suite: its tests use the
  builder's synthetic feeds).
- **Tests [C, code].** `test_render_host.py` `test_config_refused`: `render_test_units_charge_any` "2" and "on",
  `render_test_units_tier_gate` "2"; `test_recorder_host.py` `test_bundle_from_the_host`: both keys "1" in a bundle the host
  recorded; `sceneshot_test.cpp` (units gap case): the keys absent -> `charge_any 0, tier gate absent`, `--set` "1" / "1"
  -> `charge_any 1, tier gate 1`, "0" -> 0, "on" refused by the server's parser before the line.
- **The builders outside run_all [C, run, workstation, 2026-11-21 15:58-16:19 EST]** (`C:/c5-64bit-work/0042/w43/allb43.sh`,
  head 3e071d9f9 = the keys + main a505511a6 with 0615's live terrain, trees on C:, every build through `quietlock.py run`,
  pins `keep/pins-0.1b`, the pure_vp copy and 0327's database read only): render library OK, `build_server.py --deps` OK,
  `test_scene_units` 74/74 plain and 74/74 ASan (0 AddressSanitizer reports), units extras 13/13, unitfeed built and
  `test_unitfeed.py` 19 passed, trigger_scene built and `run_trigger_scenes.py` 19/19 PASS, `run_effect_routes_test` OK,
  farm fields and unit shadow built. Again after main 9dce33eab (4739's compiled change in `srv_units_anim.*` and
  `srv_units_scene.cpp`) at head 7c894e8db, 16:48-17:04 EST, the same steps: all OK, `test_scene_units` 79/79 plain and
  79/79 ASan (0 reports; 4739 added cases), extras 13/13, unitfeed 19 passed, trigger scenes 19/19, effect routes OK.
- **Rule A run [C, run, workstation, 2026-11-21 17:50:12-18:37:02 EST, MC-B's slot (moved to 17:50 by MC-B 17:47)].**
  `server/tests/render/run_all.py` plain on head 4badc6c99 (code = 7c894e8db: `w39-4737` + the keys + main up to 25a8dbcad,
  with 0615's live terrain and 4739), `C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt` exported (the tree
  has b1a98cd0a; run_all passed it only to offline-equals-live), `C5_SCENE_LIBRARY=C:/c5-64bit-work/scene-library/bundles`
  (`scenelib: OK copy ... matches the master ...: 1082 file(s), 16 bundle(s), 7.81 GB`; read from C:, no F: read), the
  pure_vp install as `--exe` / `--game` (read only), script `C:/c5-64bit-work/0042/w43/rulea.sh`, logs
  `C:/c5-64bit-work/0042/w43/ra/logs/`. Every build OK; **`run_all: OK: 9 suite(s) passed (plain)`**: render-host 167/0/0
  (164 before 4737 plus the three new refusals), fow-scene 3/0/0, overlay-scene 3/0/0, shadow-ice-scene 6/0/0, water-scene
  29/0/0, forest-scene 3/0/0, scenebundle-programs 4/0/0 (bundle_format_test 1175, recorder_test 247, sceneshot_test 226
  (216 before plus the 4737 checks on WARP and hardware), sources_test 24), scenebundle-py 22/0/0, **offline-equals-live
  17/0/0** (the 16 bundles and the no-bundle negative, 680 s). So 4737 with its keys changes no library replay, as the
  scan predicted. Main merged after the run (290fcde61) changed no compiled or test file (`git diff --name-only`: evidence,
  tracker, research and docs only), so the result stands for that head. **It is the run of 4737 alone**: 4737 merges on
  top of ticket 4738 (form C, `unplaced_attacker_approaches`, same `combat.{h,cpp}`), and the MC asked for one run on the
  combined head (MC #9686), below.
- **The combined head with 4738 (W43, 2026-11-21).** `origin/w40-4738` fa6c69901 merged into `w43-4737` (combat.h: both
  `CombatParams` fields kept; README 05 row: both texts); 4738's test of form C counted Approach steps only and would have
  failed with charge_any on (its archer has the soldier's RunCharge and AttackCharge, so it charges): it now runs with
  charge_any on and off (unit-combat-server.md 10.10, "With 4738's form C"). Every builder of the shared sources on head
  cb69b8916, 18:42-19:15 EST (`C:/c5-64bit-work/0042/w43/allb43b.sh`) [C, run]: `render/build.py --test` 106/106,
  `--asan -R c5r_unitanim` passed with 0 ASan reports, server build OK, `test_scene_units` 79/79 plain and ASan (0
  reports), `run_trigger_emitter_test --asan` OK, trigger scenes 19/19 plain and 19/19 `--effects`, extras 13/13, unitfeed
  19 passed, effect routes OK, `build_render_tests.py`, scenebundle tests, sceneshot (`--smoke`), farm fields and unit
  shadow built.
- **The rule A run on the combined head [C, run, workstation, 2026-11-21 19:28:31-20:26:29 EST, MC #9686, MC-B's slot]**:
  head 23d90c310 (`w43-4737` with W40's final `w40-4738` f00aa03f0; code as cb69b8916), the same settings and suites as
  the run above (`C:/c5-64bit-work/0042/w43/rulea2.sh`, logs `C:/c5-64bit-work/0042/w43/ra2/logs/`), impl-0652's own
  run_all beside it (MC-B allowed: separate output folders, library read only). **`run_all: OK: 9 suite(s) passed
  (plain)`**: render-host 167/0/0, fow-scene 3/0/0, overlay-scene 3/0/0, shadow-ice-scene 6/0/0, water-scene 29/0/0,
  forest-scene 3/0/0, scenebundle-programs 4/0/0, scenebundle-py 22/0/0 (774 s under the load), **offline-equals-live
  17/0/0** (637 s). This is the run 4737's merge request cites. Main 3013cf459 (4738 itself and 0653's farm building
  records, no unit file; MC #9741: the rule A result stands) was merged after it; the builders ran again on that head.
- **The builders on the final head [C, run, workstation, 2026-11-21 20:34-21:11 EST]** (head 3cc6549e3, code 600f840df =
  the combined head + main 3013cf459; `C:/c5-64bit-work/0042/w43/allb43c.sh`, every step, builds and test runs, through
  `quietlock.py run`): `render/build.py --test` 108/108, `--asan -R c5r_unitanim` 0 ASan reports, server build OK,
  `test_scene_units` 79/79 plain and ASan (0 reports), `run_trigger_emitter_test --asan` OK, trigger scenes 19/19 plain and
  `--effects`, extras 13/13, unitfeed 19 passed, effect routes OK, `build_render_tests.py`, scenebundle tests, sceneshot
  `--smoke`, farm fields built and `test_farmfields_producer.py` 3 passed, `build_models_tests.py` and `test_models.py` 28
  passed, unit shadow built. (A first start at 20:27 was refused by impl-0655's quiet-machine lock; that script's last
  step, an ungated `test_scene_units` ASan run, ran 20:27:50-~20:36 inside 0655's lock window and was reported to it:
  test runs in a builder script go through `quietlock.py run` too.)
- **After main 99f0b12ab (0652's model layer and `srv_render_scene.cpp`) [C, run, 2026-11-21 21:14-21:48 EST]**, head
  4950b0600 (`allb43d.sh`, no library run, lead-0042's list): render ctest 108/108, unitanim ASan 0 reports, server build
  OK, `test_scene_units` 79/79 plain and ASan (0 reports), emitter plain and ASan OK, trigger scenes 19/19 plain and
  `--effects`, `build_render_tests.py` and `test_render_host.py` 79 passed, scenebundle tests and `test_recorder_host.py` 7
  passed, sceneshot `--smoke` built. **After main 2c87ab763 (0615's h18 fix in the live terrain producer; the shell's
  farm guard with an `exebuild/symbols/shell.tsv` change) [C, run, 21:50-21:59 EST]**, head ad4e87e85: `gen_table.py`
  `--identity` and the 26577625 carry first (418 of 418 symbols, local only, D202), then render ctest 108/108, server build
  OK, `test_scene_units` 79/79 (`allb43e.sh`). 0652 and the h18 fix draw nothing of the units; no library run after them
  (lead-0042; 0652 ran its own offline-equals-live 17/17).

## 36. The turn before a fight whose figures stand (ticket 4735, epic 0042, 2026-11-21): one render_config key, format 9 unchanged

Written 2026-11-21 by W45 of team-0042 (account B, branch `w45-4735`). Ticket 4735 turns on by default
`CombatParams::turn_first` (`research/05-graphics-composition/unit-combat-server.md` 10.11): a figure of a ranged, siege
or naval fight (or a city volley's target) facing more than 30 degrees away from its opponent first turns in place
playing its run (1400), then fights as before; those fights get 0.13-0.6 s longer. Rule A is answered as section 35 did.
- **Which bundles change [C, by section 35's scan of 2026-11-21 15:48 EST].** None: the 16 library bundles hold 0 combats,
  and turn_first acts only in fights the runner starts. Nothing they draw or hear changes.
- **The key [C, code].** Host key `render_test_units_turn_first` (TEST ONLY; "1" default, "0" no turn; other values
  refused: `render_test_units_turn_first=<v>: 0 or 1`) -> `UnitsOptions::turnFirst` -> `AnimRunner::setTurnFirst` ->
  `CombatParams::turn_first`. `RecorderRenderConfig` writes it into every bundle; the player (`sceneshot.cpp`, units on)
  sets it only when the key is "1", so **a bundle without the key (recorded before 4735) replays without the turn**, and
  prints `sceneshot: render_config: units turn_first T`. No member, no `FormatVersionChange`, no `gap:` note.
- **Tests [C, code].** `test_render_host.py` `test_config_refused`: "2" and "on"; `test_recorder_host.py`: the key "1" in a
  bundle the host recorded; `sceneshot_test.cpp` (units gap case): absent -> `units turn_first 0`, `--set` "1" -> 1.

## 37. The reform before a fight (tickets 4745 + 4747, epic 0042, 2026-11-22): one render_config key, format 9 unchanged

Written 2026-11-22 by W53 of team-0042 (account B, branch `w53-reform`). Tickets 4745 + 4747 turn on by default
`CombatParams::reform_first` (`research/05-graphics-composition/unit-combat-server.md` 18.17): before any fight, a unit whose
`ReformBeforeCombat` names its side (1 the attacker, 2 the defender) and that stands turned away from its opponent re-lays
its figures about its own centre facing the opponent (runs of 0.1-1.0 s, then the run stop) and nobody strikes before it
has; offline those fights get 1.0-1.4 s longer. Rule A is answered as sections 35 and 36 did.
- **Which bundles change [C, by section 35's scan of 2026-11-21 15:48 EST; 4744's W49 and lead-0042 10:38 the same].**
  None: the 16 library bundles hold 0 combats, and reform_first acts only in fights the runner starts. Nothing they draw
  or hear changes. Live, the default frames of such fights do change (figures move before the fight).
- **The key [C, code].** Host key `render_test_units_reform_first` (TEST ONLY; "1" default, "0" no reform; other values
  refused: `render_test_units_reform_first=<v>: 0 or 1`) -> `UnitsOptions::reformFirst` -> `AnimRunner::setReformFirst` ->
  `CombatParams::reform_first`. `RecorderRenderConfig` writes it into every bundle; the player (`sceneshot.cpp`, units on)
  sets it only when the key is "1", so **a bundle without the key (recorded before 4745) replays without the reform**,
  and prints `sceneshot: render_config: units reform_first R`. No member, no `FormatVersionChange`, no `gap:` note.
- **Tests [C, code].** `test_render_host.py` `test_config_refused`: "2" and "on"; `test_recorder_host.py`: the key "1" in a
  bundle the host recorded; `sceneshot_test.cpp` (units gap case): absent -> `units reform_first 0`, `--set` "1" -> 1.

## 38. HUGE_0350 and C4PORT recorded again after 0667's near-pair fix (ticket 0670, epic 0615, 2026-11-22)

Written 2026-11-22 by lead-0615 (account B, branch `task-0670-rerecord` from main b391a7a22). Evidence:
`server/evidence/0670/` (`build_0670.sh`, `rec_0670.sh`, `check_0670.sh`, `notes_0670.py`, `swap_0670.py`,
`baselines_0670.sh`). (Numbered 37 while on its branch: the `RETIRED.log` lines of the swap say
"offline-scene-harness.md 37"; they mean this section.) Marks: [C] checked on the data named, [L] seen in this ticket's live runs, [I] inferred.

**Why.** Ticket 0667 (`research/05-graphics-composition/live-terrain.md` 11.11) re-bakes in place a held near pair whose
content changed when the 512 pool has no new pair. The library's `C5H-0301-HUGE_0350_20261119-214920` and
`C5H-4980-C4PORT_0009_20261120-034655` had been recorded by a server with the old rule: their `near.bin` held pairs whose
content was behind (huge 36, C4PORT 24 textures "from recorded texels"), drawn stale live; the fixed player re-bakes
them, so offline-equals-live failed on both (colour only, about 1,045 px per frame). Recorded again with the fixed
server, both replay byte-equal and their `near.bin` holds no recorded texels (`0 textures from recorded texels, the
rest baked`: huge 40 pairs, C4PORT 60) [C].

**Build [C, 2026-11-22 10:57-11:12 EST].** server.exe `80713f7e...` and proxy `3a1b2458...` (release, `build_server.py`
on the 0.1b pins: the fork's source 1b87dd185 and its build_all release output in `C:/c5-64bit-work/keep/pins-0.1b`);
**the x64 DLL of the fork's branch `x64-render-events` addd57b8b** (x64-batch5 1fd51353e + the render-messages commit;
`tools/x64-dll/build.py --unlocked --config release` with the 0.1b pin's stubs; `render_events: on`; `4e6d2490...`). The
MC's order (#10254): so that feed.bin keeps the DLL's kind-1000/1001 inputs the Extras-B scenes read, as C4PORT's
recording of 2026-11-20 with `3e731989...` of the same commit (not byte-equal to that build: other stubs and tools).
The database, localization and Lua DLLs are the 0.1b candidate layout's (06d9e82); the composite is the candidate's
`cb781d20...` (shell/ and proxy/src unchanged since 06d9e82); readers: recorder_test `a3e06c9c...`, sceneshot
`074a74d9...`. **`pins.dll_lock.commit` in both scene.json names deps/dll.lock's pin 1b87dd185, not the DLL's commit;
`dll_sha256` is the DLL's** (the stamp reads the lock); each bundle carries a `note:` in `version_notes` saying so
(`notes_0670.py`; a note that begins neither `gap:` nor `incomplete` changes no reader).

**Recordings [L, instance c5h-a (C:, Goldberg, 4390913), RTX 4080].** Each bundle by the recipe of the one it replaces
(`rec_0670.sh`): huge = 4888's (`rec_4883.py`, options 12 / 13 on, `--save huge`, units by the TEST key
`render_units=1`, `--after 900`, settle 120), 11:15-11:20; C4PORT = 4980's (`rec_kit.py`, `ServerUnits = 1`, the kit
save `C5H-4980-KIT_0009` 74fc7428... copied in and removed, `--after 1800`, `kit.py camera c4_port`, settle 200,
`bundle_refs.py`), 11:32-11:38. Every run: stamp and checker `scenebundle: OK`, restore by hash rc 0, front end restored,
`provision.py verify` rc 0, no box. C4PORT's feed.bin: kind 1000 x 35 and kind 1001 x 2 at setup (as on 2026-11-20).

| Bundle (`F:/c5-64bit-work/scene-library/bundles/`) | Replaces (moved to `retired/`) | Span (8 frames, all dumped) | Members, bytes | Offline equals live (0:0) |
|---|---|---|---|---|
| `C5H-0301-HUGE_0350_20261122-111456_workstation` | `..._HUGE_0350_20261119-214920_...` | 893..943 | 56, 648,993,236 | byte-equal 8/8 |
| `C5H-4980-C4PORT_0009_20261122-113229_workstation` | `..._C4PORT_0009_20261120-034655_...` | 1850..1880 | 43, 366,356,114 | byte-equal 8/8 |

**A rule A gap of ticket 4785 found here: the units' shadows have no bundle field [C code, L difference image].** The
first C4PORT take (`..._20261122-112040`, 11:20-11:26, the product default `render_units_shadow=1`) replayed 0 of 8
frames equal: 14,551 of 3,603,694 pixels, colour only, depth equal, largest channel difference 69; the difference image
puts every differing pixel on the ground shadows of the warriors and catapults. Live, with `ServerUnits = 1` the mode
word's bit 4 is set, the EXE drops its unit mask draws from its shadow map and the server merges its own (render.log
`unitshadow 1s: ready=1 bit=1 merged=31`). `sb::ShadowRecord` (`srv_scene_bundle.h`, 344 B) has no field for the shadow
slot's `VPSHD_SLOT_UNIT_MASK_DROPPED` flag or the raw size, so the player's `ShadowFrame.unitMaskDropped` stays false and
`Scene::unitShadowStep` never merges: the replay draws no unit shadows. The huge bundle is not affected (its units come
from the TEST key: unit bit 0 live, `bit=0 merged=0`), nor are the 14 other bundles, all recorded before 4785 (the EXE
cast the unit shadows into the recorded map). **Every bundle recorded with `ServerUnits = 1`, unit shadows on and a unit
in view fails offline-equals-live this way** until the record carries the flag. (2026-11-22, ticket 4749: format version
10 carries it, section 40; a recording by a server with that change is needed.) The MC's decision (#10258): C4PORT
recorded again with `render_units_shadow=0` in the run's `server_cfg_extra` (TEST ONLY; the EXE casts the unit shadows,
the picture of the bundle it replaces; `render_config` names the 0 and a `note:` says why); the format field is a new
child of epic 0042. The rejected take and its difference images: `F:/c5-64bit-work/0615/0670/rejected/` (outside the
library).

**The swap [C, 2026-11-22 11:44 EST].** `swap_0670.py`: the two old bundles to `retired/` (one `RETIRED.log` line each,
kept), the two new ones into `bundles/`, `index.json` kept as `index-before-0670.json` and written again (16 bundles,
SHA-256 `364910d6...`); then scenerun's baselines for the two (`baselines_0670.sh`: pass 1 makes them, pass 2 compares)
and `scenelib.py sync` of the C: copy. The rule A run on main after the swap: ticket 0670's log.

**Not verified (0670).** The new bundles were not replayed on oldpc. The render-events DLL `4e6d2490...` is not the
product's (the 0.1b pin has no render messages); with every extra off nothing in the frames depends on the DLL's events
[I: the extras that read kinds 1000/1001 are visual additions, off by default].

## 39. Thirteen bundles recorded again after 0665 hid the open sea's uncached tiles (ticket 0672, epic 0615, 2026-11-22)

Written 2026-11-22 by lead-0615 (account B, branch `task-0672-rerecord-depth` from `task-0670-rerecord` @8aa9e071b =
main 2715e2efb + 0670). Evidence: `server/evidence/0672/` (`rec_0672.sh`, `check_0672.sh`, `swap_0672.py`,
`baselines_0672.sh`; 0670's `build_0670.sh` and `notes_0670.py` reused). Marks as in 38.

**Why.** Ticket 0665 (`research/05-graphics-composition/fog-after-camera-jump.md` 6, on main 2715e2efb) skips an
uncached non-land tile in the terrain pass: the depth under the open sea stays at the far plane (as stock), the colour
is unchanged. The dumps of 13 library bundles held the old depth, so offline-equals-live failed on them with main's
player (depth only: 3,100 to 733,563 values per frame; 0665's acceptance (2)). Unchanged by 0665 and kept:
`TINY_0009_20261119-123934`, `FOG_0008_20261119-124546`, `C2BWONDER_0009_20261120-032950`.

**Build [C, 2026-11-22 12:39-12:43 EST].** As 38, from this branch (main 2715e2efb + 0670): server.exe `da915258...`,
proxy `40cfdded...`, the render-events DLL `4e6d2490...` (38, reused), readers recorder_test `04cc7f38...`, sceneshot
`550b539e...` (one LTO link, the server's).

**Recordings [L, 2026-11-22 12:44-13:29 EST, two instances in parallel: c5h-a and c5h-b (C:, Goldberg, 4390913)].**
Each by the recipe of the bundle it replaces (`rec_0672.sh`): huge as 38; the four scenes by 4883's recipe
(`rec_4883.py --save scn`, the Tiny save's copy `C5H-0301-SCNB_0009`, options 12 / 13 on, units by the TEST key,
`--after 1500`, settle 200, `runner.py camera tools/scenario/scenarios/scenes_tiny.json --frame <scene>_c1` beside the
run); the eight kit cases by 4980's (`rec_kit.py`, `ServerUnits = 1`, `--after 1800`, settle 200, `kit.py camera
<case>`, `bundle_refs.py` with the stock shot, `feedkinds.py`: kind 1000 x 35, kind 1001 x 2 in every one) with
`render_units_shadow=0` in the run's `server_cfg_extra` (38: the unit-shadow gap). Every camera was set (zoom 15.0, the
city and c2a 17.0) 120-176 s after the start; every run: stamp and checker OK, restore by hash rc 0, front end restored,
`provision.py verify` rc 0, no box. Each scene.json carries the `note:` lines of 38 (`notes_0670.py`).

| Bundle (`bundles/`) | Replaces (moved to `retired/`) | Instance, recorded (EST) |
|---|---|---|
| `C5H-0301-HUGE_0350_20261122-124422_workstation` | `..._HUGE_0350_20261122-111456_...` (38) | c5h-a 12:44-12:51 |
| `C5H-0301-SCNB_0009_20261122-125114_workstation` (coast_c1) | `..._SCNB_0009_20261119-125241_...` | c5h-a 12:51-12:57 |
| `C5H-0301-SCNB_0009_20261122-125725_workstation` (river_c1) | `..._SCNB_0009_20261119-125843_...` | c5h-a 12:57-13:03 |
| `C5H-0301-SCNB_0009_20261122-130330_workstation` (city_c1) | `..._SCNB_0009_20261119-130448_...` | c5h-a 13:03-13:09 |
| `C5H-0301-SCNB_0009_20261122-130935_workstation` (highlights_c1) | `..._SCNB_0009_20261119-131111_...` | c5h-a 13:09-13:15 |
| `C5H-4980-C7INTERCEPT_0009_20261122-131558_workstation` | `..._C7INTERCEPT_0009_20261120-024541_...` | c5h-a 13:15-13:22 |
| `C5H-4980-C1MOUNTAIN_0009_20261122-124422_workstation` | `..._C1MOUNTAIN_0009_20261120-032355_...` | c5h-b 12:44-12:51 |
| `C5H-4980-C2ANATURAL_0009_20261122-125128_workstation` | `..._C2ANATURAL_0009_20261120-030410_...` | c5h-b 12:51-12:57 |
| `C5H-4980-C3AGARRISON_0009_20261122-125744_workstation` | `..._C3AGARRISON_0009_20261120-033534_...` | c5h-b 12:57-13:04 |
| `C5H-4980-C3BGARRISON2_0009_20261122-130405_workstation` | `..._C3BGARRISON2_0009_20261120-034114_...` | c5h-b 13:04-13:10 |
| `C5H-4980-C4PORT_0009_20261122-131013_workstation` | `..._C4PORT_0009_20261122-113229_...` (38) | c5h-b 13:10-13:17 |
| `C5H-4980-C5AIR_0009_20261122-131703_workstation` | `..._C5AIR_0009_20261120-035243_...` | c5h-b 13:17-13:23 |
| `C5H-4980-C6CARRIER_0009_20261122-132303_workstation` | `..._C6CARRIER_0009_20261120-025123_...` | c5h-b 13:23-13:29 |

**Checks [C, 13:29-13:41 EST].** All 13 copied to `staging-0672/bundles` by hash (4883's `copy_f.sh`), checker OK,
**8 of 8 frames byte-equal at 0:0 with main's player, every one**; scenerun index and verify of the staging folder OK.
Two recordings on one GPU at once changed nothing a replay compares (each bundle replays against its own dumps).

**The swap [C, 13:38-13:40 EST].** `swap_0672.py`: the 13 old bundles to `retired/` (one `RETIRED.log` line each, kept),
the 13 new ones into `bundles/` (the scenes keyed by the camera frame in `staging-0672/staged.txt`), `index.json` kept as
`index-before-0672.json`. The script as first run stopped with a NameError after the moves, before writing `index.json`
(a module constant lost when it was derived from 0670's); its last step was finished at once by hand with scenerun's
`write_index` (16 entries, every new entry's scene.json SHA-256 equal to the staging index's; `index.json` SHA-256
`63534299...`) and `staging-0672/` removed; the script is fixed. Then scenerun's baselines for the 13 and `scenelib.py
sync` (`baselines_0672.sh`); the rule A result: ticket 0672's log.

**Not verified (0672).** No oldpc replay. (2026-11-22, 4749 W66: the eight kit bundles of this table are retired; their
format-10 recordings with the units' shadows on replaced them, section 41.)

## 40. Format version 10: shadow.bin carries the unit-mask-dropped flag and ShadowRaw's size; format 9 still read (ticket 4749, epic 0042, 2026-11-22)

Written 2026-11-22 by W62 (account B, branch `w62-4749` from main 141fc777a) for lead-0042. Closes the code side of the
rule A gap of 38 (4785's units' shadow). Marks: [C] code and offline tests on this branch, [I] inferred. Offline only: no
game instance, no LTO link, no library re-record (the later stage, below).

**The change [C].** `sb::ShadowRecord` (`srv_scene_bundle.h`) gains `rawWidth`, `rawHeight` (ShadowRaw's size of the
slot's Present: `ShadowFrame.rawWidth/rawHeight`) after `iceLoadSeq`, 344 -> 352 bytes, and the flag `kUnitMaskDropped =
8` (the bound triple's slot carried `VPSHD_SLOT_UNIT_MASK_DROPPED`: `ShadowFrame.unitMaskDropped`). shadow.bin's file form
goes 1 -> 2 (`MemberKindVersion(Shadow)`), `kFormatVersion` 9 -> 10 with its `FormatVersionChange(10)` line.
- The recorder (`SceneRecorder::afterDrawSeamG`, `srv_scene_recorder.cpp`) writes the flag and the raw size from the
  scene's `lastShadowFrame` (the `.shtx` still holds the tap's own, unmerged map).
- The player's `FileShadowSource::acquire` (same file) hands both back to the scene; `Scene::unitShadowStep`
  (`srv_render_scene.cpp`, unchanged) then merges the server's unit mask on exactly the frames whose record carries the
  flag, by the live rule `UnitShadowWanted` (switch, unit scene, flag, quality 1-3, a texture, a raw size).
- `sceneshot` sets `SceneOptions.unitsShadow` from `render_config` `render_units_shadow` ("0" off; "1" or absent: the
  server's default 1) and prints `sceneshot: render_config: units shadow N`. Before, it was always the default 1; no
  format-9 record carries the flag, so this changes no format-9 picture.
- `LoadSeamGMembers`' note line names the flagged frames and the form: `shadow.bin: N records, M shadow map file(s), K
  ice mesh(es), J with the unit mask dropped (form F)`.

**Format 9 bundles are still read (rule B as changed here; lead-0042 #10465).** For the first time a format raise keeps
the previous version readable. The refusal floor is `kOldestReadFormatVersion = 9`: `ParseManifest` refuses a version
below it as OlderVersion ("format_version V is older than 9 (...; changed since: ..." up to `kFormatVersion`); 9 and 10
are read. `MemberKindVersionFor(kind, format_version)` gives the file form a bundle of that format carries (shadow 1
below 10, else `MemberKindVersion`); the manifest's member version and shadow.bin's header must say it (BadVersion
otherwise: form 2 in a format-9 bundle, form 1 in format 10). `ParseShadow(b, out, res, formatVersion)` reads form 1 as
344-byte records with `rawWidth = rawHeight = 0` and the flag never set (form 1 still refuses flags above bit 2), which is
what the player drew from them before. Why read-compatible: the change only adds data a format-9 recording could not
hold, and its absence means "no merge", the old picture. So the 16 format-9 library bundles need no re-record for this
change (they need one only to gain unit shadows, below). Rule B's "a bundle of an older format is recorded again" now
means a format below `kOldestReadFormatVersion`.

New refusals of form 2 (BadRecord): `kUnitMaskDropped` without `kShadowBound` or `kShadowQ0`; a raw size above 16384 or
with one side 0; flags above bit 3. A record size other than 352 in form 2: WrongSize.

**The regression case [C, 2026-11-22].** `recorder_test.cpp` `TestUnitShadowMask`: a two-frame recording in the host's
order (`afterDraw`, then `afterDrawSeamG`) with a WARP R16_UNORM 4 x 4 map at quality 3, frame 1's triple with
`unitMaskDropped` and raw 160 x 120, frame 2's without; then `LoadSeamGMembers`, the player source's `acquire` per frame
and the scene's rule `UnitShadowWanted`. It uses only API that main has, so it compiles on main:
- main 141fc777a + this test (16:49-16:51 EST): `RESULT: 253 passed, 3 failed`: "frame 1 replayed: have 1, unit mask
  dropped 0 (live 1), raw 0 x 0 (live 160 x 120)" and "frame 1: the scene's merge rule says 0, live 1 (the EXE cast its
  units (slot flag clear)): REPLAYED WITHOUT THE UNIT GROUND SHADOWS".
- this branch (16:52-17:04 EST): `RESULT: 256 passed, 0 failed`.

`bundle_format_test.cpp` `TestShadowUnitMask`: the constants; a form-2 round trip (bound + flag, q0 + flag, empty); form 1
read in format 9 (raw 0, flag clear); form 2 in format 9 and form 1 in format 10 refused (BadVersion); the new refusals;
the manifest matrix (format 10 + form 2 and 9 + 1 read, 10 + 1 and 9 + 2 BadVersion, 8 OlderVersion): `RESULT: 1197
passed, 0 failed`. `sceneshot_test.cpp`: the older-format case names versions 8-10 and "older than 9", and the same
synthetic bundle rewritten as format 9 (it has no shadow.bin) replays with exit 0 and "(format 9, save".

What the regression case does not do: draw the merged map. That needs the unit scene with game art and the install EXE's
ShadowShader programs; `Scene::unitShadowStep` and `UnitShadow::apply` are 4785's, tested in `server/tests/render/shadow/`
([unit-combat-server.md](../05-graphics-composition/unit-combat-server.md) 13.2) and unchanged here. The flag and the raw
size reaching the scene's rule is the whole of what 38 found missing.

**Old bundles replay byte-identically [C, 2026-11-22 17:36-17:42 EST, RTX 4080].** Four format-9 library bundles, copied
from the C: library copy into the worker's folder, replayed by main's sceneshot (141fc777a, `a982bff1fa3b7e14...`) and
this branch's (`aa72932000d98c24...`), each `--dumped --compare --tolerance 1:1 --asset-root
C:/c5-64bit-work/scene-asset-root/mods548-vt`: every PNG SHA-256 equal between the two players, 8 of 8 per bundle, 32 of
32, and both players byte-equal to the server's own dumps (colour and depth, 0 of 3,603,694) in every frame. The four
cover each `render_units_shadow` state of the library: `SCNB_0009_20261122-130330` (city_c1, the key 1), `TINY_0009_20261119-123934`
(the key absent), `C2BWONDER_0009_20261120-032950` (kit, absent), `C4PORT_0009_20261122-131013` (kit, 0: the branch's
player prints `units shadow 0`). Every one: `shadow.bin: 8 records, 8 shadow map file(s), 1 ice mesh(es), 0 with the unit
mask dropped (form 1)`. Script `C:/c5-64bit-work/0042/w62/ident62.sh` (its copy and summary in
`server/evidence/0042/4749/`; the pictures stay local).

**Every builder [C, 2026-11-22 16:48-17:25 EST].** Runbook 2.10 "the rest of `server/src/render/`" = R13, each through
`quietlock.py run --who w62-0042`, the game copy outside the pool, script `C:/c5-64bit-work/0042/w62/allb62.sh` (from
lead21's `allb57.sh`, outside git): render lib (77 s); `build_scenebundle_tests.py --run`: bundle_format_test 1197/0,
recorder_test 256/0, sources_test 24/0, sceneshot_test 244/0 (WARP and the RTX 4080; its first run found the stale
`"format_version": 9` string, its second that the format-9 replay must not write into the refusals' `--out`; both
fixed, 17:42-17:52 EST); `build_sceneshot.py --smoke` OK;
`build_server.py` release, no LTO (144 s, server.exe `bdeaffae...`); `build_render_tests.py` OK; `test_render_host.py` 89
passed; `test_recorder_host.py` 7 passed; the farmfields, forest, fow and overlay-scene builds OK; `build_scene_shadow.py`
OK and `test_scene_shadow.exe` 13 cases PASS (its shadow.bin uses `sizeof(ShadowRecord)`; source unchanged);
`build_units_tests.py` + `test_scene_units.exe` 79/79; `build_units_extras_tests.py` + `test_units_extras.exe` 13/13;
`build_unitfeed.py` + `test_unitfeed.py` 19 passed (after the change below; 18 passed 1 failed before it). `test_unitfeed.py::test_bundle_refusals` wrote `cur - 1` as
the refused version; it now reads `kOldestReadFormatVersion` from the header and writes that less one (lead-0042 #10465).

**The later stage (not done here; a separate START-REQUEST).** (1) A live recording with `ServerUnits = 1`,
`render_units_shadow=1` and units in view (the kit recipe of 38 and 39, `rec_kit.py`, without
`EXTRA_CFG=render_units_shadow=0`) by a server built from this change, replayed offline-equals-live at 0:0: the first
bundle whose replay merges the unit mask (`J with the unit mask dropped` above 0 in sceneshot's note). (2) The eight kit
bundles of 39 (`C1MOUNTAIN` to `C7INTERCEPT`, recorded with `render_units_shadow=0`) recorded again with unit shadows on,
so that the library shows the product's picture; the other eight need nothing (their units come from the TEST key, unit
bit 0 live, or were recorded before 4785: no flag). (3) The rule A run on main after the swap. From 39's times [I]: about
7 min per kit bundle on one instance (two instances in parallel: about 30 min for eight), about 13 min checks and swap,
rule A 11-15 min.

**Not verified (4749).** No oldpc replay. (Corrected 2026-11-22 by W66: the live recording at format 10 is done and the
merged picture offline is seen on real bundles, section 41; this line said "No live recording at format 10 yet".)

## 41. 4749's later stage: the kit bundles recorded at format 10 with unit shadows (ticket 4749, epic 0042, 2026-11-22)

Written 2026-11-22 by W66 (account B, branch `w66-4749` from main a30005c33, 4749's offline stage merged) for lead-0042.
Evidence: `server/evidence/0042/4749/w66/` (`build66.sh`, `rec66.sh`, `check66.sh`, `notes66.py`, `swap66.py`,
`baselines66.sh`, and the summaries of each step, the swap's dry run in `check_summary.txt`). Marks as in 38.

**Why.** Section 40 gave the format the field; the eight kit bundles of 39 had been recorded with `render_units_shadow=0`
(38's gap). Recorded again with the product default (units' shadows on) by a server with 4749, the library shows the
product's picture: the EXE drops its unit mask draws from its shadow map, the server merges its own, and the replay does
the same from shadow.bin's flag.

**Build [C, 2026-11-22 18:24-18:32 EST], no LTO link.** From this worktree (main a30005c33) on the 0.1b pins
(`keep/pins-0.1b`): `build_server.py --config release` (120 s; as W62's `allb62.sh` phase 1): server.exe
`e08d8ce9cea2acecd11b66e1a820d06d308793636d04cf2c08d95a93ce7c098d`, proxy `faf9c406...`, vp64helper `c26b9b87...`; the
database, localization, lua DLLs and civ5save from that build's `bin/` (`9c37b39e...`, `35863ede...`, `30724d6d...`,
`409422ef...`). **The x64 DLL: the fork's branch `x64-render-events` addd57b8b (a shallow clone), `tools/x64-dll/build.py
--unlocked --config fast` with the 0.1b pin's stubs (release's compile flags without -flto; link 1.2 s; `render_events: on`):
`cde0a7811bf75b9a59d7ee117ddfa0b77731ea9d85deec18155072b5d0d57d0c`** (lead-0042 #10510: 38's render-events DLL `4e6d2490...`
was no longer on disk, lead12's folder cleared, and none of 432 x64 DLLs in the C: and F: work folders since 2026-11-19 had
its hash; its release build is an LTO link). The composite `3f7e8471...` is W64's shell build of main f2e97eecf (`shell/` and
`proxy/src` unchanged up to a30005c33). Readers from this worktree: recorder_test `f03a60cf...`, sceneshot `97018e3c...`.
`pins.dll_lock.commit` names deps/dll.lock's pin 1b87dd185, `dll_sha256` this DLL's (as 38).

**Recordings [L, 2026-11-22 18:31-19:05 EST, c5h-a and c5h-c (C:, Goldberg, EXE stamp 0x546CD0A8 = 4390913 checked
before each launch)].** Each by 4980's recipe as 39 (`rec_kit.py`, `ServerUnits = 1`, the kit save `C5H-4980-KIT_0009`
74fc7428... copied in and removed, `--after 1800`, settle 200, `kit.py camera <case>`, `bundle_refs.py` with the stock
shot), **without a `server_cfg_extra`**: `render_config` `render_units_shadow` "1". C4PORT first and alone (part 1), then
c5h-a C1-C3B and c5h-c C5-C7 in parallel. Every run: stamp and checker `scenebundle: OK`, restore by hash rc 0, front end
restored, `provision.py verify` rc 0, no box, `bundle_refs: OK`; every camera set 122-161 s after the start (zoom 15.0,
c2a 17.0). Live render.log in every run: `unitshadow 1s: ready=1 bit=1 merged=0..31 ... flag_frames=5059..6432
not_merged=0 mask_fails=0 refused=0 raw=2554x1411` (the server merged its unit mask on every flagged frame). feed.bin: kind
1000 x 35 and kind 1001 x 2 in every one (as 39; the fast-built DLL gives the same render events), the other kinds
identical across the eight.

**Part 1, the live recording [C, 18:38 EST]: the merged picture offline is seen.** `C5H-4980-C4PORT_0009_20261122-183155`
(warriors and catapults in view) replayed by main's sceneshot at 0:0: `render_config: units shadow 1`, `shadow.bin: 8
records, 8 shadow map file(s), 1 ice mesh(es), 8 with the unit mask dropped (form 2)`, 8 of 8 frames byte-equal (colour and
depth, 0 of 3,603,694). 38's first C4PORT take at format 9 under the same conditions differed by 14,551 pixels per frame on
the units' ground shadows; at format 10 the replay merges them as live. Every one of the eight below prints the same note:
**8 with the unit mask dropped** in every bundle.

| Bundle (`F:/c5-64bit-work/scene-library/bundles/`) | Replaces (moved to `retired/`) | Instance, recorded (EST) | Span (8 frames, all dumped) | Members, bytes | Offline equals live (0:0) |
|---|---|---|---|---|---|
| `C5H-4980-C1MOUNTAIN_0009_20261122-183905_workstation` | `..._C1MOUNTAIN_0009_20261122-124422_...` (39) | c5h-a 18:39-18:45 | 2185..2225 | 43, 367,303,451 | byte-equal 8/8 |
| `C5H-4980-C2ANATURAL_0009_20261122-184537_workstation` | `..._C2ANATURAL_0009_20261122-125128_...` | c5h-a 18:45-18:52 | 2753..2794 | 43, 366,052,115 | byte-equal 8/8 |
| `C5H-4980-C3AGARRISON_0009_20261122-185217_workstation` | `..._C3AGARRISON_0009_20261122-125744_...` | c5h-a 18:52-18:58 | 2042..2078 | 43, 366,960,141 | byte-equal 8/8 |
| `C5H-4980-C3BGARRISON2_0009_20261122-185851_workstation` | `..._C3BGARRISON2_0009_20261122-130405_...` | c5h-a 18:58-19:05 | 1940..1973 | 43, 367,082,832 | byte-equal 8/8 |
| `C5H-4980-C4PORT_0009_20261122-183155_workstation` | `..._C4PORT_0009_20261122-131013_...` | c5h-a 18:31-18:38 | 1817..1847 | 43, 366,342,756 | byte-equal 8/8 |
| `C5H-4980-C5AIR_0009_20261122-183906_workstation` | `..._C5AIR_0009_20261122-131703_...` | c5h-c 18:39-18:46 | 2366..2406 | 43, 367,277,838 | byte-equal 8/8 |
| `C5H-4980-C6CARRIER_0009_20261122-184639_workstation` | `..._C6CARRIER_0009_20261122-132303_...` | c5h-c 18:46-18:53 | 3281..3314 | 43, 366,690,252 | byte-equal 8/8 |
| `C5H-4980-C7INTERCEPT_0009_20261122-185318_workstation` | `..._C7INTERCEPT_0009_20261122-131558_...` | c5h-c 18:53-18:59 | 2142..2178 | 43, 366,699,115 | byte-equal 8/8 |

**The changed members, named for rule A (the MC's condition #10497).** Against the format-9 bundle each replaces:
`render_config` `render_units_shadow` 0 -> 1; `shadow.bin` form 1 -> 2 (352-byte records, the flag on all 8 frames, raw
2554 x 1411); `shadow/*.shtx` (the tap's own map, now without the EXE's unit casts: bit 4 of the mode word, 38 [C]); `dumps/*`
(colour: the units' ground shadows merged by the server); `format_version` 9 -> 10. Each scene.json carries two `note:`
lines (`notes66.py`, before the staging index): the DLL (fork sha, `--config fast`, `cde0a781...`, #10510) and the unit
shadows with this list of changed members. A note that begins neither `gap:` nor `incomplete` changes no reader.

**Checks [C, 19:05-19:11 EST].** All 8 copied to `staging-4749/bundles` by hash (4883's `copy_f.sh`, F: under 50 ms),
checker OK, notes added, **8 of 8 frames byte-equal at 0:0 with main's player, every one**, feed kinds as above, scenerun
index and verify of the staging folder OK, and `swap66.py --dry`: the full path printed with every precondition checked
(16 bundles with the 8 old ones, no target present, format 10 with near pairs and unit shadows on, the staging index's
SHA-256s, scenerun's `write_index` loaded).

**The swap [C, 2026-11-22 19:11:02 EST].** `swap66.py`: the 8 old bundles to `retired/` (one `RETIRED.log` line each,
"offline-scene-harness.md 41"), the 8 new ones into `bundles/`, `index.json` kept as `index-before-4749.json` and written
again (16 bundles, SHA-256 `db61167f...`, the new entries equal to the staging index), `staging-4749/` removed; one run,
rc 0. `scenelib.py sync` of the C: copy 19:13:40-19:14:11 (rc 0; check OK, 16 bundles, 1226 files, 8.38 GB). The other
eight library bundles are unchanged (40: their units come from the TEST key or were recorded before 4785).

**Baselines and the C: copy [C, 19:11-19:37 EST].** `baselines66.sh` on the F: master: scenerun pass 1 made the 8 new
bundles' baselines (72 shots, keyed by bundle id), pass 2 (`--no-baseline-write`) compared: every shot "regression
equal", every dumped frame "within" P1; then `scenelib.py sync` (19:37:27; 1306 files, 16 bundles, 8.84 GB) and check OK.
The 8 retired bundles' baselines stay under their own ids.

**Rule A [C, 2026-11-22 19:41:05-19:58:40 EST], PASS.** `rulea66.sh` = runbook 2.4: `run_all.py --only
offline-equals-live` on branch `w66-4749` @0c2edb15b (main a30005c33 + this section and evidence, no compiled change; the
run built c5render, the checker and sceneshot from it), `C5_SCENE_LIBRARY` the checked C: copy, the shared asset root,
the 4390913 copy outside the pool: `SUITE offline-equals-live PASS 17 passed, 0 failed, 0 skipped 797 s`. Main's 0675
(the composite's lockstep bound in `shell/common/vp_frame.h` and `shell_composite.ini`, merged after a30005c33) was not
in that build; nothing under `server/` or `render/` reads it, so the offline players are the same. The test compares each
bundle with its own dumps, so no rule A baseline changed by itself: the 8 kit bundles' pictures changed with their new
recordings (the units' ground shadows, members named above) and their scenerun baselines were made new by bundle id.

**Not verified (4749 later stage).** No oldpc replay of the new bundles. The fast-built DLL `cde0a781...` is not the
product's (the 0.1b pin has no render messages); with every extra off nothing in the frames depends on the DLL's events
[I, as 38]. The fidelity column against the stock shots (`stock/`) was printed by scenerun (e.g. c1_mountain median 4,
p95 58) and not compared with the retired bundles' numbers.

## 42. Format version 11: `water_frames.bin`, the water copy drawn per frame (ticket 0622, 2026-11-23)

Written 2026-11-23 by the 0622 implementer (account B, branch `task-0622-water-copy-switch` from main d828158fa). Closes
epic 0615's rule A gap (`live-terrain.md` 9.4, "Gap (rule A), the water copy after `Waves_Reload`"). Marks: [C] code and
offline tests on this branch, [L] the live recording and its replay, [I] inferred.

**The gap [C, code read].** A live land/water change makes the gate call `Waves_Reload` and raise the water tap's
`requestSeq`; the server then accepts a later copy of the same load, which the recorder writes as
`water/load<NNNN>-c<copySeq>.wtap` (`onWaterImage`). Before this change the player took the load's last copy (the
highest `copySeq`) for every frame. A span holding the reload therefore drew the new waves on its earlier frames too, and
when the two copies hold different wave counts the water clock recorded at the span's start (the first copy's) was
refused: "setClockState refused: 531 waves given, the image in use has 528" (W6P03, `live-terrain.md` 9.8 R1).

**When the live render thread switches [C, code read].** `Scene::poll` (`srv_render_scene.cpp`) calls
`WaterScene::poll` -> `WaterTapReader::poll`. For a copy that passes every check the reader calls its image sink (the
recorder's `onWaterImage`) and returns true, and `WaterScene::poll` then calls `useImage`. The host
(`srv_render_host.cpp`) runs `scene.poll()` before the frame's `beforeDraw` / `Scene::draw` / `afterDraw`. So the copy a
frame is drawn with is the newest copy the sink saw before that frame's `afterDraw`. `useImage` of another copy of the
same load keeps the water clock's start time and start values (ticket 0586) and re-reads the waves from the new image.

**The member [C].** `water_frames.bin` (kind `water_frames`, magic `C5WF`, file form 1): the 16-byte header, u32
`recordBytes` = 56, then one `WaterFrameRecord` per record of `frames.bin`, same order and frame ids
(`srv_scene_bundle.h`): `frameId` i64, `flags` u32 (bit 0 `kWaterCopyInForce`), `loadSeq` u32, `copySeq` u32, `reserved`
u32 (0), `wtapSha256` 32 bytes (the SHA-256 of the `.wtap` member, which is the manifest's `sha256` of that member). A
frame drawn before any copy was accepted has flags 0 and zeros. `kFormatVersion` 10 -> 11 with `FormatVersionChange(11)`
and `kWaterFramesFormatVersion = 11`; `kOldestReadFormatVersion` stays 9.
- **Recorder** (`srv_scene_recorder.cpp`): `onWaterImage` hashes the `.wtap` bytes it writes and keeps them as the copy in
  force; `afterDraw` appends one record per span frame; `finish` writes the member beside `frames.bin` (always, also
  when no copy came).
- **Checker** (`CheckBundle`): parse refusals (BadRecord): flags above bit 0, reserved not 0, a record without a copy
  but with a load, copy or hash, a copy without its hash, frame ids not increasing, a copy followed by none, a copy of
  the same load whose `copySeq` goes back; record size not 56 (WrongSize), a partial record (Truncated). Cross-checks:
  the record count (WrongSize) and frame ids (BadRecord) are `frames.bin`'s; a named hash that is no `water_tap` member
  (MissingMember); a member whose block has another load or copy seq than the record (BadRecord); a format-11 bundle with
  `water_tap` members and frames but no `water_frames.bin` (MissingMember); the member in a bundle below format 11
  (BadValue).
- **Player** (`sceneshot.cpp`): with the member and the full water mode, it loads record 0's copy (`LoadWaterImageMember`
  -> `WaterScene::loadImage` -> `useImage`), then `water_clock.bin`, and before each frame's `scene.poll()` it loads the
  copy that frame's record names when it differs from the one in use, through the same `loadImage` (the live
  `useImage`; a same-load copy keeps the clock). Lines: `sceneshot: water image <member> (copy N) in use from frame F
  (water_frames.bin)` and `... (load L copy N) in use from frame F, as live (water_frames.bin)`. Without the member
  (formats 9 and 10) nothing changes: the load's last copy for every frame.

**Tests [C, results below].** `bundle_format_test.cpp` `TestWaterFrames` (constants, round trip, each parse refusal,
the checker's refusals through the child and in process, a two-copy span passing, format 10 without the member read);
`BuildFull` now writes the member. `recorder_test.cpp` `TestWaterCopies`: a span of four frames in the host's order,
frame 12 before any copy, copy 1 accepted before frame 13, copy 2 of the same load before frame 14: the records name
none, copy 1, copy 2, copy 2 by their members' hashes. `sceneshot_test.cpp` `CaseWaterCopies`: a live side that takes
copy 2 (2 coast waves; copy 1 has 4) before frame 102's poll; replayed with the member every dumped frame (100, 103, 105)
is byte-equal; the same live run written as format 10 is refused at the water clock ("refused by the water scene (4
waves", W6P03's failure), and records naming copy 1 for every frame differ at 103 and 105 (the case's teeth). The
older-format refusal names version 11; the format-9 copy drops the member first.

**Results.**
- **The four scene-bundle programs [C, 2026-11-23 16:58-18:00 EST, this branch, render lib `C:/c5-64bit-work/0622/allb/out/rb`,
  the pure_vp game copy outside the pool (build 4390913), WARP and the RTX 4080].** `build_scenebundle_tests.py --run`:
  bundle_format_test 1260 passed 0 failed; recorder_test 268/0; sources_test 24/0; sceneshot_test 274/0 (warp PASS, hw
  PASS). `CaseWaterCopies` on the RTX 4080: with the member, frames 100, 103 and 105 colour and depth EQUAL (0 of 19,200),
  the switch line at frame 102 ("recorded image of load seq 1 copy 2 loaded ... waves 1 chunks / 2 waves"); format 10:
  exit 8 at the water clock; copy 1 named throughout: frame 100 equal, 103 and 105 differ (exit 10). Driver
  `C:/c5-64bit-work/0622/allb.sh` (copy in `server/evidence/0622/`). Two test-side faults found on the way and fixed:
  `Writer::appendStart` takes only the listed stream kinds (water_frames added), and `Writer::finish` writes
  `kFormatVersion` whatever the manifest says (the test rewrites scene.json to make its format-10 bundle).
- **Live recording and its replay [L, 2026-11-23 20:00-20:44 EST, c5h-a (MC-B's grant), Tiny, build 4390913].** The
  details and numbers are in `live-terrain.md` 9.4 ("The water copy after `Waves_Reload`"). In one line: Q2W40 (40
  frames) draws copy 1 in 1853-1867 and copy 2 from 1868; offline every frame before the live terrain generation is
  byte-equal (W40 11, P03 11, P01 8, P02 16 of 16), where main's player cannot replay these bundles (copy-1 clock
  refused); from the generation frame on the frames differ in the changed coast's land tiles only, the named re-bake
  latency gap (`live-terrain.md` 9.8), and the differing set does not move across the water switch. MC #12190 accepted
  that reading of acceptance (3). Commands: `server/evidence/0622/live.sh` (smoke_4914.sh with this branch's tree),
  `waves40.sh` (one 40-frame process), `replay.sh`, `switchmask.py`.
- **How to record a span that holds a `Waves_Reload` [L].** `smoke_4914.sh`'s 16-frame spans with every frame dumped
  last about 4 s; the reload's copy arrives about 1 s after the change (P01's after its last frame, P03's at its last).
  A 40-frame span with the change after 3 dumped frames (`waves40.sh`) puts the switch in the middle.
- **Rule A [C, 2026-11-24 02:27-03:06 EST, MC-B's slot, at 806e2da55 = this branch + main 15bf0cb94].** `run_all`
  render-host 205 passed, scenebundle-programs 4/4, scenebundle-py 28/28, offline-equals-live 17 passed, 0 failed, 0
  skipped: the 16 library bundles (formats 9 and 10) replay as before. run_all's exit 1 came from its scene-library
  check after the suite only (exit 3221225794 = 0xC0000142, a short workstation-wide "no process can start" state at
  02:55-03:00); `scenelib.py check --copy` rerun alone: OK. No library re-record is needed for format 11 (formats 9 and
  10 stay readable); a format-11 member with a `Waves_Reload` inside its span is a candidate for the library once the
  re-bake latency gap (`live-terrain.md` 9.8) is closed or the member's span ends before the generation.

## 43. The first live bundle with fights in its span does not replay equal to live (ticket 0742, epic 0042, 2026-11-25)

Written 2026-11-25 04:3x EST by w142-0042 (branch `w142-0742-scene`). `[L]` live on c5h-a (build 4390913), offline on the
workstation. Details and commands: `research/05-graphics-composition/unit-combat-server.md` 18.43.3.
- Bundle `C5H-0742-OVERTAKE_0009_20261125-041228_workstation` (format 11; `--after 1500 --frames 450 --dump every:50`;
  two melee fights started by Lua at the span's first frame, one of them held behind its attacker's walk): the recorder's
  checker OK; unitfeed replays both fights (2 started, 0 refused). `sceneshot --dumped --compare --tolerance 1:1`: the
  span's first frame is byte-equal; every dumped frame from 1.7 s after the orders differs on the fighting pairs only
  (33-88 k colour pixels, depth up to 0.0053), up to 13 s later, after both fights ended: the same figures in a different
  idle pose (clip phase). Before this no library bundle held a fight in its feed (section 35: 0 combats in each).
- So a bundle with a fight in its span cannot join the library until the replay's fight and clip times equal live; this
  one is not in the F: master (it would fail `offline-equals-live`) and stays in the worker's folder. Cause open.
- Recorder notes met: the instance's Tiny save plays with Quick Combat and Quick Movement on (no fight is visualised:
  set both off by `Network.SendGameOptions` and read them back); a span of 450 frames with `water_frames.bin` makes a
  4.0 GB bundle (Tiny); published frames ran at about 30 a second, the span came about 32 s after the units' controller
  started (`--after 1500`); a stimulus can wait for render.log's `recorder: span starts` line in the newest run folder.

### 43.1 The cause: sceneshot replayed the units in mode own (ticket 5070, epic 0042, 2026-11-25)

Written 2026-11-25 from 10:18 EST by w157-0042 (branch `w157-5070`). Details: `research/05-graphics-composition/
unit-combat-server.md` 18.47.
- **Cause [C]**: sceneshot's `OptionsFromBundle` (the port of server.exe's `SceneOpts`) never read
  `render_units_positions` or `render_units_ready_hold_ms`, and builds its unit scene with `MakeSceneUnitsWith` instead of
  server.exe's `MakeLive` (which sets `UnitsOptions::positionsExe` from the parsed key). A bundle recorded with `exe` (this
  one; the nine `C5H-4980-*` library bundles too) replayed in mode own: walks rooted and timed by the server instead of by
  the EXE's kind-6 records, the 0042 walk-hold rules off, so a fight after a walk and every clip after it ran at other
  times. The player's log said so (`position source is own`); unitfeed (which reads the key since 4755) said `exe`. Not a
  clock, seed or record order: `nowMs` and `drainMs` are recorded and replayed as live, every recorded frame is drawn.
- **Rule for later ports** [C]: a server.cfg key that server.exe applies outside `SceneOpts` (here in `MakeLive`) is not
  in the player unless the player reads it too; the player's log line `sceneshot: render_config: units positions ...` now
  names the source. A bundle whose offline replay differs only where units walk: check this line against scene.json
  first.
- **Fix and test**: sceneshot takes both keys as `ParseHostConfig` read them; `sceneshot_test.cpp`'s units gap case
  (fails before the fix). Results `[L]` (2026-11-25 11:24-11:36): `sceneshot_test` 316 passed; the bundle replays
  byte-equal to live in 9 of 9 dumped frames (exit 0); with `--set render_units_positions=own` exit 10 with 18.43.3's
  numbers. So a fight in the span replays equal to live once the position source is replayed; the first fight bundle
  can be a library member. Rule A with it as a member (2026-11-25 13:41-14:02, a private hard-linked library of the 16
  bundles plus this one): offline-equals-live 18 passed, scenebundle-py 34 passed (unit-combat-server.md 18.47). The F:
  master write waits for 5070's merge (the trap below); its commands are in the ticket's log.
- **Ordering trap for the library** [C]: a bundle that needs this fix fails `offline-equals-live` for every branch
  without it, so it enters the F: master only together with (or after) the merge of the player change that it needs;
  until then a rule A that holds it runs on a private library folder (no `scenelib-manifest.json` beside it, so run_all
  runs no `scenelib.py check`).

## 44. scenebundle-programs on a loaded machine: sceneshot_test's timeout, and a timeout is no result (ticket 0800, 2026-11-26)

- **The fault** [L, from the night's gates]: `build_scenebundle_tests.py` gave `sceneshot_test` 1,500 s (TIMEOUTS, ticket
  0482) and `run_all.py` passed no timeout to `--run-only`; beside nine heavy jobs on 2026-11-25/26 `sceneshot_test` ran
  past 3600 s and timed out four times (W155, W160, W164, impl-0782), and run_all reported each as `SUITE
  scenebundle-programs FAIL ... 1 failed` (the builder printed `FAIL sceneshot_test.exe: timeout after 1500 s` and its
  run_all parser counted every `FAIL` line as a failed test). Each gate then reran it alone with `--timeout 7200`.
- **The change** [C, the code]: TIMEOUTS `sceneshot_test` 7200 s; the builder's `--timeout-scale F` multiplies every
  program's limit, TIMEOUTS' included (at least 1 s); run_all's `--sb-timeout S` and `--sb-timeout-scale F` are forwarded
  to `--run-only` as `--timeout` / `--timeout-scale`. A timeout is a separate outcome: the builder prints `TIMEOUT <exe>:
  no verdict after <s> s (NOT A RESULT; ...)` (never a `FAIL` line), then `SUMMARY <p> passed, <f> failed, <t> timed out of
  <n> test program(s)`, and when timeouts are the only problem `NO VERDICT: ...` with exit 4 (1 stays for a failure, 3 for
  the launch gate). run_all reads the SUMMARY and TIMEOUT lines (an older builder's OK/FAIL lines still parse), marks the
  suite `TIMEOUT` with the program named in the reason, and ends `run_all: NO VERDICT: ...` with exit 4 when no suite
  failed; a failure elsewhere still gives `FAILED` and exit 1, with the timed-out suites named after it.
- **Tests** [C]: `server/tests/render/test_run_all.py` (no build, no game; in run_all's render-host suite): the 7200 s
  floor and the scale, the forwarded options, the builder's TIMEOUT / SUMMARY / NO VERDICT output and exit 4 with
  `subprocess.run` faked, a failure beside a timeout still exit 1, run_all's parse of old and new output and its last
  line and exit code. 9 passed (2026-11-26 20:36), and inside run_all's render-host suite.
- **Live runs** [L, 2026-11-26, workstation with 4 heavy jobs of other owners registered, plain clang-cl, game copy =
  the setting `render_game_dir` (build 4390913)]: `run_all.py --only render-host --only scenebundle-programs`
  (`server/evidence/0800/run_sb_0800.sh`, 20:39-21:26): builds c5render 132 s, rt 173 s, sb 357 s; render-host PASS 258
  passed 1204 s; scenebundle-programs PASS 4 passed 923 s (bundle_format_test 1260, recorder_test 268, sceneshot_test 328
  at its 7200 s limit, sources_test 24); `run_all: OK`, exit 0. The timeout path (`server/evidence/0800/timeout_demo_0800.sh`,
  `--timeout-scale 0.002`): the builder printed `TIMEOUT sceneshot_test.exe: no verdict after 14 s (NOT A RESULT; ...)`,
  `SUMMARY 0 passed, 0 failed, 1 timed out`, `NO VERDICT`, exit 4; run_all `--no-build --sb-timeout 1 --sb-timeout-scale
  0.002` printed `SUITE scenebundle-programs TIMEOUT 0 passed, 0 failed, 0 skipped, 4 timed out 22 s (... sceneshot_test.exe
  after 14 s ...)` and `run_all: NO VERDICT: 1 of 1 suite(s) timed out`, exit 4.
- **A risk to keep in mind** [I]: on a timeout `subprocess.run` kills only the test program and then reads its pipes to
  the end; `recorder_test` and `bundle_format_test` start child processes with inherited handles, so a child still alive
  after the kill could hold the pipe open and keep the run waiting past its limit. In the demo both were killed after 1 s
  and the run went on at once (22 s for all four), so it did not happen there; `sceneshot_test` starts no child.
- Commands: runbook 2.3 "scenebundle-programs on a loaded machine" and 2.4.
