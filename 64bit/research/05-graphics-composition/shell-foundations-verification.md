# Independent verification of the shell-side foundations (epic 3000), one-off 9031

Written 2026-11-11 by one-off 9031, an agent that did not build epic 3000. Subject: `shell/` and the research of epic
3000 at `main` a0b3cf3 (`shell/` unchanged since the merge c85cbb5). Every number below was measured in this session
unless it says "their tool". Scripts and outputs: `shell/evidence/9031/`; full data `D:/c5-64bit-work/9031/` (not
committed).

Files named below as `ins/...`, `pick/...`, `fc/...`, `grid/...` are in `D:/c5-64bit-work/9031/live/`; their copies in
`shell/evidence/9031/live/` carry the folder as a name prefix (`fc_frame_cost.json`, `grid_land_z17_gridmetric.json`).
The verifier's scripts are in `shell/evidence/9031/tools/` (`v9031.py` the live driver, `gridmetric.py`, `fc_stats.py`,
`hold.py`).

Marks: [C] measured here offline (dump, disassembly, stand-in run, own code); [L] measured here in the live game;
[I] inferred. "Their tool" = the team's own program rerun.

Live runs: instance c5h-a (Goldberg, cloaked), DLL `i2-shell-test` b92373aeb (SHA-256 638B5E21...C83B4, the pin
`deps/i2-shell-test.lock`, installed with `provision.py set-dll`), CP/VP/EUI, huge save `C5H-VP8P-HUGE_0350` t350,
`shell_composite.dll` built from the clone (SHA-256 fd5f464d...24d7). Four launches 12:37-13:52 plus one that stayed in
the menu, all with guard self-test ok and start-up watch ok, no dialog, no crash, no hang; ToggleStrategicView never
called. c5h-a restored by hash afterwards (`backup.py restore`, DLL by `set-dll --original` to 5fdf3432,
`frontend.py restore`), `provision.py verify c5h-a` ok, claim released 13:52.

## 1. Verdict per claim

| # | Claim (epic 3000) | Verdict | Section |
|---|---|---|---|
| 1 | fresh-clone build and the asan variant; 93 of 93 in both, 0 skipped | **confirmed by own measurement** (93/93 plain and ASan, 0 skipped, 0 ASan reports) | 2 |
| 2 | error paths handled (README table) | **not confirmed**: 4 defects found by the verifier's own hostile cases (V1-V4), one of them a hang of the presenting thread (V2); the camera's TORN/stale/garbage/pointer cases, the server stall/restart/wrong-size cases and the resize storm are handled | 3 |
| 3 | road answer: roads, railroads, farm fields and trees gone; models at stock positions | **confirmed by own measurement** (places of own choice) | 5 |
| 4 | rule "after the list that clears the back buffer"; 0 missed in map frames; misses only in leader screens, load screens and 1 per resolution change | **confirmed by own measurement** (12.9 min of map frames incl. 5 AI turns: 0; leader screens; 1 per resolution change; 22 over a map change) | 6 |
| 5 | frame cost +0.108 ms (3.856 vs 3.748 ms) | **confirmed, weakly**: own held series +0.076 ms, 95 % CI [-0.096, +0.248], overlaps theirs; p99 +0.29 ms (theirs +0.31) | 6 |
| 6 | grid within about 1 px on land at zooms 11/17/20.5; water fix (3012) | **land confirmed by own measurement** (centre line -> grid median 1.0 / 1.0 / 0.0 px); **water fix: direction confirmed, size wrong**: open ocean +23 px without it, **-5 px with it**, not about 0 | 4 |
| 7 | clicks land on the right hex (18/18) | **confirmed by own measurement**: 20/20 incl. seam, both map edges and water, against the published camera block (not at zoom 20.5) | 6 |
| 8 | camera read: statuses under resolution change, map change, leader screen, minimise/restore; cost | **confirmed by own measurement** (OK throughout each; 4 frames INCONSISTENT at the map change; 2.7 us mean per Present); **one wrong detail**: the team's resolution "back" step went to 130x10, not back | 6 |
| 9 | 426 MB less private memory in D33; launch-to-game -25 s | **memory confirmed** (-428.5 and -457.9 MB in two pairs); **launch: -13 s to the ready screen confirmed, -25 s to in game not reproduced** (-14.0 s and -12.9 s) | 5 |
| 9b | soak growth 1,649 -> 1,804 MB also without the shell? | **not verified** (no stock one-hour run; time) | - |
| 10 | dark red: building PS blends with the fog mask at 0xDA12AC, written only by FluidFOW start-up | **confirmed by own measurement** (own extraction and disassembly; byte scan: 2 writers, FluidFOW start-up and the terrain editor; live 0x0 in D33, 0x269 in stock) | 8 |
| 11 | research quality | **mostly good**; three statements wrong or stale, corrected in place (section 9) | 9 |

**Defects found** (for the MC to file; severity as the verifier judges it):

| Id | Severity | Defect | Where | Proposed fix |
|---|---|---|---|---|
| V1 | medium | The camera consistency checks ignore NaN: a NaN anywhere in g_View[0] or a wrap view (all NaN included) is published as **OK** by the DLL and accepted as OK by the server; 270 of 2,334 OK blocks carried a NaN in the hammer run | `exe_camera.h` `VPCamConsistency` 112-125 and `VPCamWrapError` 129-145 (`if (e > rot)` drops NaN); `shell_server.cpp` `ServerConsistent` 401-409 checks only the projection for finiteness | in both helpers return +inf when any input or product is not finite (or test `!(e <= rot)`); check every published matrix for finiteness in `CamRead` and in the server |
| V2 | **high** (hang of the game's presenting thread from one ini value) | `timeout_ms` has no limit: `-1` is read as INFINITE, and the host's render thread then waits for good at the first server texture change (seen 2 ms after the first open); any positive value stalls every frame by that much while the server is gone or resizing (500 -> 1 frame per second) | `shell_composite.cpp` 242 (`(DWORD)_wtoi`), `AcquireAndCopy` 631-672 (waits even while STALE) | clamp to 0..50 ms and log; never wait while the section is STALE or a resize is pending; add `timeout_ms=-1` and a killed-server case to `test_bad_ini_values_fall_back` |
| V3 | medium (wrong placement, flagged by `ins_wrong` only) | A `tag_match` that parses to no token (`" , "`) or contains a character the C locale cannot convert (an ini saved as UTF-8 with any non-ASCII character) matches **every** list: the composite fires after the first list of every frame, 55-62 % of insertions outside the window | `ParseCond` 188-208 (`empty` stays true; `wcstombs_s` EILSEQ unchecked), `Matches` 1498 | treat a present but empty or unconvertible value as `impossible` and log it, as the README promises |
| V4 | low (no second chain in the game today [I]) | A swap chain created through the factory by anything else in the process becomes the target at its first Present and keeps it while it presents, so the game's own chain is no longer composited; with more than 8 other presenting chains the DLL logs "another swap chain presents" at every Present (2.17 MB in 6 s) | `OnSwapChainCreated` 1976-1980, `OnPresent` 1811-1830 (`g_otherChains[8]`) | adopt a new chain only if it uses the target's window (or device) or the target has stopped presenting; log an unknown chain once per chain with a counter, never per Present |
| V5 | low | A server texture of the same size but another format replaces the shared texture while the local copy keeps the old format: `CopyResource` between them is invalid (nothing copied) | `OpenSharedTexture` 526 compares only width and height | recreate the local texture when the format changes too |
| V6 | low (server process) | The server accepts maps up to 1024 x 1024 plots from a block and builds up to about 50 M line vertices (600 MB) per grid change | `shell_server.cpp` `BuildGrid` 339-345 | limit to the game's largest map (128 x 80 plus margin) [code review only, not run] |
| V7 | low (process) | The team's live tools hard-code instance c5h-b (`darkred.py`, `live3008.py`, `mapchange.py`, `turns_soak.py`; `i2_capture.py` defaults to it) | those files | take `--instance` like `i2_capture.py` |
| V8 | low (process) | `shell/build.cmd` builds into `D:\c5-64bit-work\3000\shell-build` (epic 3000's own scratch folder) when `C5_SHELL_BUILD` is unset | `build.cmd`, README "Build" | default to `shell\build` in the repository |
| V9 | info | "WARNING: hooks installed 10 s ago but no Present seen" is logged at every game launch (5 of 5): the DLL is loaded at the front end about 16-30 s before the game's first Present | control thread 2364-2368 | start the 10 s from the first Present of any chain, or say "not yet" |

## 2. Build and the team's suite from a fresh clone

Fresh `git clone <this repository's GitHub URL> D:/c5-64bit-work/9031/clone` (a0b3cf3, then
fast-forwarded to eefa0e8; `shell/` identical to c85cbb5 in both), own build folders through `C5_SHELL_BUILD`
(`D:/c5-64bit-work/9031/shell-build`, `-asan`), every step through `quietlock.py run` [C]:

| Step (2026-11-11) | Result |
|---|---|
| `cmd /c shell\build.cmd` | BUILD OK, 0 warnings in the log |
| `cmd /c shell\build.cmd asan` | BUILD OK, ASan runtimes copied, 0 warnings |
| suite, plain build (12:13-12:21, `memcap.py --mb 6144`) | **93 passed, 0 skipped**, 472.5 s, peak committed 245 MB |
| suite, ASan build (12:21-12:30, `ASAN_OPTIONS=log_path=...`) | **93 passed, 0 skipped**, 524.0 s, peak committed 971 MB, **0 ASan reports** |

Logs: `shell/evidence/9031/suite_plain.log`, `suite_asan.log`. Note: `shell/build.cmd` writes into
`D:\c5-64bit-work\3000\shell-build` by default (epic 3000's own scratch folder) when `C5_SHELL_BUILD` is unset; a later
user who follows the README writes into another epic's folder (defect V8, low).

## 3. Hostile cases on the stand-in

The verifier's own cases (`shell/evidence/9031/hostile/hostile_cases.py` and `hostile_conftest.py`, renamed in the
evidence so that pytest does not collect them; run as `test_hostile.py` / `conftest.py` from `D:/c5-64bit-work/9031/hostile`, reusing the team's fixtures
and hidden desktop; `twochain.cpp`, a host of the verifier's that presents several swap chains), run 2026-11-11
12:31-12:36 on the plain build: 26 cases, 16 pass, 10 fail. Every failure is one of the defects below; no case crashed
the host, none produced an ASan-relevant fault (plain build), no DISABLED. Per case JSON: `shell/evidence/9031/hostile/`.

**Camera reader.** Method: test_host `--fake-exe` with the view globals frozen after 1 s (the fake writer then leaves
g_View, g_Proj and g_NumViews alone), then one hostile value written into the fake image with WriteProcessMemory; the
camera block sampled from the section every 5 ms for 3 s and checked by the verifier's own NaN-propagating check; the
server's grid state from its log.

| Case | DLL status published | Server | Verdict |
|---|---|---|---|
| NaN in g_View[0] translation x | **OK in 540 of 540 blocks** (consistRot 6e-8, consistTrans 1e-5) | **OK, draws with it** | **defect V1** |
| g_View[0] all NaN | **OK in 537 of 537** (consistRot 0, consistTrans 0) | **OK** | **V1** |
| NaN in wrap view 1 (g_NumViews 2) | **OK in 539 of 539** | **OK** | **V1** |
| +inf in g_View[0] translation x | INCONSISTENT | HIDDEN | correct |
| +inf in g_Proj[0] / g_Proj all NaN | OK (only the information flag) | SRV_INCONSISTENT (its finite check on the projection) | caught by the server only |
| g_Proj all 1e29 | OK | OK | accepted (finite; the server's limit is 1e30) |
| g_NumViews 0x7FFFFFFF | INCONSISTENT (views 0) | HIDDEN | correct |
| 320 random bytes over g_Proj and g_View[0..3] | INCONSISTENT | HIDDEN | correct |
| random words / NaN / inf hammered into g_Proj, g_View, g_NumViews and camWorld for 6 s (386,465 writes) against the panning frame writer | 2,334 OK, 1,522 INCONSISTENT; **270 OK blocks carry a NaN** (e.g. g_View[0][15]) | - | **V1** |
| GVS pointer -> the fake image's MZ page | BAD_GVS | HIDDEN | correct |
| GVS pointer so that GVS+0x758 faults | EXCEPTION x5, then off for good; host runs on | HIDDEN | correct |

Cause [C]: `VPCamConsistency` (`shell/common/exe_camera.h` 112-125) takes the maximum with `if (e > rot) rot = e;`, and a
NaN never compares greater, so NaN entries of `camWorld * view0` are skipped (all-NaN gives rot = trans = 0);
`VPCamWrapError` (129-145) has the same `if (d * 1000.0f > e)`. The DLL's `!(x <= tol)` guard in `CamRead` cannot see a
NaN that the max already dropped; the server's `ServerConsistent` uses the same helpers and checks only the projection
for finiteness (`shell_server.cpp` 401-409). No crash follows on the stand-in (the GPU drops NaN vertices: the grid just
vanishes while the state says OK), but the claim "every failure mode is detected and published as a status" does not
hold for NaN, and an increment-2 renderer that trusts an OK block gets NaN matrices.

**Tag rules** (`test_host --game`, rule `after`):

| Case | Result | Verdict |
|---|---|---|
| `tag_match= , ` (parses to no token) | 0 missed, **16,206 of 26,278 insertions outside the clear..3D window** (`ins_wrong`) | **defect V3** |
| `tag_match=clear_bb€` (ini saved as UTF-8) | 0 missed, **14,248 of 23,100 outside the window** | **V3** |
| `tag_nth=2` with one scene clear per frame | every frame `miss_nomatch`, nothing drawn | correct |
| anchor that never comes (`clear_dsv+ctx=99`) | every frame `miss_noanchor` | correct |
| 300-character garbage after `clear_bb,` | truncated at 127 bytes, unknown token: every frame `miss_nomatch` | correct |

Cause [C]: `ParseCond` (`shell_composite.cpp` 188-208) leaves `empty = true` when no token survives `strtok` and when
`wcstombs_s` fails on a character outside the C locale (it then empties the text, EILSEQ unchecked); `Matches` returns
true for an empty condition, so the rule fires after the first executed list of every frame. The README and the ini
say a bad rule "matches nothing". The `ins_wrong` counter does flag it.

**Shared-texture consumer:**

| Case | Result | Verdict |
|---|---|---|
| `timeout_ms=-1` (read as the DWORD 0xFFFFFFFF = INFINITE) | **the host's presenting thread hangs for good** as soon as the server switches texture generation (here at its first resize request, 2 ms after the open): no stats line, no frame, until the test killed the host | **defect V2 (high in the game process)** |
| `timeout_ms=500`, server killed | every frame waits 500 ms: **1 frame per second** (ft_mean 501-514 ms) for as long as the server is gone | **V2** |
| server suspended 6 x 1.5 s (NtSuspendProcess) | host never below 5,659 frames per stats row; STALE / alive again each time; 20 AcquireSync timeouts; recovers | correct |
| server killed and restarted 15 times, 0.4 s apart | 17 textures opened, the last one live; host handles 390 -> 390, private 37.7 -> 38.7 MB | correct |
| server texture 16 x 16 / 8192 x 8192 (`request_resize=0`) | opened and drawn stretched; host private 37.6 / 43.6 MB; frame time 0.07 / 0.24 ms | correct |
| `ResizeBuffers` every 50 ms for 10 s | 185 resizes, 38 generations opened, 0 missed, host handles 413 -> 413, server 443 -> 443 | correct |

Cause of V2 [C]: `timeout_ms` is read with `(DWORD)_wtoi(v)` and no limit (`shell_composite.cpp` 242), and
`AcquireAndCopy` (631-672) calls `AcquireSync(1, timeout)` every composited frame whenever the timeout is not 0, also
while the server is STALE. The suite's `test_bad_ini_values_fall_back` covers only `timeout_ms=abc`. The default (0)
is safe; any positive value turns a dead or resizing server into a stall of the game's main thread.

**Swap chains** (`twochain.exe`, the DLL loaded first, chain A 1280x720, then 1 or 10 more chains created through
`IDXGIFactory::CreateSwapChain`, all presenting round-robin for 6 s):

| Case | Result | Verdict |
|---|---|---|
| 1 extra chain | the new 320x200 chain is adopted at its first Present; **A, the main chain, is dropped and never composited again** (A keeps presenting, so the 2 s rule never hands it back) | **defect V4** |
| 10 extra chains | each new chain adopted in turn; the last (320x200) stays the target; **20,265 "another swap chain presents" lines, 2.17 MB of log in 6 s**: `g_otherChains` has 8 slots, so chains 9 and 10 are logged at every Present | **V4** |

The game has one swap chain (and the resolution change replaces it), so V4 does not show in the game today [I]; an
overlay or a second window that creates a chain through the factory would take the composite away from the game's
window and flood the log.

## 4. Grid against the EXE's own outlines, and the water fix of issue 3012, live [L]

Setup: c5h-a (Goldberg, cloaked), DLL `i2-shell-test` b92373aeb (SHA-256 638B5E21...C83B4, installed by
`provision.py set-dll`), CP/VP/EUI, huge save `C5H-VP8P-HUGE_0350` t350, **stock configuration** (only OverlayDebug and
ColorKeyEdit off, so the EXE draws `SerialEventHexHighlight`), 2554x1411 windowed; `shell_composite.dll` built from the
clone, `insert=present`, `blend=alpha`; `shell_server.exe --scene grid --grid-scale 0.9 --heightmap
<i2-ground-truth huge dump>` with `--water-z -1` (off) or the default (12). Launch 12:37:26 (pid 60764), guard
self-test ok, start-up watch ok. Plots and cameras of the verifier's choice (not the team's Memphis/(100,30)): land at
Alexandria (95,37) (96,37) (94,36) (97,36) (96,38), flat grassland; coastal water at Giza (95,20) (94,21) (95,21)
(94,19) (95,22); open ocean (86,19) (85,18) (86,20) (85,20) (86,18), every neighbour water. Method (own code,
`v9031.py gridacc` + `gridmetric.py`): three DLL back-buffer dumps at one still camera (view matrix change 0.0): H =
highlights on, grid hidden (`camera=0`); P = plain; G = grid (`camera=1`), no highlight. Magenta = H differs from P and
is magenta; the centre line of each visible band (ridge of the inside distance) is compared with the nearest grid pixel
(the occlusion-free direction), and the integer shift that puts the most centre-line pixels within 1 px of the grid is
reported (search +-10 px in x, +-40 px in y).

| Place, zoom | water-z | Band width | Centre line -> grid: median / within 1 px / p95 | Best shift grid - outline (px), score |
|---|---|---|---|---|
| land, 11.0 | 12 | 11 px | 1.0 / 69 % / 4.1 px | (0, -1), 76 % |
| land, 17.0 | 12 | 6 px | 1.0 / 91 % / 1.4 px | (0, -1), 98 % |
| land, 20.5 | 12 | 4 px | 0.0 / 99.5 % / 1.0 px | (0, -1), 100 % |
| coast, 16.5 | off | 7 px | 4.5 / 32 % / 16.6 px | (1, +6), 58 % |
| coast, 16.5 | 12 | 7 px | 3.2 / 47 % / 4.1 px | (0, -4), 100 % |
| open ocean, 16.5 | off | 7 px | 5.0 / 20 % / 21.5 px | (1, **+23**), 78 % |
| open ocean, 16.5 | 12 | 7 px | 3.2 / 45 % / 4.1 px | (0, **-5**), 100 % |

(The wheel steps the zoom by 0.5 from 20.5, so the water rows are at 16.5, not 17.) **On land the claim holds: about
1 px at all three zooms.** **Over open water the 3012 fix is confirmed in direction but not in size:** without it the
grid lies 23 px below the EXE's outline (the team's 22 px, reproduced at another place); with `--water-z 12` it lies
4-5 px *above* it. So the EXE draws its water outline lower than z = 12: interpolating the two shifts linearly gives
z of about 10 [I] (a plane at 0.2 x 50 would be 10; not checked in the code). The picking plane stays z = +12
(camera-and-picking.md section 5), so the water outline and the water picking plane are not at the same height [I].

## 5. The road answer and memory, on a save and at places of the verifier's choice [L]

Two launches on c5h-a, same DLL (`i2-shell-test` b92373aeb), same save (`C5H-VP8P-HUGE_0350` t350), the composite DLL
loaded in both (shots taken with it in `passthrough` or with the grid hidden, so the image is the EXE's own), harness
PrintWindow shots at full size: stock (12:37:26, pid 60764, masks 0xFFEFFFF7 / 0xFFEFFFF7) and D33 (12:59:04, masks
**0xFF000663 / 0xFF000662**, `viewmask.log`: `clear=0x00000001`, view 0xffffffff -> 0xff000662). Places not used by the
team: Xochicalco (93,32) at the load-default zoom 14.26 and at zoom 11, Giza (97,21), Pi-Ramesses (105,27) at zoom 11.
Camera positions identical in both arms to 0.1 world units (`lookat` log).

| Item | Stock | D33 |
|---|---|---|
| Roads, railroads | drawn (Xochicalco: 21 rail plots) | **not drawn** |
| Farm-field patchwork | drawn | **not drawn** |
| Terrain, water, rivers, coast | drawn | not drawn (grey clear colour, RGB 84,84,84) |
| Forest and jungle trees | drawn | **not drawn** (Xochicalco top-left jungle, Giza forest) |
| Improvement models (farmhouses, pasture fences, oil wells, citadel, plantation crops), resource animals | drawn | **drawn, dark red, at the stock positions** |
| Wonders (Great Wall, lighthouse at Pi-Ramesses), city buildings | drawn | **drawn, dark red, at the stock positions** |
| Units, ships | drawn | drawn, normal textures |

Pictures (800 px): `shell/evidence/9031/xochicalco_z11_stock_top_d33_bottom.jpg`,
`piramesses_z11_stock_top_d33_bottom.jpg`. One stock shot (Giza) caught a frame with no UI and no models (a grey band
over raw terrain); a D33 shot right after a `UI.LookAt` caught an all-grey frame with no UI. Both are PrintWindow
captures taken within 2 s of a camera jump; the next capture of each was normal [I: a transitional frame, not
investigated]. *Extended 2026-11-14 (issue 0172) [L]: on the product path (ServerRenderer = 1, composite, D33 with Overlay
on, huge save, c5h-a) the harness's PrintWindow shot returned a frame without the EXE's layer (the server's terrain only,
no interface, no models) or a flat grey frame (84, 84, 84) in 12 of 18 attempts (a first round without the check: 3 of
6 pictures and both end shots), also at the load view with no camera jump
and 3 s after a LookAt, with the composite at a steady 30 frames/s; never in the stock arm (8 of 8 attempts fine at each
of 3 cameras). Not a transitional frame only. A shot of the joined window is checked for the interface before it is
used (`shell/evidence/0172/run_0172.py` `ui_present`: bottom-right corner mean < 75 over the dark D33 minimap and the
top bar's std > 40). Cause not investigated.* The claim is **confirmed by own measurement** (one run per arm).

**Memory and load, one launch per arm, 2026-11-11, machine under load (MC decision: no hold; the registered jobs at
each launch are in `shell/evidence/9031/jobs_at_*_launch.txt`: 9028's d10 run, 9027's and 3200's and 3011's games, 3200's
shell suite). Sample 10 s after the load screen was dismissed, before any turn:**

| | Stock | D33 | Delta | Team (3003) |
|---|---|---|---|---|
| private MB | 2,011.7 | 1,583.2 | **-428.5** | -426 |
| committed MB | 2,340.4 | 1,914.6 | -425.8 | -409 |
| largest free block MB | 1,168.6 | 1,653.0 | +484.5 | +424 |
| launch -> load screen ready | 177.3 s | 163.4 s | -13.9 s | -13.4 s |
| launch -> in game | 178.1 s | 164.1 s | -14.0 s | -25 s (191.2 -> 166.1) |

Memory: **confirmed by own measurement** (-428.5 against -426 MB; the composite DLL is loaded in both arms here and not
in the team's, so it cancels). Launch times: indicative only (loaded machine, one run each); the D33 saving to the
ready screen matches (-13.9 against -13.4 s); the team's -25 s to "in game" is not reproduced (-14.0 s here: the
scripted Continue took 0.7-0.8 s in both of my arms) [I: their stock arm's 23 s from ready to in game was a slow
dismissal, not the configuration].

**Repeated under the quiet-machine lock** (hold 13:33:15-13:46:23, not disturbed; `quietlock.py check` at 13:29 showed
a 3200 suite, which ended, and a 9033 job that `take` waited for; inside the hold a 9033 testbed submit at 13:40-13:42,
39 % of one CPU at its peak; total CPU mean 20.7 %, max 42.1 %): stock 13:40:46, D33 13:43:53.

| | Stock | D33 | Delta |
|---|---|---|---|
| private MB | 2,049.1 | 1,591.2 | **-457.9** |
| committed MB | 2,377.8 | 1,937.3 | -440.5 |
| largest free block MB | 1,179.1 | 1,598.4 | +419.3 |
| launch -> load screen ready | 152.1 s | 139.1 s | **-13.0 s** |
| launch -> in game | 152.7 s | 139.8 s | **-12.9 s** |
| `0xDA12AC` / `0xDA12B0` | 0x269 / 0x26A | 0x0 / 0x0 | |

Verdict on claim 9: memory -428.5 and -457.9 MB in two pairs against the team's -426: **confirmed** (one pair each,
noise of a single pair about 20-30 MB). Load: the saving to the ready screen (-13.9 s loaded, -13.0 s quiet) confirms
the team's -13.4 s; the team's **-25 s to "in game" is not reproduced** (-14.0 s and -12.9 s; the dismissal takes under
1 s in all four of my launches).

First D33 launch attempt (12:48): the game stayed at the main menu. `frontend.prepare`'s load token is used up by one
launch; a second launch needs `frontend.py prepare` again (not a defect of epic 3000; noted for EFFECTIVENESS).

## 6. Insertion, clicks and the camera read in the D33 game [L]

Same D33 launch as section 5 (pid 51840, 12:59), `insert=tagged tag_rule=after tag_match=` (default `clear_bb`),
`shell_server.exe --scene test --rate 60` on a hidden desktop, composite mode, camera at Thebes (101,25) zoom 11.

**Missed insertions, own count** (13:04:36-13:18:30, 833 stats seconds, 266,475 frames; the DLL's per-second counters
parsed by the verifier's own code, and attributed to events by the verifier's own times; `ins/sample_d33.json`):

| When | Seconds with misses | Frames missed | Reason |
|---|---|---|---|
| 13:04:36-13:08:59 | 236 | 97,090 | Montezuma's peace-deal screen at the end of turn 350 (the 1116 handler was not armed: the screen came up before `turns.py wait` armed it; answered by the located REFUSE click after 38, 73 and 102 s) |
| 13:09:17-13:09:18 | 2 | 436 | Bismarck's discussion screen (turn 351, 1116 handler: answered in 2.0 s) |
| 13:12:51, 13:13:05, 13:13:49 | 3 | 3 | three resolution changes, 1 each |
| everything else: 5 AI turns 350 -> 355 (13:04-13:10), 20 posted clicks, two `UI.LookAt` jumps per click group, minimise and restore | 0 | **0** | - |

All misses are `miss_nomatch`; 0 `noexec`, 0 `ins_wrong`, 0 stale, 0 DISABLED; camera `OK` in every stats line. The map
frames outside leader screens and resolution changes add up to 592 s (9.9 min). A further 180 s in the D33 game launched
inside the hold (pid of 13:43:53, Thebes, 13:46:40-13:49:40): 51,235 frames, **0 missed**, and 360 of 360 PrintWindow
captures showed the composite under the UI (2 more all-grey artifacts). In all **772 s (12.9 min) of map frames with 0
missed**. **Map change in one process** (13:50:20, `Events.PlayerChoseToLoadGame(<abs path of c5h-c5h-a-t0018-204942>)`
on the main thread, from in game, to a 56 x 36 map): grid seq 4 (valid 0) -> 5 -> 6 `valid 1 ... samples 3584x2560 (map
56x36 plots) ... period 3583.0 ... ox 1792.000 oy 1108.480`, camera `OK -> INCONSISTENT` for 4 frames (trans 2432.8),
then `OK` with views 2; 22 missed frames over the change (1 `noexec`, 21 `nomatch`) as the team saw; no EXCEPTION; the
load screen was dismissed by `frontend.py dismiss` and the game was in game. **Independent of the DLL:** 1,655 harness PrintWindow captures (one every 0.5 s,
`v9031.py sample`) checked for the D33 clear grey in the map area under a drawn UI (the picture a missed insertion
leaves): **0**. 15 captures (0.9 %) were grey from edge to edge with no UI and no models, at random times also while
the map sat idle (13:14-13:17) with 0 misses counted; 12 failed while the window was minimised. A frame without UI
cannot be a missed insertion (the UI list still draws), so these are capture artifacts of PrintWindow on this window
[I]; the same artifact spoiled one stock road shot (section 5).

**Clicks [L]:** 20 posted mouse moves; for each the EXE's `UI.GetMouseOverHex()` against the verifier's own unprojection
of the cursor through **the camera block the server draws with** (read from `Local\VPShellSection`, not from the EXE)
onto the plane z = +12, M_w2h and cube rounding (`pick/picks.json`): land at zoom 11 (4), open water (4), across the
seam with `numViews` 2 (4), beyond the top map edge (3) and the bottom edge (3), land near hex corners (3; the wheel
did not change the zoom from 11 in that run, so not at 20.5). **20 of 20 name the same plot**: 18 identical numbers; 2 beyond the
seam where the EXE returns the unwrapped x 128 and the verifier wraps to 0 (`Map.GetPlot(128, 40)` is plot (0, 40)).
Off the map the EXE returns off-map numbers ((60, 80), (61, 81), (59, -1), (60, -2)) and so does the unprojection.

**Camera read under events [L]:** resolution change 2554x1411 -> 1914x1051 (`OptionsManager.SetWindowResolution_Cached`
+ `CommitResolutionOptions` on the main thread): new chain adopted 69 ms after `CreateSwapChain`, the server's new-size
texture 347 ms after it, 1 missed frame, camera `OK` throughout. **Setting the cached resolution back to (0, 0), as the
team's "back" step did, does not restore the window: the game makes a 130 x 10 swap chain** (adopted in 37 ms; the team's
own evidence `3008_reschange.json` shows the same 130x10 after "back"); (2560, 1440), the instance's original
`WindowResX/Y`, gives back 2554x1411 (adopted 34 ms after creation, new texture 310 ms). The change also writes
`MSAASamples = 0`, `FSResID = 0` and the window size into `GraphicsSettingsDX11.ini` (restored by hash afterwards).
Minimise / restore without activation (`ShowWindow` 7 / 4): camera `OK`, `presentCount` kept rising while iconic, 0 missed.
Leader screens: camera `OK`, views 1, mode 1 during both screens; every frame `miss_nomatch` (nothing drawn over the
leader). Cost of the read and publish per Present (the DLL's own timer, `cam_us_mean` of 1,017 composite stats lines,
12:59-13:19): mean 2.7 us, median 2.4 us, median of the per-second p99 4.3 us (team: 2.3 us). Strategic view: not
touched (bug 3010).

## 7. Frame cost

**Frame cost, under the verifier's own hold [L]** (13:33:18-13:40:23, the D33 game of section 6 (pid 51840), Thebes (101,25), zoom 11, 2554x1411,
server `--scene test --rate 60`; arms switched live in the ini with 10 s settle; 60 s windows; order C P | P C | C P;
metric = frame-weighted mean of the DLL's per-second Present-to-Present `ft_mean`, parsed by `v9031.py window-stats`,
statistics by `fc_stats.py`; `fc/frame_cost.json`, `frame_cost_stats.json`):

| Arm (3 windows) | Windows (ms) | Mean (sd), 95 % CI | p99 (median of the windows' medians) | Missed |
|---|---|---|---|---|
| composite | 3.244, 3.217, 3.209 | 3.223 (0.018), [3.177, 3.268] | 4.24 ms | 0 |
| passthrough | 3.220, 3.072, 3.150 | 3.147 (0.074), [2.962, 3.332] | 3.95 ms | 0 |
| **composite - passthrough** | | **+0.076 ms, 95 % CI [-0.096, +0.248]** (Welch df 2.2; detectable about 0.22 ms) | **+0.29 ms** | |

The team's +0.108 ms [+0.001, +0.215] lies inside this interval, so the two series agree; with three windows per arm
this one cannot exclude zero for the mean. The p99 shift (+0.29 ms against the team's +0.31 ms) is reproduced
closely. AcquireSync ran 59.5 times a second in the composite arm (3,567 per 60 s), none in passthrough. The view
differs from the team's (zoom 11 here, 14.26 there: 310 against 260 fps). Grade B (three windows per arm, held lock).

## 8. The fog cause (issue 3009): disassembly and the global

**Writers of 0xDA12AC [C].** A byte scan of `CivilizationV_DX11.exe` on disk (Steam, TimeDateStamp 0x546CD0A8,
read-only) for the absolute address as a 4-byte little-endian operand in `.text`, every hit disassembled with
`dumpbin /disasm`: 23 references. Two write it, each a store of `eax` to the global: 0x44A731 (FluidFOW_System
start-up) and 0x561FE2 right after the call of the texture loader 0x8C6540 (the terrain editor's set-up). The other
21 read it (word or dword loads; among them 0x7B267C, a 16-bit load in the building draw-object builder). (Quoted
instructions replaced by these descriptions, 2026-11-18, D83 audit.) The other addresses the
research lists with the FluidFOW start-up (`0x44AC35`, `0x44AF3E`, `0x44B14B`) write `0xDA12B0` (the base layer), as
the research's combined sentence allows. Limit: a write through a computed pointer is not visible to a byte scan.

**Shaders [C].** The two pixel shaders copied out of the EXE's `.shader` section by the verifier's own extractor (DXBC
blob at VA 0x061A56E8, 2,788 B; VA 0x0676D388, 2,264 B) and disassembled with `fxc /dumpbin` (SDK 10.0.26100.0): the
building PS is instruction for instruction identical to the team's `BuildingShader_CODE_ps_061a56e8.asm` (97 lines);
line 93 is a multiply-add of the mask's `.x` (t7, the FOW mask) with the lit colour minus the base layer (t8), plus
the base: `lerp(base, lit, mask.x)`. With mask 0 the output is the base layer alone. The forest PS compares its t6
mask's `.x` with 0 and discards where it is equal. (Shader lines replaced by these descriptions, 2026-11-18, D83
audit.)

**The global, live [L].** Read by ReadProcessMemory (rebased, EXE base 0x007F0000 in both launches) 10 s after each
load of section 5: stock `0xDA12AC` = **0x269**, `0xDA12B0` = 0x26A; D33 `0xDA12AC` = **0x0**, `0xDA12B0` = 0x0. So in
D33 the building PS samples whatever texture handle 0 binds as its mask and its base layer, as the team says. Claim 10
**confirmed by own measurement**. (The white-mask fix, issue 3011, was not touched.)

## 9. Research quality (claim 11), corrections made, and what is not verified

Read: `insertion-point-and-frame-structure.md`, `shared-texture-and-compositor.md` sections 7-10,
`camera-and-picking.md` sections 5, 9, 10, `disable-systems-and-masks.md` section 5, `hooks-overlays-and-ceg.md`,
`shell/README.md`. Measurements are dated, carry the save, DLL, instance and configuration, and mostly carry [C]/[L]/[I];
earlier statements are corrected in place with a trace (e.g. the picking plane, the heightmap layout, the dark-red
cause). The places where this verification found a statement wrong or stale, each now corrected in place with a dated
note naming 9031:

| File, place | Was | Now |
|---|---|---|
| `shared-texture-and-compositor.md` section 8, resolution-change row | "2554x1411 -> 1914x1051 -> back" | the "back" step made a 130x10 chain; (2560, 1440) goes back |
| `camera-and-picking.md` section 10 item 6 (water) | 22 px -> ~0 px with `--water-z 12`, not yet verified | live: +23 px -> -5 px; EXE water outline near z = 10 [I] |
| `camera-and-picking.md` section 10, "B's failure modes are each detected" | every failure detected | NaN is not (V1) |
| `disable-systems-and-masks.md` section 5, memory/load table | -25 s launch -> in game | not reproduced (-14.0 / -12.9 s) |
| `insertion-point-and-frame-structure.md` section 7 | vptr swap preferred | superseded by option B (marked) |
| `insertion-point-and-frame-structure.md` section 4 | "Not yet verified live" | pointer to the live verification |

Not corrected (outside the files the verifier may edit; for the MC): `shell/README.md`'s error-path table says bad
`timeout_ms` values fall back to "defaults / limits" (V2: there is no limit) and that a `tag_match` matching nothing
"matches nothing" (V3: an empty or unconvertible one matches everything).

**Not verified here:** the one-hour soak and whether a stock game grows the same 155 MB (claim 9b; no stock soak run);
clicks at zoom 20.5 (the posted wheel did not move the zoom in that run); the strategic view (bug 3010, forbidden);
exclusive fullscreen, the Steam overlay, real alt-tab (Goldberg, cloaked); D3D creation failures and MinHook failures
(no fault injection, as the team states); the ASan build against the verifier's hostile cases (run on the plain build
only); V5 and V6 (code review only). The 0.9 % all-grey PrintWindow captures are explained as capture artifacts [I],
not proven.

## 10. The fixes of V1-V9 and of the water height (one-off 9035, 2026-11-13)

Written 2026-11-13 by the bugfix team's worker for one-off 9035 (issues 3012, 3014-3018), branch
`worktree-agent-ae97eaf883bbea2be`. Code in `shell/`; negative tests in `shell/tests/test_verification_defects.py` (new)
and `shell/tests/test_errors.py`; the error table in `shell/README.md` corrected. Each defect's state:

| Id (issue) | Fix [C: code] | Negative test (reaches the failing state the way the product does) |
|---|---|---|
| V2 (3014) | `timeout_ms` parsed as a 64-bit number and clamped to 0..50 ms (`kMaxTimeoutMs`); a value outside, or not a number, is logged once per config load as `CONFIG ERROR timeout_ms='...'`. `WaitThisFrame()`: no wait while the server is STALE, while the section names another texture than the one open (new generation or new server: the old key 1 never comes back, this was the hang), or while a resize is pending. After 8 timeouts in a row (`kTimeoutGiveUp`) no wait until an AcquireSync succeeds again (the zero-timeout path does so at the server's next frame); logged once, counted per stats line as `wait_off` | `test_timeout_ms_limited_and_killed_server[-1, 0, 4000000000, abc]` (server asks for a resize, then is killed; frames per 0.5 s and the log line), `test_timeouts_in_a_row_stop_the_wait` (server frozen with NtSuspendProcess) |
| V1 (3015) | `exe_camera.h`: `VPCamAllFinite` tests every element by its exponent bits (independent of `/fp`); `VPCamConsistency` and `VPCamWrapError` return +inf for any non-finite input or product and use `!(e <= max)` maxima. `CamRead` refuses a non-finite `g_Proj`, camWorld or any used view with INCONSISTENT; the server's `ServerConsistent` checks camWorld and every used view as well as the projection | `test_nonfinite_camera_never_published_ok` (9 cases: NaN / inf in view 0, a wrap view, the projection, camWorld, written into test_host's fake EXE image); `test_errors.py::test_server_refuses_bad_camera_block[nan-view0, nan-view0-x, nan-wrap-view, inf-camworld]` (server alone) |
| V3 (3016) | `ParseCond` converts the ini text by hand (no C locale): a character outside printable ASCII, or a present value with no token after parsing, makes the rule `impossible` (matches nothing) with the reason logged once per config load as `CONFIG ERROR tag_match: ...` / `tag_anchor: ...`. An empty value (`tag_match=`) still means the default and `tag_anchor=` / `-` still means no anchor | `test_unusable_tag_rule_matches_nothing[match " , ", match "clear_bb€" in a UTF-8 ini, anchor " + "]`: no insertion at all, every frame a counted miss, `ins_wrong` 0 |
| V4 (3017) | Stated rule: the target is the swap chain of the game's main window. A chain created through the factory is adopted at its first Present only if it presents to the target's window (the game's resolution change: same hwnd, `hooks-overlays-and-ceg.md` section 5) or the target's window no longer exists (`IsWindow`); otherwise it is logged as NOT adopted and listed as another chain. The 2 s silence rule still hands over when the target stops presenting. Presents of other chains beyond the 8 listed are counted and logged at most once per 10 s | `test_other_windows_swap_chains_do_not_take_the_target[1, 10]` (test_host `--extra-chains N`: N windows with chains made through the factory, presenting every frame); positive cases `test_new_chain_on_the_same_window_is_adopted` (`--recreate-same 1`) and `test_tagged.py::test_swap_chain_adopted_at_creation` (new window, old one destroyed) |
| V5 (3018) | `OpenSharedTexture` recreates the local texture when the format changes at the same size (logged) | `test_texture_format_change_at_same_size` (server `--tex-format rgba`, new test option) |
| V6 (3018) | `BuildGrid` refuses maps above 256 x 160 plots (twice the Huge map, 128 x 80, each way); was 1024 x 1024 | `test_errors.py::test_server_refuses_bad_camera_block[too-wide, too-high]` |
| V7 (3018) | `darkred.py`, `live3008.py` (and `stock_acc.py`, which uses it), `mapchange.py`, `turns_soak.py` take `-i/--instance`; `i2_capture.py` has no default instance any more | `test_live_tools_name_the_instance`, `test_live_tools_refuse_without_instance`, `test_darkred_refuses_without_instance` |
| V8 (3018) | already fixed 2026-11-11 (MC order after the 3200 incident): `build.cmd` refuses without `C5_SHELL_BUILD`; the stale comment about the asan default corrected | `test_build_cmd.py` (existing) |
| V9 (3018) | a plain "no Present seen yet" line at 10 s; the WARNING only 10 s after a swap chain was created through the hooked factory without a Present, or at 120 s | none (needs a host that presents through an unhooked path; the timing rule is code review only) |
| 3012 | `shell_server.exe --water-z` default 10 (was 12) | `test_errors.py::test_server_water_z_clamp_and_off` (all-water heightmap = flat z 10, pixel-exact) |

**The new tests against the unfixed code [C], 2026-11-13 01:10-01:16** (charter rule 2: each run once, failure kept).
Unfixed = commit 0388c13 exported to `D:/c5-64bit-work/4000/9035/base-src` with the new test files copied in; V4 and V5
on a hybrid (unfixed `shell_composite.dll`, fixed `test_host.exe` / `shell_server.exe`, which carry the new test
options). Builds for these single-test checks: `cl` as in `shell/build.cmd`, linking epic 3200's `c5render.lib`
(render unchanged); logs `D:/c5-64bit-work/4000/9035/unfixed-*.log` (not committed).

| Test (unfixed) | Failure line |
|---|---|
| `test_timeout_ms_limited_and_killed_server[-1]`, `[4000000000]` | `TimeoutError: no ' stats .*srv_new=[1-9]' in composite log within 10 s`: the last DLL line is `requested server texture 1280x720 (server has 1920x1080)`; the presenting thread hangs at the new generation (V2 reproduced) |
| `test_timeouts_in_a_row_stop_the_wait` | no give-up; the stats line shows `frames=12 ft_mean=42.844` per 0.5 s with the server frozen (every frame waits) |
| `test_unusable_tag_rule_matches_nothing` | `ins_wrong` 19,239 of 31,100 insertions (" , "), 21,450 of 34,707 ("clear_bb€" in a UTF-8 ini), 32,487 of 32,487 (anchor " + ", rule before) |
| `test_nonfinite_camera_never_published_ok` | OK blocks with a non-finite matrix: 977 of 977 (NaN view 0 translation), 978 of 978 (view 0 all NaN), 971 of 971 (NaN wrap view 1), 979 of 979 (g_Proj all NaN), 982 of 982 (inf g_Proj[0]), 750 of 1,173 (NaN camWorld, racing writer). inf in view 0, a wrap view or camWorld was already caught (pass, kept as controls) |
| `test_errors.py::test_server_refuses_bad_camera_block` | `[nan-view0]`, `[nan-view0-x]`, `[nan-wrap-view]` drawn (exit 0, dump written); `[too-wide]` (257 x 36) and `[too-high]` (56 x 161) drawn; `[inf-camworld]` already refused |
| `test_errors.py::test_server_water_z_clamp_and_off` | the default clamp draws flat z 12, not 10 |
| `test_other_windows_swap_chains_do_not_take_the_target[1]`, `[10]` | 2 adoptions: the extra 320x200 chain on window 'VP shell test host extra' becomes the target |
| `test_texture_format_change_at_same_size` | 0.991 of the free area still shows server A's last frame (should be < 0.05): the copy from the RGBA8 texture into the old BGRA local texture does nothing |
| `test_live_tools_name_the_instance` | 6 of 6: no `--instance` in the help (5) or `"c5h-b"` in the source (`i2_capture.py`'s default) |

`test_new_chain_on_the_same_window_is_adopted` is a positive test (the unfixed DLL adopts every new chain; it fails there
only on the new log wording). The same tests on the fixed code: all pass (quick build 01:05-01:09; the acceptance runs
on `shell/build.cmd` builds are below).
*Flake found 2026-11-13 (issue 3313, team 3300 W13) [L]: the test fails when `test_host --recreate-same 1` gets the
**same swap-chain address** for the new chain as for the released old one (heap reuse): the composite then sees its
current target and logs no adoption, and the 5-s wait times out. Two full composite-subset runs (16:37, 16:53) failed this
way (host.log "new swap chain 0FD76FD8" = the adopted 0FD76FD8; "0F3DD270" = 0F3DD270); the run alone passed with a new
address (10BACB78 after 0FAF61C0). The composite went on drawing into the new chain (stats `comp_n` ~13,000 per 500 ms
after the recreate) [L]; whether it refreshed its per-chain state (back-buffer views) for the new object at the old
address was not checked [I: a possible stale-state case, worth one look]. The test needs the host to keep the old chain
alive until the new one exists, or to accept "the same address presents again".*
*Fixed 2026-11-13 (issue 0141, account B) [L]: the cause was in the composite, not the test. A chain's identity was its
address alone, so a new chain at the released target's address passed `g_pendingAdopt == sc && sc != g_target` as false:
no adoption, and the composite kept the old chain's per-chain state (`g_bbRes`, the back-buffer views) for a new object
(the stale-state case above is real: no `DropTarget`/`AdoptTarget` ran). Now every chain created through the hooked
factory gets a creation number (`g_scSeq`, logged as `-> sc %p creation #N`; written before the pending pointer), the
target keeps its own (`g_targetSeq`; 0 = adopted without a seen creation, as the stand-in's first chain made before the
hooks), and a pending chain is new when its address or its number differs. A new chain at the target's address is
adopted by the same window rule; if it presents to another window it is adopted too ("the old target was released (its
address reused)": an address is reused only after its chain was freed). The reuse is logged once ("has the address of
the target ... which was released"). A chain the composite never saw created (`adopt_on_create=0`, or created before the
hooks) is still known by address only. Forcing the case: `test_host --recreate-same 1 --recreate-same-address N` keeps
each new chain at another address (never presented) and creates another until the old address comes back, then releases
the kept ones. Release-and-retry does not work: 5,000 tries all got the first try's address (the freed block is handed
out again at once). Keeping them got the old address back within 9 tries in most first rounds, but in 2 of 18 runs not in 100
tries (21:30, 21:31: the block was evidently taken by another allocation meanwhile [I]), so the host works in rounds: a
round that fails ends with the new chain presenting (adopted as usual), and 0.5 s later the next round starts from it,
up to 8 rounds (`--recreate-same-address 40` per round in the test; 4 of 20 runs needed round 2, 21:33-21:42). Negative
test `test_verification_defects.py::test_new_chain_at_the_old_chains_address_is_adopted` checks the behaviour first (a
second adoption at the old address within 5 s): unfixed composite 3 of 3 failed there (21:39, "the new chain at the old
address was not adopted"); fixed 10 of 10 passed with the positive test (`shell/evidence/0141/ACCEPTANCE.md`). Under MSVC ASan (`build.cmd asan`) the old address never comes back (100 kept
chains, 21:23): ASan's allocator holds freed blocks in its quarantine, so the test starts the host with
`quarantine_size_mb=0` appended to `ASAN_OPTIONS` (then 1 try, pass, 21:30). The same quarantine means the original flake
cannot happen under ASan at all; an ASan suite does not exercise address reuse unless a test turns the quarantine off.*

**Acceptance runs on `shell/build.cmd` builds [C], 2026-11-13** (owner 4000, each build + suite inside one
`quietlock.py run`, `memcap.py --mb 6144`, `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`): plain build 01:47:47 (33 s, 0 warnings),
suite **329 passed, 0 skipped**, 1,142.9 s, peak committed 711 MB; ASan build (`build.cmd asan`, 0 warnings), suite
**329 passed, 0 skipped, 0 ASan reports**, 1,226.4 s, peak committed 1,020 MB. Deselected in both (not run: PowerShell
hangs on this machine tonight): the 3 cases of `test_errors.py::test_ps1_tools_refuse_without_permission`.
Pitfall found: `ASAN_OPTIONS=log_path=D:\...\asan` unquoted makes every instrumented program stop at start-up with
"AddressSanitizer: ERROR: expected '=' in ASAN_OPTIONS" (the drive colon separates options): quote the path, as
`shell/README.md` says (`log_path="D:/.../asan"`). A first ASan run with the unquoted path (203 failed) was discarded.

**Live check of the water clamp z 10 (2026-11-13 03:52-03:59 EST) [L].** c5h-c (Goldberg, cloaked, guard self-test ok,
start-up watch ok), DLL `i2-shell-test` b92373aeb, CP/VP/EUI, huge save `C5H-VP8P-HUGE_0350` t350, the instance's
stock configuration (OverlayDebug and ColorKeyEdit off), `shell_composite.dll` built from this branch (`insert=present`,
`blend=alpha`, own section `Local\VPShell3012`), `shell_server.exe --scene grid --grid-scale 0.9 --heightmap
<i2-ground-truth huge heightmap>`; 9031's method and code (`v9031.py gridacc` + `gridmetric.py`, rerun on c5h-c) at
9031's open-ocean plots (86,19) (85,18) (86,20) (85,20) (86,18), same still camera for both arms (view change 0.0).
Zoom 16.256 (the wheel steps 2.0 from the load zoom 14.256 here, so 17 is not reachable; 9031 used 16.5):

| water-z | Centre line -> grid: median / within 1 px / p95 | Best shift grid - outline (px), score |
|---|---|---|
| 10 (the new default) | **0.0 px / 100 % / 1.0 px** | **(0, 0)**, 100 % |
| 12 (control, same camera) | 3.6 px / 39 % / 4.5 px | (0, -5), 100 % (9031's -5 px reproduced) |

So over open ocean the grid now lies on the EXE's water outline within 1 px. Files `shell/evidence/9035/ocean_z16_*`
(the shots stay in `D:/c5-64bit-work/4000/3012/`). Coast and other zooms not remeasured. c5h-c restored by hash
(DLL back to e589306e, front end restored, `shell.helper` removed, verify ok, leftovers clean).

**Not verified:** the water grid at coasts and at other zooms; V9's new timing in the game; the
V4 rule with a real overlay (Steam overlay, RenderDoc) in the game.
