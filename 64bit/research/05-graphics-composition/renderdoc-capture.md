# RenderDoc frame captures of the EXE's own drawing (one-off 9027, 2026-11-11)

Decision D57 (MC, 2026-11-11): the harness may load one allowlisted, hash-pinned DLL into a test game at launch, so
that RenderDoc can capture the EXE's own D3D11 frame. Before this there was no compliant route
(`insertion-point-and-frame-structure.md` section 9.4). Code: `tools/harness/launch.py` (allowlist, `check_preload`,
`trigger_capture`), `tools/harness/cli.py` (`launch --preload`, `capture`), `tools/harness/native/src/import_wrapper.cpp`
(the load and RenderDoc's API). Usage: `tools/harness/README.md`, "Preload and RenderDoc captures (issue 9027)".

## 1. How to take a capture (one command sequence)

```
set C5_QUIETLOCK_WHO=<epic>& set C5_JOB_OWNER=<epic>& set C5_POOL_OWNER=<epic>
python tools/harness/pool.py claim --owner <epic> --name c5h-c --hours 1
python tools/harness/native/wrapper.py install %USERPROFILE%/c5-instances/c5h-c/install      # wrapper from 9027 or later
python tools/harness/frontend.py -i c5h-c prepare --mods cp,vp,eui --load <SMALL save name>   # NOT the huge save: section 4
python tools/harness/cli.py -i c5h-c launch --preload %USERPROFILE%/tools/renderdoc-1.47/x86/renderdoc.dll --capture-dir C:/c5-64bit-work/<epic>/captures
python tools/harness/frontend.py -i c5h-c ingame          # repeat until exit 0; `frontend.py -i c5h-c dismiss` at the load screen
python tools/harness/cli.py -i c5h-c capture --timeout 120   # prints the .rdc path
python tools/harness/cli.py -i c5h-c stop --human-turn-hook tools.harness.turns:stop_readiness
python tools/harness/frontend.py -i c5h-c restore
python tools/harness/pool.py release --owner <epic> c5h-c
python perf/quietlock/quietlock.py run --who <epic> --what "rdc-summary" -- python tools/harness/cli.py rdc-summary <the .rdc> --json <out.json>
```

Back up the instance's `install/steam_api.dll` and `install/c5_wrapper.json` by SHA-256 (copy + a JSON of the
hashes) before `wrapper.py install`, and copy them back, checking each hash, after the stop (AGENTS.md section 4).
A wrapper built after 9027 already carries the preload; then this step is not needed.

`rdc-summary` opens the capture without a window (section 5) and prints draws per bound render target.

## 2. The mechanism [C, code] [L, 2026-11-11 12:37, c5h-c]

- Allowlist (`launch.PRELOAD_ALLOWLIST`), one entry: `%USERPROFILE%\tools\renderdoc-1.47\x86\renderdoc.dll`, 22,398,752 B,
  SHA-256 `54a75e46160f2abc4d20d734078893942bffc0f5d1472faddd728f173864bf40` (recomputed 2026-11-11 by 9027; equal to the
  value in section 9.4 of the frame-structure file). From the official `RenderDoc_1.47_64.zip`, GPG-checked
  (`research/sources/shell-gates/TOOLING.md`). Not in the repository.
- `check_preload` runs in `launch()` before the gates and before any process: one DLL only; a provisioned test
  instance only (instance.json, folder directly under a known instances root, not legacy_steam); route "import" only;
  path on the list (by normalised path, a copy elsewhere is refused even with the same bytes); size and SHA-256 equal
  to the pin; capture folder under `C:/c5-64bit-work/` or `F:/c5-64bit-work/`, never D: (default
  `F:/c5-64bit-work/<C5_JOB_OWNER>/captures`; *corrected 2026-11-21, one-off 9080: `tools/harness/launch.py` since ticket 0180,
  D133; was "under `D:\c5-64bit-work\` (default `D:\c5-64bit-work\<C5_JOB_OWNER>\captures`)"; the D: paths further down
  are the records of 2026-11-11 to 2026-11-14 runs*). Each
  refusal is a `LaunchRefused` (CLI exit 2). The record (path, hash, size, kind, capture folder and template) goes into
  the state file as `preload` and into `run\harness.log` (`launch_start`, `preload_ready`).
- The environment of that one launch carries `C5_PRELOAD`, `C5_PRELOAD_SHA256`, `C5_PRELOAD_KIND=renderdoc`,
  `C5_CAPTURE_TEMPLATE`. Without a preload the four are set to "remove", so an inherited value never reaches the game;
  `extra_env` cannot set them.
- The import wrapper (`steam_api.dll` in the instance's install), in its DllMain on the loader thread:
  0. before the guard: opens the file deny-write, SHA-256 with CNG, compares; on any mismatch it ends the process with
     its own code before the launcher's handshake (helper exit 4, `0xC5F00040` missing/short hash, `41` unknown kind,
     `42` relative path, `43` cannot open, `44` hash differs);
  1. installs the focus guard and completes the handshake as before;
  2. `LoadLibraryExW(path, LOAD_WITH_ALTERED_SEARCH_PATH)` (the deny-write handle is closed only after the map), then
     `RENDERDOC_GetAPI(1.4.1)`: `MaskOverlayBits(0,0)` (overlay off), `SetCaptureKeys(NULL,0)` (F12/PrtScn off),
     `SetFocusToggleKeys(NULL,0)` (F11 off), `UnloadCrashHandler()`, `SetCaptureFilePathTemplate(C5_CAPTURE_TEMPLATE)`;
     starts a thread on the auto-reset event `Local\C5Capture_<pid>` that calls `TriggerCapture()` once per signal and
     logs the written path; removes the four variables from the process environment; logs `preload: ready pid=N`;
     failures after the handshake end the process (`0xC5F00045` load failed, `46` no API, `47` no trigger thread);
  3. loads the real `steam_api_gbe.dll` as before.
- `launch()` fails closed (process ended, `LaunchRefused`) unless the shim log holds `preload: ready pid=<pid>` once the
  main window exists. An older wrapper ignores `C5_PRELOAD` and is caught by this check.
- `trigger_capture(inst)` / `cli.py capture`: signals the event of the recorded process (pid + creation time + exe
  checked) and waits for a new `.rdc` in the capture folder whose size holds for 2 s.
- The early route (`--inject focus_guard.dll --early`) has no preload; `check_preload` refuses it. Not needed: premise
  P1 holds on the import route (below). [C, test_shim early/preload: C5_PRELOAD is ignored there.]

## 3. Premises, checked [L] unless marked

- P1, load order: the wrapper's DllMain runs on the loader thread before the EXE's entry point; the EXE creates its
  device at `0x8BB6B1` (`D3D11CreateDevice`, addresses.md) long after. Live (c5h-c, pid 24116, 12:37:52): the guard
  installed at 12:37:52.542, RenderDoc loaded 12:37:52.806 (264 ms incl. its crash-handler start), and RenderDoc's own
  log shows "Registering D3D11 hooks" at load and "New D3D11 device created ... Adding D3D11 device frame capturer"
  at 12:37:56. Before it, the process (driver/DXGI probing) created and released five D3D12 devices, also wrapped.
- P2, RenderDoc 1.47 loaded by `LoadLibrary` and driven through the in-application API, without its launcher:
  documented (renderdoc.org/docs/in_application_api.html: load the library at runtime, set options "before any
  graphics API has been initialised"; `TriggerCapture` captures the next presented frame); live: "Initialising
  RenderDoc API version 1.7.0 for requested version 10401", "Using capture file template D:\c5-64bit-work\9027\...".
  The DLL's DllMain does all its work inside the loader (RenderDoc source v1.47 `win32_libentry.cpp`, `core.cpp`):
  `RenderDoc::Initialise()` and `LibraryHooks::RegisterHooks()`.
- P3, mute watch and Documents redirection unchanged: the mute watch saw the game's audio session at 30.98 s, already
  muted, peak 0.0 (`run\audio.log`); `redirect_state=1`, `isolate_state=1`, self-tests 15/15 (shim log).
- Guard and start-up watch with RenderDoc loaded: watch ok, 804 polls over 29.3 s, 0 foreground or seen polls, 0 for
  children; guard self-test ok `missing=0x0`, `win_created 2`, both cloaked at creation; `status`: `seen_by_art false`.

What RenderDoc 1.47 does on load besides hooking, from its source (v1.47) [C] and live [L]:

- It starts its crash handler, `x86\renderdoccmd.exe crashhandle --pipe ...`, with `CREATE_NEW_CONSOLE` and
  `SW_HIDE`, and waits up to 400 ms for it (live: 153.6 ms). The console is a hidden conhost: conhost hands a
  console to Windows Terminal only when it is not started hidden (microsoft/terminal `src/server/IoDispatchers.cpp`
  `_shouldAttemptHandoff`: `SW_HIDE` and the minimise values do not hand off) [C]. Live: child `renderdoccmd.exe`
  plus `conhost.exe`, 0 windows, not seen. `UnloadCrashHandler()` removes RenderDoc's exception handler from the
  game, but the `crashhandle` process stays until the game exits (its server ends on the client's exit) [C]. Its
  crash UI would be `x86\qrenderdoc.exe`, which the x86 folder does not contain [C].
- It listens for target control on TCP `0.0.0.0:38920` and up (`core.cpp`, hard-coded, no option) [C]. Windows
  Firewall would ask on the owner's desktop for a program without a rule; all five pool instance EXEs have inbound allow
  rules (read 2026-11-11 with `Get-NetFirewallApplicationFilter`), so no prompt [C]. Consequence: while the game runs
  with RenderDoc, its target-control port is reachable from the local network on the Public profile. Keep such
  sessions short.
- It writes its log to `%TEMP%\RenderDoc\RenderDoc_app_<date>.log` and reads `%APPDATA%\renderdoc\renderdoc.conf`.

## 4. Huge save: the capture fails in the 32-bit address space [L, 2026-11-11 12:41]

Setup: c5h-c, DLL `i0ship-ccfd6dd4-ia` (pinned), CP/VP/EUI, stock `config.ini` (no D33), save
`C5H-VP8P-HUGE_0350` (huge, turn 350), 2554x1411, RenderDoc defaults plus the four calls above (no `RefAllResources`,
no `CaptureAllCmdLists`, no callstacks, no API validation).

At the trigger (12:41:43) RenderDoc snapshots the initial contents of every resource written since the last frame
("Prepared 6419 dirty resources"); about 6,400 staging copies failed with `E_OUTOFMEMORY`, then "Allocation for
4456416 bytes failed" (Fatal) and an access violation, write to NULL, at `renderdoc.dll+0xc1533` (file offset in the
DLL's crash box). The DLL's crash box recorded: free 60,304 KB of 4 GB, largest free block 4,160 KB; the process had
3,188 MB virtual, 2,883 MB resident just after. No capture file. The crash box stayed cloaked (not seen by the owner);
minidump and `crashes.log` copied to `D:/c5-64bit-work/9027/crash-124146/`.

Closing the crash box took 20 minutes: `stop` refuses while a dialog is open (also with `--coordinator-override`),
and `dismiss` needs the human-turn hook, which a crashed game cannot answer (`NeedsDecision` after 300 s). MC decision
D60 added `dismiss --coordinator-override REASON` (tools/harness/README.md rule 7); with it the box was closed at
13:02:03 and the process ended by itself. After a crash under RenderDoc, use that route.

A lighter mode that would fit [C, RenderDoc 1.47 header and source, checked 2026-11-11]:
- `RefAllResources` is already off (only resources the frame references are saved).
- `SaveAllInitials` is deprecated since 1.1 and ignored; initial contents of dirty resources are always kept.
- `SoftMemoryLimit` (spill to disk during capture) is used only by the Vulkan driver (source search: `vk_memory.cpp`,
  `vk_initstate.cpp`); D3D11 ignores it.
- So there is no D3D11 option that avoids the in-process staging copies.
- The D33 configuration uses 426 MB less (disable-systems-and-masks.md) [I: that may or may not cover the initial-state
  copies of a huge turn-350 frame; not measured]. It also draws a different frame (world systems off), so it is not
  the frame epic 3000 needs.
- Practical rule: capture on a small map (Tiny/Small, early turn), or a huge map right after load with the camera on
  a small area. Measured below.

## 5. Small map capture and its contents [L, 2026-11-11 13:02-13:07]

Setup: c5h-c, same DLL, mods and window as section 4, stock `config.ini`, save `c5h-9028-tiny-t0008` (Tiny, turn 8;
loaded read-only, SHA-256 `b2ceb663...29163` unchanged after the run). Launch 13:02:54 (pid 46972; start-up watch ok,
838 polls, 0 hits; guard self-test ok; audio session muted at first sight, 0 mutes needed later), in game 13:06:32.

| Capture | When | File | Size | Written after trigger |
|---|---|---|---|---|
| frame 24677 | default view after load | `D:/c5-64bit-work/9027/captures/c5h-c_20261111-130254_frame24677.rdc` | 97,981,327 B | 3.3 s |
| frame 27581 | after 5 wheel ticks (zoom) | `..._frame27581.rdc` | 65,862,944 B | 3.0 s |

The game ran on; `stop` with the human-turn hook ended it normally at 13:07:18 (the `crashhandle` child and its
conhost ended within 0.36 s, no orphans). The `.rdc` files are not committed.

Opened without a window: `renderdoccmd.exe convert -f <rdc> -o <x>.xml -c zip.xml` (the x64 `renderdoccmd` in the
RenderDoc folder reads the x86 capture), started on a new hidden desktop with `CREATE_NO_WINDOW` and SW_HIDE
(`tools/harness/rdc.py run_hidden`: CreateProcessW with `lpDesktop`, output to a file), through `quietlock.py run`;
3-4 s each. The XML lists every API chunk in order; `rdc.summarise` (iterparse) maps views to textures and groups
the draws by bound render target per command list. CLI: `cli.py rdc-summary <rdc>`. Outputs
`D:/c5-64bit-work/9027/analysis/` and `D:/c5-64bit-work/9027/captures/analysis/`.

What the two frames hold [L]:

| | frame 24677 (default view) | frame 27581 (zoomed) |
|---|---|---|
| Command lists executed | 51 (each list's `ExecuteCommandList` chunk appears twice in the XML export) | 53 |
| Draw calls (all `DrawIndexedInstanced`) | 572 | 998 |
| Clears RTV / DSV | 7 / 5 | 7 / 6 |
| Deferred contexts | 8 created (`CreateDeferredContext`), 7 used for recording + the immediate context (0 draws) | same |

Passes by bound target, in frame order (draws in frame 24677 / 27581):

| Target (size, format) | Depth | Draws | Matches 3005's phase |
|---|---|---|---|
| 320x190 RGBA8 | none | 12 / 11 | new (small offscreen; not in 3005's huge-save trace) [I: minimap] |
| 2554x1411 R16G16B16A16_FLOAT | none | 46 / 46 | A, full-size offscreen |
| 2048x2048 R16_UNORM | none | 1 / 1 | B |
| 1024x1024 RGBA8 | 1024x1024 D24S8 | 8 / 8 | C |
| 2048x2048 RGBA8 | 2048x2048 D24S8 | 2 / 6 | new [I: shadow map, it grows with the view] |
| 1024x1024 RGBA8 | none | 9 / 37 | new |
| 2048x2048 RGBA8 | none | 0 / 2 | new |
| back buffer | 2554x1411 D24S8 | 280 / 626 | D/E: scene clear, then the 3D lists |
| 2554x1411 RGBA8 offscreen | none | 43 / 43 | new, between the 3D lists |
| back buffer | none | 171 / 218 | F, UI |

**The tile bake is not in either frame [L]:** no draw goes to a 512x512 target (the bake's `BakeTexelScale` is 1/512,
asset-pipeline.md) or to three targets at once (`TerrainShaderScreen`'s "Albiedo/Normal/Spec Temp"); every draw
binds one render target. Zooming the camera five ticks did not cause a bake in the next frame. The bake runs when
tiles are (re)built [I: at load and on terrain changes]; catching it needs a capture across the load or a trigger
from the bake itself. For the frame's passes the capture is complete: 0 command lists were missing (all 51/53 were
recorded during the capture, so `CaptureAllCmdLists` was not needed here).

## 6. Not verified

- ~~Captures of several frames (`TriggerMultiFrameCapture` is not wired; one frame per `capture` call).~~ Done by
  0302 on 2026-11-14: `capture --frames N`, section 10.
- Whether a tile bake falls into a triggered frame by chance or needs a camera move during the capture.
- The early route with a second inject (not built; not needed while P1 holds on the import route).

## 7. On the test machines (one-off 9065, 2026-11-14) [L]

The same preload runs on `oldpc` and `laptop` through the testbed: RenderDoc 1.47 is shipped to each root
(`<root>/runtime/renderdoc-1.47`, testbed base `renderdoc`), `launch.PRELOAD_ALLOWLIST` lists that path under the same
pin, captures go into the job's own folder (`launch.TESTBED_CAPTURE_ROOTS`), and `tools/testbed/jobs/live_session.py
--renderdoc` installs a prebuilt import wrapper into the job's instance and removes it by hash at the end. On the Tiny
save `TB-TINY_0008` (turn 9) a capture took 3.5-3.6 s and was 89.2 MB (oldpc, 1912x1049) and 72.8 MB (laptop,
1530x835); the laptop's opened on the workstation with `rdc-summary` (57 lists, 601 draws). `renderdoccmd thumb` gives
the capture's back buffer as a full-size PNG, which is the only picture route on the laptop (its screen grabs are black).
Details and commands: `research/06-tooling-and-workflows/quiet-testbed.md` section 21.

## 8. Constant buffers without a replay; a failed way past qrenderdoc's debugger setting (issue 0154, 2026-11-14)

- **Per-draw constants from the zip.xml conversion [L].** `render/evidence/0154/terrain_draws_0154.py XML OUT.json` walks
  the conversion as 3601's `xml_export.py` does (it imports that module) and, for every tessellated terrain draw
  (16-control-point patch list with a domain shader), rebuilds its constant buffers from the frame-start contents plus every
  `Unmap` / `UpdateSubresource` in execution order (deferred lists inline after their `ExecuteCommandList`), and writes the
  TerrainTile and TerrainFrame members, the bound height page, the targets and the depth / rasterizer states. On five stock
  captures this gave the EXE's LodMip, LodStep and TessFactor per draw (renderer-in-server-design.md 4.16, last block).
  It needs no replay: what the CPU wrote is in the capture; what the GPU rendered (depth, colour targets) is not.
- **qrenderdoc with a scratch USERPROFILE does not start either [L, negative].** The owner's `%APPDATA%/qrenderdoc/UI.config` has
  `Python_DebugEnabled: true`, which held 3601's scripts at "Importing debugpy from ... ms-python.debugpy" (water-system.md
  6.2; qrenderdoc ignores APPDATA). `render/evidence/0154/rd_hidden_0154.py` gave the child process alone USERPROFILE and
  HOME = `C:/c5-64bit-work/0154/rd_home` (so that the search for VS Code's debugpy under the home folder would find
  nothing), hidden desktop, `--ui-python` with a script that only writes a file: no output file, **no RenderDoc log written
  at all** in `%TEMP%/RenderDoc`, ended at the 90-s timeout (2026-11-14 16:50 EST). Only an NVIDIA shader cache and an
  Overwolf log folder appeared under the scratch home. Why it hangs was not found (nothing to read without a log). The owner's
  settings were not touched. A replay route still needs the owner's decision on that setting, or RenderDoc's replay API driven
  from a program of our own (not tried).

## 9. Offline replay of a capture's terrain draws with the overlay target t1 (issue 0182, 2026-11-14)

Written by the queue implementer of 0182 (account B, branch `task-0182-terrainshot-replay`). Tool:
`render/tools/terrainreplay/` (README there; `export.py`, `manifest.py`, `terrainreplay.cpp`, `compare.py`, test).

- **The replay route existed already [C code, L run].** 3019's `render/evidence/3019/terrain_replay.cpp` (commit
  1234c563) replays hull and domain shaders (the terrain's 16-control-point patch list, D3D11 topology 48) and binds
  every pixel-shader SRV of the export, t1 included; 3019's `terrain_export.py` exports the TerrainShader draws through
  3601's `xml_export.py`. 3704's hand-back and `overlay-system.md` 10.4 ("no export of a capture's terrain draws exists",
  "watershot refuses hull and domain shaders") were written before 3019 landed. So neither an exporter nor watershot
  was extended: the 3019 tool was promoted into `render/tools/terrainreplay/` (output folder checks, exit codes, a
  `device warp` and a `shader_hlsl` directive for the test) and gives byte-identical outputs to 3019's on the run below
  (SHA-256 of exe.rgba and nooverlay.rgba equal) [L, 2026-11-14 20:31 EST].
- **What the export holds of t1 [C export].** Each TerrainShader draw's PS t1 is the 2048 x 2048 R8G8B8A8_TYPELESS
  target (view R8G8B8A8_UNORM, 12 mips, MiscFlags GENERATE_MIPS), exported as RenderDoc's frame-start contents of all
  12 mips with `"written_in_frame": true`: the previous frame's finished overlay (`overlay-system.md` 3). RenderDoc
  stores the small mips of a BC texture with a padded row pitch and a tight last row (a 256 x 256 BC1 cache: mip 3,
  32 x 32, row pitch 128, 960 bytes = 7 x 128 + 64), so a size check of a subresource must count (rows - 1) x pitch
  plus one tight row, not rows x pitch (the first check of 0182 refused every BC cache) [C, 2026-11-14].
- **The run [L replay, 2026-11-14 20:25 EST (export 20:15), RTX 4080].** Capture 3701 stock A
  (`D:/c5-64bit-work/0167/captures/c5h-a_20261114-131137_frame1762.rdc`; Tiny `C5H-0167-TINY_0008`, turn 9, not fully
  revealed; 32-bit `i0ship-26234371-ia`, stock config, RenderDoc 1.47, c5h-a, VT present; stimuli of `overlay-system.md`
  "Evidence": hex grid on, MovementRangeBorder, WorkedFill, Culture contour, unit 1002 selected at (50,13)); conversion
  and export 1 min (86 draws: 42 m73p0, 1 m73p1, 43 m73p2; 130 MB export, 71 MB xml); 43 draws into the back buffer
  (2554 x 1411) replayed with the captured t1 (`exe`), with a transparent t1 (`nooverlay`) and without fog of war
  (`nofow`). Reference: the harness shot of the same launch and camera (`.../3701/live_stock_stimuli.png`, written 13:14:41,
  16 s before the capture file).
- **Result [L].** The overlay changes 846,162 drawn pixels (37 %: the grid lies on every hex edge); 582,122 of them on
  land. Per pixel the largest channel difference, replay vs stock frame:

| Set (compare.py) | Pixels | Median / p95 / max | Equal | Within 4 | Within 16 |
|---|---|---|---|---|---|
| overlay pixels on land, all plots | 582,122 | 19 / 153 / 251 | 38.1 % | 42.6 % | 48.4 % |
| overlay pixels on the 9 seen plots without an unseen neighbour (selected unit's plot left out) | 181,248 | **0** / 93 / 241 | 57.3 % | 61.2 % | 66.2 % |
| control: the same plots' land pixels outside the overlay | 135,482 | 0 / 84 / 216 | 63.5 % | 64.6 % | 68.2 % |
| plot (50,12): grass, movement border and grid, nothing on top | 19,359 | 0 / 2 / 12 | 81.2 % | 99.8 % | 100 % |
| plot (52,14): plains, culture border, river on one edge | 32,718 | 0 / 22 / 145 | 93.0 % | 93.2 % | 94.6 % |

  Difference images and the side by side: Visual Checks `vc-20261114-131441-workstation-16b1` and `-6069` (issue 0182,
  diagnostics). Numbers: `render/evidence/0182/compare_stockA.json`, `acceptance_0182.txt`.
- **Causes of the rest, each named [L by the difference image, I for the attribution of single plots]:** what the stock
  frame draws after the terrain and the replay does not: the fog-of-war clouds of unseen plots over their seen
  neighbours (every edge plot; unseen plots median 43-157), water and the shore foam at the coast (the (50,12) differences
  up to 12 are all at its shore), rivers along hex edges (where the grid and border pixels are: (52,14), (52,15)), units
  ((50,13) masked, (51,15) the warriors, (50,14)), Honolulu's buildings ((51,14)), tree models ((50,15) forest), resource
  models and icons ((50,14) gems, (52,15) wheat), the city banner, and a vertical light shaft over the city in the stock
  frame ((50,14), (51,14)). The overlay adds no difference of its own: on the seen plots its pixels match as well as the
  control, and on a plot with nothing on top it matches within 4 on 99.8 % (the rest is the shore). The selection ring
  (frame-start t1 vs the frame's own) was masked, not measured.
- **The capture's own picture is not a pixel reference [L, 2026-11-14 20:31 EST].** `renderdoccmd thumb` of this
  2554 x 1411 capture gives a 2048 x 1131 PNG, not full size (section 7's "full-size" holds for the laptop's 1530 x 835
  frame, below 2048). Against the harness shot scaled to that size it differs by median 6-10 even on the static interface
  (top bar, unit panel; nearest, box, bilinear, bicubic and Lanczos scaling alike): scaled and lossy. It does confirm the
  camera: signed mean +0.3 over the frame, i.e. the harness shot and the capture show the same view.
- **Checks [L].** `test_terrainreplay.py`: 38 of 38 on WARP (t1 composed `colour * (1 - a) + rgb` per quadrant; a
  transparent t1 gives the base colour; tessellation factor 4 seen in the domain locations 0, 64, 128, 191, 255; depth and
  id; 15 negatives of the replayer and 8 of the front ends, each with its exit code and no output written). Under MSVC
  `/fsanitize=address`: the test 38 of 38 and the real stock A replay (peak 970 MB) with 0 ASan reports, outputs equal to
  the clang-cl build's byte for byte.
- **Not done / open.** Water draws with t9 (3704 scope 3, second half: "the same for one water draw with t9 if a capture
  has a border over water"): the replayer takes any draw list, but the water's export (3601) and a manifest for it were
  not joined to this run. The ring's exactness under decision 2 (i) needs a state without a selected unit. A replay route
  for the targets rendered inside the frame (after-draw colour) is still RenderDoc's own replay, held by qrenderdoc's
  debugger setting (section 8).
- **`--select all` exports and buffer SRVs (issue 0347, 2026-11-15) [L run, C code].** Before 0347, `manifest.py`
  stopped on every export that holds a buffer SRV with `texture format None not handled (buffers/<res>_<hash>.bin)`
  (exit 1, no manifest): it took every SRV for a texture. Every `--select all` export on disk has one (the unit draws'
  bone palette, VS and PS t27, R32G32B32A32_FLOAT, BUFFER view 0..16383 of a 262,144-byte dynamic buffer): base failed
  on all seven of them (scene-library city, coast, river, highlights; 3901; 3907 p4_shown_all, p4_hidden_all). Now a
  BUFFER view of a format the replayer's `buf` directive takes (DXGI 2, 3, 4, 10, 16, 17, 28, 41, 42, 43: 4, 8 or 16
  bytes per element, issue 4152) becomes `buf STAGE SLOT PATH FMT FIRST NUM`; any other buffer SRV (structured or
  typeless format, BUFFEREX/raw view, no view, a range past the file's end) is skipped with
  `manifest.py: warning: draw N STAGE tK: buffer SRV FILE skipped: <why>` once per SRV, and listed in replay.json's
  `skipped_srvs` (written only when something is skipped). All seven exports now give manifests (exit 0, no warning:
  their buffer SRVs are all bone palettes); 3901's `exe` variant (the 490 draws of its most-drawn target, 16 `buf` lines) replays on the
  RTX 4080 with exit 0 (Visual Checks `vc-20261115-050242-workstation-d025`, diagnostic). Exports without buffer SRVs
  are unchanged: base (main@2b2a6a76) and task outputs SHA-256-equal for all ten variants `--no-run` on 4152's
  `export_terrain` (1,737 files), 4109's `frame654_bake` (93) and `frame1286_bake` (229), and for a hardware replay of
  `exe`, `nooverlay`, `nofow` of `export_terrain` (18 files incl. colour, depth, id, PNGs). Evidence:
  `research/05-graphics-composition/evidence/0347/`. Test: `buffer_export_cases` (12 cases; the suite 74 of 74 on WARP).
- **Still open for `--select all` exports [C export, I effect].** `manifest.py` gives a cube view (ViewDimension 9;
  3901: 336 PS SRVs of the unit draws) DIM 2 with ARR 6, which a TextureCube register reads as 0 (4152's finding, README
  "Directives added by issue 4152"); `models/tools/unitreplay/unit_manifest.py` maps them to DIM 4. The terrain variants
  never meet one (terrain exports hold none), so unit draws in a `manifest.py` replay are not colour-faithful.

## 10. Several frames per request, the address-space guard, and `rdc-export` (issue 0302, 2026-11-14)

Written by the queue implementer of 0302 (account B, branch `task-0302-harness-w5-capture`; W5 of the offline scene
harness plan, `research/07-architecture-decisions/architect-2026-11-14-offline-scene-harness.md` 4.5, premises P3, P6).
Code: `tools/harness/native/src/import_wrapper.cpp` (request block, guard), `tools/harness/launch.py`
(`trigger_capture(frames=, min_free_mb=)`, `CaptureGuardStop`), `tools/harness/cli.py` (`capture --frames N --mode
multi|loop --min-free-mb M --json F`, `rdc-export`), `tools/harness/rdcexport/__init__.py`,
`render/tools/terrainreplay/manifest.py --no-run`. Tests: `tools/harness/tests/test_capture_frames.py` (35, hermetic),
`tools/harness/native/tests/test_shim.py --only preload` (62 checks, plain and with the ASan wrapper). Run records
(the `--json` of every series below, the frame times, the export summaries) and the acceptance text:
`research/05-graphics-composition/evidence/0302/`. The captures themselves are not kept (game data, D83).

### 10.1 Commands

```
python tools/harness/native/wrapper.py install <instance>/install          # a wrapper from 0302 or later (request block v2)
python tools/harness/cli.py -i c5h-b launch --preload %USERPROFILE%/tools/renderdoc-1.47/x86/renderdoc.dll --capture-dir C:/c5-64bit-work/<ticket>/captures
python tools/harness/cli.py -i c5h-b capture --frames 8 --json C:/c5-64bit-work/<ticket>/runs/series.json   # exit 8: the guard stopped it
python tools/harness/cli.py rdc-export <capture>.rdc C:/c5-64bit-work/<ticket>/exports/<name> --xml-dir C:/c5-64bit-work/<ticket>/xml \
       --select terrain --manifest exe,nooverlay [--replay-exe <build>/tools/terrainreplay/terrainreplay.exe]
```

`--select` is `terrain` (materials 73, 76, 78, as `render/tools/terrainreplay/export.py`), `water` (3601's own
selection), `materials:L`, `draws:R` (frame-order indexes) or `all`. The output is 3601's export format (what
`render/tools/watershot` and `terrainreplay/manifest.py` read); `--manifest` writes terrainreplay manifests through
`manifest.py --no-run`, or runs the replays with `--replay-exe`. `OUT/rdc_export.json` holds the times, the counts and
the frame's number and Present time (`frame_info`). Exit codes are in the module's docstring. Shader names need the
player's EXE (3601's default path, or `--exe`); `all` and `draws:` work without it.

### 10.2 P3: the multi-frame call [C]

- `TriggerMultiFrameCapture(uint32_t numFrames)` is in the table the wrapper already gets: `renderdoc_app.h` of
  RenderDoc 1.47 (`%USERPROFILE%/tools/renderdoc-1.47/renderdoc_app.h`) typedefs `RENDERDOC_API_1_4_1` to the 1.7.0
  struct, and the call ("new function in 1.1.0") is its 23rd entry, after `EndFrameCapture`. Live: the wrapper logs
  `renderdoc api version 1.7.0, multi-frame yes` [L, c5h-b, 2026-11-14 21:01].
- RenderDoc's core (`renderdoc/core/core.h`, `core.cpp`, branch v1.x, read 2026-11-14): `TriggerCapture(n)` sets
  `m_Cap = n`; `ShouldTriggerCapture` starts a capture while `m_Cap > 0` and decrements it. So
  `TriggerMultiFrameCapture(0)` cancels the frames not yet started; the 0302 guard uses that.
- The request: the harness writes the frame count and the floor into the named section `Local\C5CaptureReq_<pid>`
  (magic `C5RQ`, version 2) before it signals `Local\C5Capture_<pid>`; frames 0/1 call `TriggerCapture` as before. A
  wrapper without the section (before 0302) is refused for N > 1. Instances keep their own wrapper: install one from
  0302 or later before a series (c5h-b was restored to its earlier wrapper by hash after these runs).

### 10.3 What "consecutive" means, and the time between frames (P6) [L]

Tiny `C5H-0302-TINY_0008` (copy of c5h-9028-tiny-t0008, turn 9), c5h-b, DLL `i0ship-26234371-ia`, CP/VP/EUI, stock
config, frame cap 30 (0157), 2554 x 1411, RenderDoc 1.47; 2026-11-14 21:04.

- A multi-frame request captures **every second presented frame**: file and `Frame Metadata` numbers 3574, 3576, 3578
  (and 1000, 1002, ... 1016 in a fresh process). The frame between two captures is the one during which RenderDoc
  writes the previous capture; it is not captured. [L; why RenderDoc's D3D11 Present consumes the count only a frame
  later is not established, I.]
- Each capture carries a usable time: the conversion's header has RenderDoc's timebase (`frequency` 10 ticks per
  microsecond, one base for the whole process), every chunk a timestamp. `rdcexport.frame_info` reads them:

| Capture | Frame | Begin to Present of the captured frame | Present to the previous capture's Present |
|---|---|---|---|
| frame3574 (first of the series) | 3574 | 577.4 ms (includes RenderDoc's initial-state copies) | - |
| frame3576 | 3576 | 22.5 ms | 1,094.7 ms |
| frame3578 | 3578 | 32.2 ms | 1,239.2 ms |
| frame4294 (next series, 25 s later) | 4294 | 678.8 ms | 24,956.8 ms |

  So two "consecutive" captured frames are about 1.1-1.25 s of game time apart (the write of the previous capture
  stalls the game for that long), not one frame interval. For a pose sequence (plan W6) the step between captured
  frames is about a second, and its exact length is read from the Present timestamps, not assumed.
- `--mode loop` (single captures one after the other through the harness) gives frames about 70 apart (3.4 s per
  capture, 2 s of which is the harness's settle wait): not consecutive.

### 10.4 The 32-bit address space limits N; the guard (MC answer #1111 on 0302) [L]

Same setup as 10.3; Standard = a new game (Continents, WORLDSIZE_STANDARD, 7 AIs, turn 1; the D10 Standard autoplay save
`D10-STANDARD-x86-T50` did not load in the GUI game: the autoload ended at the unmodded main menu, Visual Checks
`vc-20261114-214315-workstation-18b4`). Largest free block of the EXE's 4 GB address space, from the wrapper's own
`VirtualQuery` walk every 10 ms (`largest_free_kb_min` = the lowest reading while a frame was captured and written).
No deliberate failure run (MC #1111): the limit is measured to a floor and extrapolated.

- **The first capture of a process keeps about 400 MB for good**: Tiny 766 MB before, 351 MB after the first capture
  (21:04); Standard 594 MB before, 206 MB after.
- **Each further frame takes the block down while it is captured and written and gives part of it back afterwards**:
  per-frame drops (reading after the previous capture minus the minimum during the frame) of 0-158 MB on Tiny and
  60-122 MB on Standard; net about 15-45 MB per frame over a series. The block after a series stays lower (Tiny: 94 MB
  after 18 frames in three requests; back to 202 MB a minute later, idle).
- **N per fresh process** (one request of 60 frames, stopped by the guard; the EXE never failed in these runs):

| Map | Floor | Frames written | Largest free block: before, after frame 1, lowest seen | Time |
|---|---|---|---|---|
| Tiny | 60 MB, reading only (first guard version) | 9 | 731, 346, **27 MB** (during frame 9) | 21:38 |
| Standard | 60 MB, reading only | 5 | 580, 206, **37 MB** (during frame 5) | 21:47 |
| Standard | 100 MB, reading only, process with 5 earlier captures | 2 | 215, 142, **36 MB** (during frame 2) | 21:48 |
| Standard | 100 MB with the frame-cost prediction (current) | 4 | 629, 300, 109 MB | 21:55 |
| Tiny | 100 MB with the frame-cost prediction (current) | 7 | 740, 414, 190 MB | 21:58 |

  Extrapolated [I]: with no guard the 9027 failure (an allocation of 4.4 MB failing at a 4 MB largest block, section 4)
  would come about one or two frames after the lowest readings above, i.e. at about N = 10 on Tiny and N = 6 on
  Standard turn 1 in a fresh process; fewer after earlier captures in the same process or on a later turn (more dirty
  resources per frame, section 4).
- **Why the guard predicts**: a check of the reading alone let a frame start with 118-142 MB free that then went down
  to 36-37 MB (rows 2 and 3): one frame can cost up to ~105 MB on Standard. So the wrapper keeps the largest drop a
  frame has caused in the process (its first capture not counted) and, right after each capture is written, cancels
  the rest (`TriggerMultiFrameCapture(0)`) unless the block is at least floor + that drop; a reading below the floor at
  any time cancels too; at the request nothing is triggered below floor + drop. The harness checks the floor from
  outside before it signals. Default floor 100 MB (MC), `--min-free-mb`; `capture` exits 8 and lists the files written.
  Limit: a frame already started when the decision falls is still captured (row 4: frame 4 after the decision at
  frame 3, lowest 109 MB); on these maps the prediction covers it, on larger ones it is not proven.
- Practical rule: restart the game for a long series; expect about 7 frames per process on Tiny and 4 on Standard at
  the default floor; capture fewer views per process rather than lowering the floor.

### 10.5 Times per capture and per export [L, workstation, 2026-11-14; outside a timing window, an indication (D106)]

- Capture: in a series about 1.0 s per frame by the wrapper's clock (Tiny 1,008 / 2,153 / 3,427 ms after the trigger;
  Standard 552 / 1,533 / 2,373 / 3,208 ms); one frame alone 0.7-1.1 s; the harness reports a file 2 s later (its
  settle wait). Files 98-120 MB each on Tiny, 105-118 MB on Standard.
- `rdc-export --select terrain --manifest`: Tiny frame3574 (98 MB): conversion 6.2 s, export 20.4 s (86 draws: 42
  m73p0, 1 m73p1, 43 m73p2; 135 MB), manifests 1.3 s, total 28.2 s. Standard frame548 (113 MB): conversion 5.4 s,
  export 13.1 s (82 draws), frame info 0.1 s, manifest 0.6 s, total 19.3 s. A conversion alone: 4.5-5.6 s per Tiny
  capture.
- The first captured frame of the new Standard game (frame548) holds a **tile bake**: one TerrainShaderScreen pass-1
  draw (`m76p1`, draw 53) into three targets at once and three pass-3 draws (`m76p3`, draws 55-57) into one: the
  pattern section 5 looked for and did not find in a Tiny frame [L, the export's `draws.json`; not analysed further].

### 10.6 Not verified

- The replays of the manifests written here (`--replay-exe` runs `manifest.py` as 0182 did; not run on these exports).
- The guard on a map larger than Standard or a later turn (the huge save of section 4 fails at the first capture; the
  guard should refuse or stop at frame 1 there, untested live).
- Whether the skipped frame between multi-frame captures is the same with other drivers or RenderDoc versions.
