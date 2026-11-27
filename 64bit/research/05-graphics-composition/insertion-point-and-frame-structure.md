# Insertion point and the game's frame structure

Where in the game's D3D11 frame the server's world must be drawn, what was learned about that frame on
2026-11-09, why the prototype's answer ("after ExecuteCommandList #34") is a hack, and what a robust answer needs.

## 1. What the game's frame looks like (live, B4 config, huge save turn 350)

| Property | Observed |
|---|---|
| Swap chain | blit model (DISCARD), 1 buffer, `R8G8B8A8_UNORM`, 2554x1411 (2560x1440 window), no MSAA, windowed |
| Back-buffer clear on the immediate context | **none**; the game records on deferred contexts |
| `ExecuteCommandList` calls per frame | ~47 in game, 2 on the load screen; "the list count depends on the scene" |
| `insert=clear` | never fires (`missed_ins` = every frame), as predicted by stand-in run 19a |
| Resolution change | creates a NEW swap chain (not ResizeBuffers). *Corrected 2026-11-11 (3008, [L]): in the windowed D33 run the new swap chain used the SAME window (hwnd 0x007908BA before and after, three changes); was: "and window"* |
| Outside window resize | no ResizeBuffers call; the game stretches |

The stand-in had already shown the mechanism: when the host records its clear on a deferred context and runs it
through `ExecuteCommandList`, clear-based insertion never fires (run 19a), while `exec N=1` works (run 19b). The
compositor's clear/omset hooks only see the immediate context; deferred-context work arrives as opaque command
lists.

## 2. The 33/34 boundary

`tools/scan_exec.py` stepped `nth` from 1 to 48 in `insert=exec` mode, dumped the back buffer each time and scored
the server's hex palette in the map area (`live/scan/`):

| nth | Result |
|---|---|
| <= 33 | the world is overdrawn: the scene's clear is in an early list |
| 34 and later | the world shows, with units, flags, banners and HUD on top (`live/scan/compare_34_47.png`) |
| 47 | units are covered |

The 33/34 boundary was stable in that view. The 15.5-minute soak ran at exec 34 with 1 missed insertion.

## 3. Why a fixed index is fragile

- The **order of the later lists varies frame to frame**: units and leftover red Terrain patches move between
  indices (at 47 units are covered).
- The **list count depends on the scene** (2 on the load screen, ~47 in game).
- GATES calls the fixed index "a prototype hack (condition 1)": it "works for this view only".

## 4. The robust design (GATES condition 1)

1. Hook `ID3D11Device::CreateDeferredContext`, and on each deferred context hook `ClearRenderTargetView` and
   `OMSetRenderTargets`.
2. Tag the command list that **clears the back buffer**, and the one that **starts the UI pass**.
3. Insert after the tagged clearing list (and, for units-in-the-shell, before the unit pass - see below).
4. First take a **RenderDoc capture of one B4 frame** to name the lists. RenderDoc 1.47 portable with the x86 capture
   layer is installed (`%USERPROFILE%\tools\renderdoc-1.47`); no capture has been taken yet. Do not run RenderDoc and
   the composite DLL in the same session (RenderDoc wraps the device).

**Status 2026-11-10 (issue 3006):** steps 1-3 are built and pass on the stand-in (section 8), with inline hooks instead
of vtable patches and with the rule chosen in the ini, so the RenderDoc capture of step 4 (issue 3005) only has to name
the lists; no rebuild is needed to aim the rule. Not yet verified live. *(Verified live 2026-11-11: section 9, and
independently by verifier 9031, shell-foundations-verification.md section 6.)*

The compositor-README's pre-live checklist also lists alternatives still open if the trace shows them: an
offscreen world target resolved or blitted into the back buffer (then `omset` after the resolve is the UI start),
or a `ResolveSubresource` / `CopyResource` into the back buffer with no clear or bind in between (needs an insertion
after `ResolveSubresource`, slot 57 - not implemented). Effort for "deferred-context insertion and inline hooks":
30-60 agent-hours (REVIEW.md section 7, rough, before stub sizing).

## 5. Units stay in the shell: the composite must go under the unit pass, and needs depth

The pragmatic first increment (REVIEW.md section 5 item 2; STUBS.md "Split 1") is: the server renders terrain,
rivers, landmarks, FoW, decals, borders; the EXE keeps units, cities (and particles). Granny is statically linked,
32-bit and closed, so animated units cannot simply move to the server. That requires:

- the composite to sit **above the scene clear and below the units' pass** - exactly the deferred-context tagging
  above (GATES: "Once Terrain and Unit are stubbed ... the composite must also go UNDER the units' pass but over the
  scene clear, which needs exactly this");
- a **depth hand-off**: today the compositor draws under everything with no DSV bound, so server hills cannot
  occlude EXE units ("Units composite in front of everything", coupling.md symptom g). "The server terrain needs to
  reach the EXE's depth buffer before units draw, or the server draws the units' occluders. This is a Gate-3
  compositor extension, not a system stub" (coupling.md 5.2 item 5).
- the EXE's heights must match what the server renders: units, cities, flags and banners read the global
  heightmap 0xD91270 every frame, filled by the per-load bake; the proposed interface is a load-time shared
  ushort array (W*64 x H*64, height = v/255*0.2) fed through `GridHeightmap::Init` 0x47E350 and setter 0x5A3490
  (coupling.md 5.2 item 1).

Estimates (STUBS.md, 2026-11-10): Split 1 integration ~30-60 h on top of stubs ~10-20 h; the depth hand-off
(10-25 h) is the largest item. Losses in Split 1: unit/city shadows and water reflections (part of the Terrain
render), minimap (black). Split 2 (EXE keeps only UI) adds ~45-100 h.

## 6. Windows 11: per-object context vtable, rewritten by the runtime

Found on the stand-in, confirmed in the game logs:

- On Windows 11 (d3d11.dll 10.0.26100) the `ID3D11DeviceContext` vtable is a **per-object table in heap memory
  right behind the context object** (log example: `ctx 030C00E0`, `vtbl 030C00E4`). A dummy device's context
  vtable is therefore useless for hooking the game's context; the DLL patches the real immediate context, found via
  `GetDevice -> GetImmediateContext` at the first `Present` of the target swap chain.
- **The runtime restores the `ClearRenderTargetView` slot inside every `Present`** (`slot_rewrites` equals the
  frame count in every run; right after the first patch the next frame's clear was not seen) **and inside every
  `ResizeBuffers`** (found by the soak: 29 missed insertions for 29 resizes).
- `OMSetRenderTargets` and `ExecuteCommandList` slots were never rewritten.
- Fix: re-assert the context slots right after each original `Present` and `ResizeBuffers` returns (one compare per
  slot; the table is writable heap memory, so no `VirtualProtect`). If `slot_rewrites` drops to 0 while clears are
  missed, "the runtime changed how it rewrites the table" and the re-assert point may need to move.
- Implication: vtable patching on this runtime is a moving target; this is one reason GATES recommends inline
  hooks (MinHook) or drawing from the EXE's own render path for a product (hooks-overlays-and-ceg.md).
- **Correction 2026-11-10 (issue 3006, probe on the stand-in [L]):** without any patch, the immediate context's
  `ClearRenderTargetView` slot held the same function before and after 6 Presents and ExecuteCommandLists; what the
  prototype measured was the runtime restoring its own value over our patched slot. The DLL no longer patches slots
  (MinHook inline hooks on the functions), so the re-assert is gone. Immediate and deferred contexts, and devices of
  different feature levels, use different implementation functions: hooks-overlays-and-ceg.md section 2.

## 7. Camera, for when the server draws the real world

Static analysis only (CAMERA.md, summarised in GATES.md): no DLL interface or Lua function returns camera matrices.
`g_Civ5App = *(void**)0xCC8E74`; `GameViewState::UpdateViews` 0x51DF70 (main thread, once per 3D frame) calls
`FCamera::GetViewProj` 0x8E4A80 and writes fixed globals: projection 0xDA12D0, views 0xDA1310 + 64*i (count at
0xD90FA4, up to 3 for map wrap), inverse views 0xDA1410, frustum planes 0xDA1510. Preferred read path: swap the map
camera object's heap vtable pointer (slot 2 `GetViewProj`) for a DLL thunk, a heap-only write that respects the
no-`.text` rule. *(Superseded 2026-11-11: the read was built as option B, polling at every Present with no write at all;
camera-and-picking.md section 10 says why. Noted by verifier 9031.)* Open: Strategic View's own camera (object 0x1A0ADA0), whether other scenes overwrite the globals
mid-frame (the leader screen reuses them), runtime grid constants. Picking intersects a flat plane and does not
need Terrain (STUBS.md).

## 8. The tagged insertion point (issue 3006, stand-in only, 2026-11-10/11) [L stand-in; verified live 2026-11-11, sections 9 and shared-texture-and-compositor.md 8]

Code: `shell/composite/shell_composite.cpp` (`insert=tagged`), stand-in `shell/standin/test_host.cpp --game`, tests
`shell/tests/test_tagged.py`, measurement `shell/tools/measure_tagged.py`. Build `cmd /c shell\build.cmd`.

**Tagging.** Inline hooks (hooks-overlays-and-ceg.md section 2) on every deferred context's `ClearRenderTargetView`,
`ClearDepthStencilView`, `OMSetRenderTargets` record, per context while it records: `clear_bb` / `clear_rt` (back
buffer or another target), `clear_dsv`, `bind_bb` / `bind_bb_dsv` / `bind_rt` / `bind_rt_dsv`, `nested` (a list
executed inside it), the render-target and depth formats (DXGI numbers), the number of clears, and the context's index
(order first seen). `FinishCommandList` attaches them to the command list; `ExecuteCommandList` on the immediate
context looks them up (and forgets them unless `tag_keep=1`: a list executed twice is reported `untagged` the second
time). The back buffer is recognised by resource identity with swap-chain buffer 0, from any thread.

**Rules, chosen in the ini and switched live** (`tag_rule`, `tag_match`, `tag_anchor`, `tag_nth`):
- `after`: after the executed list matching `tag_match` (default `clear_bb`), drawn on the immediate context.
- `before`: before the `tag_nth`-th list matching `tag_match` after the first list matching `tag_anchor` (defaults
  `bind_bb_dsv` after `clear_bb`: just before the 3D pass).
- `inlist`: recorded into the deferred context right after its back-buffer clear, on the recording thread, sampling
  the local copy; the immediate context does AcquireSync + copy just before that list executes (keyed-mutex sync
  cannot be recorded into a deferred list).
- Conditions are conjunctions of tag names, `!tag`, `dsv_fmt=N`, `rt_fmt=N`, `ctx=K`, `nclears>=N`; an unknown token
  matches nothing and is logged. The `.trace` trigger prints every executed list of a frame with its tags and where
  the insertion went, so the rule for the game can be written from a trace or from the RenderDoc capture (3005)
  without a rebuild. Example for the game if the capture shows the scene clear and the unit pass in separate lists:
  `tag_rule=before`, `tag_match=bind_bb_dsv,dsv_fmt=45` (45 = D24_UNORM_S8_UINT), `tag_anchor=clear_bb`.
- Per frame the DLL counts `missed_ins` with a reason (`miss_nomatch`, `miss_noanchor`, `miss_noexec`,
  `miss_notready`) and `ins_wrong`: insertions that landed outside the window "after the list that cleared the back
  buffer, before the next list that binds the back buffer", judged from the tags whatever the mode.

**Stand-in frame** (`test_host --game --vary 1 --mt 1`): 7 deferred contexts; clear (back buffer + D24S8, both
cleared), terrain (R11G11B10 offscreen + D32), units (back buffer + depth, depth test LESS: landmark, two units), UI
(back buffer, no depth, alpha), 0-3 offscreen "misc" lists; four lists recorded on four worker threads; terrain
position and misc count vary per frame (5.5 lists per frame on average).

**Results** (`pytest shell/tests`: 15 passed, 2026-11-11 00:01, hidden desktop):

| Check | Result |
|---|---|
| `after`, `before`, `inlist`, each ~6 s | 0 missed, 0 outside the window, 0 untagged after the first second; dump: free map area > 90 % server palette, < 1 % clear grey; landmark, both units (depth order kept) and UI bar pixel-exact on top |
| fixed `exec nth=2` with the varying order | lands outside the window in > 20 % of frames (measured 23.8 %, below) |
| rule matching nothing (`tag_match=dsv_fmt=999`, and an unknown token) | every frame `missed_ins` = `miss_nomatch`, no composite call, nothing drawn; units and UI intact |
| no server | "no server: section ... does not exist" and `server STALE` logged, stale=1 in every stats line, insertion still fires, nothing drawn (`nodraw`) |
| server killed (`stale_ms=500`) | `hide_when_stale=0`: last frame keeps showing; `=1`: nothing drawn |
| kill switches / mode=off | see hooks-overlays-and-ceg.md section 2 (prologue bytes read back from the host) |
| new swap chain + window (`--recreate-at`) | adopted at its first Present; old rule 1.98 s (hooks-overlays-and-ceg.md section 5) |

**Cost, tagged vs exec** (`measure_tagged.py 10 2`, 2026-11-11 00:07-00:09, after the quiet-machine lock of 9014 was
released; one other `CivilizationV` process was running on the machine; host uncapped on a hidden desktop, where
every Present reports occluded; server 60 Hz; 2 rounds x 8 intervals of 1 s per mode, first interval dropped):

| mode | frames | host frame ms | composite CPU ms/frame mean / p99 | deferred record ms mean / p99 | AcquireSync ms mean | missed | outside clear..3D |
|---|---|---|---|---|---|---|---|
| passthrough (hooks + tags, no draw) | 107,650 | 0.150 | - | - | - | 0 | 0 |
| composite exec nth=2 | 106,821 | 0.151 | 0.0126 / 0.053 | - | 0.074 | 0 | 25,450 of 106,821 (23.8 %) |
| composite tagged after | 112,211 | 0.144 | 0.0111 / 0.038 | - | 0.073 | 0 | 0 |
| composite tagged before | 117,974 | 0.136 | 0.0102 / 0.032 | - | 0.078 | 0 | 0 |
| composite tagged inlist | 111,285 | 0.145 | 0.0042 / 0.019 (acquire + copy only) | 0.0076 / 0.020 | 0.055 | 6 [I: `notready` in the first frames after load; none nomatch/noexec] | 0 |

The tagging itself costs nothing measurable at ~6,700 frames/s (passthrough vs composite differences are inside the
run-to-run spread, as in the prototype's A/B). `inlist` moves ~0.008 ms per frame of drawing work onto the recording
thread. With the exec index, "missed" stays 0 while a quarter of the insertions are misplaced: a missed counter alone
cannot judge an index rule, the tag window can.

**Live answers 2026-11-11 (3005/3008, section 9 and shared-texture-and-compositor.md section 8) [L]:** the game clears the
back buffer in exactly one deferred list per frame (with its D24S8 depth); the 3D lists bind that depth (DXGI 45);
no list was executed twice (0 untagged); the game's device added no hook targets at adoption (18 targets, "0
changed"), so there was no freeze at adoption; the Steam overlay is not in these Goldberg instances (not testable).
(Was: "Not verified live, open for 3005/3008:") Whether the game clears the back buffer in a deferred list at all (or
resolves/copies into it: then `clear_bb` never appears and the rule must anchor on a bind), which DSV format the unit
pass uses, whether lists are executed more than once (`tag_keep`), whether the game's device has implementation
functions the dummy did not show (logged at adoption), the cost of the one thread freeze at adoption inside the game,
and coexistence with the Steam overlay.

## 9. The game's command lists, named live (issue 3005, 2026-11-11) [L]

Setup: c5h-b, D33 configuration (disable-systems-and-masks.md section 5, `clear=0x1`), DLL `i2-shell-test` b92373aeb,
CP/VP/EUI, huge save `C5H-VP8P-HUGE_0350` t350, 2554x1411 back buffer, `shell_composite.dll` (this branch; trace
extended with every format, clear/bind counts and target/depth sizes per list) in passthrough, then composite with
`shell_server.exe --scene test`. No RenderDoc capture (section 9.4). Evidence `shell/evidence/3000/live/` (trace tables,
contact sheets), full traces and shots `D:/c5-64bit-work/3000/live/w5/r1/`.

### 9.1 The frame, phase by phase

Six traced frames at the load-default view (zoom 14.26, Memphis area) and four at a zoomed-in city view each show the
same order; only the number of lists per phase changes with what is on screen (47-54 lists in these views, **5 in an
empty ocean view**: one per phase):

| Phase (order) | Lists (city views / empty ocean) | Render target | Depth | Clears | Recorded on |
|---|---|---|---|---|---|
| A. Offscreen full-size pass | 18-22 / 1 | R16G16B16A16_FLOAT (DXGI 10), 2554x1411 | none | the first list of the phase clears it | worker contexts |
| B. Offscreen square pass | 1 / 1 | R16_UNORM (56), 2048x2048 | none | none | worker |
| C. Offscreen pass with depth | 7-11 / 1 | R8G8B8A8_UNORM (28), 1024x1024 | **D24_UNORM_S8_UINT (45), 1024x1024** | first list clears colour and depth | worker |
| **D. Scene clear** | **exactly 1** | back buffer (28) | **D24_UNORM_S8_UINT (45), 2554x1411** | **back buffer and depth**; tags `clear_bb|clear_dsv|bind_bb_dsv` | any worker context |
| E. 3D in the back buffer | 16-19 / 0 | back buffer | same D24S8 2554x1411 | none | workers (and 0-2 lists on context 0 at the end) |
| F. UI | exactly 1, last | back buffer | **none** (`bind_bb`, no DSV) | none | **context 0**, the main thread's |

- Position of the scene clear in the frame: list 28 to 33 of 47-54 (frame to frame), 4 of 5 in the empty view: the old
  `exec 34` worked by luck and the 33/34 boundary of section 2 is this varying position.
- Nothing is cleared or bound on the immediate context (0 clears, every immediate `OMSetRenderTargets` is the
  runtime's own null bind after each `ExecuteCommandList`). No list is executed twice in these frames (0 untagged with
  `tag_keep=0`).
- Contexts: 9 deferred contexts; #0 records the load screen and, in game, the UI list (and sometimes 1-2 back-buffer
  lists just before it); #1, #3-#8 are seven worker contexts that take any phase in rotation, so **a context index
  names nothing; the tags do**.
- What A-C hold is not visible in the back buffer and is [I] without a capture: A scales with the number of visible
  objects (18-22 lists vs 1) and binds no depth (a per-object pre-pass into a float target); C (1024x1024 colour +
  D24S8, also scaling with objects) has the shape of a shadow or reflection map; B is one list whatever the view.

### 9.2 Which lists draw what: the relative scan (`tag_rule=before`)

The test scene (opaque) was composited before the N-th `bind_bb_dsv` list after the scene clear, N = 1..22, and the back
buffer dumped each time (`shell/evidence/3000/live/3005_scan_before_bbdsv_crop.jpg`; per N: `live3008.py ini
tag_rule=before tag_match=bind_bb_dsv tag_anchor=clear_bb tag_nth=N`, then `live3008.py shot`): with N = 1 the dark-red landmark and city
models (D33, issue 3009) all lie on top of the server image; by N = 7 they are gone from the top, while units (riflemen,
ships, cavalry) stay on top until N = 13-14; units are drawn by the later lists. With N past the frame's count of 3D
lists the rule finds nothing and the frame shows the EXE's scene clear colour (grey, RGB 84,84,84), counted as
`miss_nomatch`. Rule `before bind_bb,!bind_bb_dsv` (the UI list): all 3D under the server image, unit flags, city
banners, tooltips and the HUD on top. So, in order: **scene clear -> landmark and city lists -> unit lists -> UI list**.
The grouping is by order, not strict: the job system interleaves within phase E, so a rule between landmarks and units
cannot be an index; for "server under units, over landmarks" (not wanted in Split 1) a tag that tells the passes apart
would be needed (draw counts or shader identity, not recorded).

**Chosen rule: `insert=tagged`, `tag_rule=after`, `tag_match=clear_bb` (the ini default).** Live it fired in every frame
(e.g. 317 of 317 frames, 0 missed, 0 outside the clear..3D window, 47.5 lists per frame) and put the image over the
scene clear and under landmarks, cities, units and UI at zoom 11, 17 and 20.5 (`zooms_test/sheet.jpg`).

**Missed insertions of the chosen rule, by reason (issue 3008, 2026-11-11; W5 03:58-04:39 and W7 06:05- on c5h-b, D33,
huge save) [L]:**

| Reason | Counter | When | Count seen |
|---|---|---|---|
| Load sequence: load-screen frames without a list or without a back-buffer clear, then 33 frames of ~115 lists with no clear just before the map appears | `miss_nomatch` (1 `miss_noexec`: a frame with no list) | every load, before the in-game UI | 41 frames per huge-save launch, identical in all four (W5 x3, W7 06:07); 22 for the Tiny save loaded from the main menu in the same process (W7 07:23), plus 1 `noexec` at the exit to the menu. The main menu itself is not missed: its frames have a back-buffer clear, so the image is drawn under the menu (covered by it) |
| AI leader screen (diplomacy, trade, discussion) | `miss_nomatch` | every frame the screen is up: its frames have no scene-clear list | 19,511 frames over 42 screens in 30 AI turns (W7), 0 seconds unexplained within 4 s of a screen |
| Resolution change | `miss_nomatch` | the new swap chain's first frame (its lists were recorded before the chain was adopted) | 1 per change (W5) |
| Deliberate | `nomatch`/`noexec` | kill switches `hook_clear/finish/exec=0`, wrong-rule tests, rule scans past the list count | W5 only |
| Normal map frames, in AI turns and idle | - | - | **0** (W5 1,202 stats seconds; W7 30 AI turns and the one-hour soak, 3,564 stats seconds and 1.03 million frames, shared-texture-and-compositor.md section 9) |

`miss_noanchor` and `miss_notready` were never seen live. Nothing is drawn in a missed frame, which is the wanted
behaviour on the leader screen (nothing over the leader) and harmless on the load screen.

### 9.3 Depth buffers (brief item 8, findings only)

*Answered 2026-11-11 by epic 3200 (issue 3208, live): clear value 1.0 (DEPTH only, stencil never cleared), depth
function LESS_EQUAL with writes on at each list's last draw, stencil off; nothing bound at the insertion. Details and
the server-depth conversion: `shared-texture-and-compositor.md` section 12.*

- The main 3D phase (E) and the scene clear bind **one D24_UNORM_S8_UINT depth view, 2554x1411** (the back-buffer
  size), cleared once per frame in the scene-clear list; the UI list binds no depth. A second D24S8, 1024x1024, belongs to
  the offscreen phase C. A and B bind no depth.
- The composite runs right after the scene-clear list, so the EXE's depth buffer at that point holds the clear value
  only: **a depth hand-off can write there** (the composite already binds the back buffer; binding the game's DSV
  as well and writing the server's depth, converted to the EXE's projection, with depth test ALWAYS / write ON would put
  server terrain into the depth the landmark and unit lists then test against). The DSV is reachable without new hooks:
  it is the DSV bound in the scene-clear list (the deferred `OMSetRenderTargets` hook sees the view; keep a reference
  per frame). Needed and not yet known [I]: the clear value and depth function the EXE uses (reversed or not), whether
  the server's depth (its own projection) can be re-projected with the EXE's g_Proj (camera-and-picking.md section 10:
  near 50, far 4000, fov 29), and the stencil use of the 3D lists.
- The EXE's models must also stop drawing what the server draws (Split 1); with D33 the terrain pass is not drawn, so
  the depth buffer before phase E really is empty of terrain (the reason models sit on nothing but the clear colour).

### 9.4 The RenderDoc capture: no compliant route

RenderDoc must be in the process before the D3D11 device exists. The DLL's `shell.helper` loader runs at
DllGetGameContext (main menu), long after; `renderdoccmd capture` launches the EXE itself (outside the harness: no
guard, no gates); `renderdoccmd inject` writes into the running game (forbidden, and too late for D3D11); RenderDoc's
global hook needs AppInit_DLLs (registry); a proxy d3d11/dxgi in the instance's install is an undeclared instance change.
The harness has no option to load another DLL at start (only the guard: route "import" wrapper or `--inject
focus_guard.dll --early`). A harness change request (a `--preload` option with an allowlist, loaded by the import
wrapper's DllMain) went to the coordinator for the MC with 3005's report. The lists are named from the trace and the
scans above instead; the contents of phases A-C stay [I] until a capture.

Granted 2026-11-11 by the MC as decision D57, one-off 9027 (not built by team 3000). The DLL to preload [C]:
`%USERPROFILE%/tools/renderdoc-1.47/x86/renderdoc.dll`, SHA-256
`54a75e46160f2abc4d20d734078893942bffc0f5d1472faddd728f173864bf40`. It is the x86 capture layer from the official
RenderDoc 1.47 Windows release `RenderDoc_1.47_64.zip` (with its `.sig`), downloaded 2026-11-09 17:21 EST to
`%USERPROFILE%/tools/_downloads/` and checked against the RenderDoc GPG signature
(`research/06-tooling-and-workflows/reverse-engineering-setup.md`, TOOLING.md). The 64-bit `renderdoc.dll` in the folder root
(`a81e3f5f...4726`) is not the one to load. Captures go under `D:/c5-64bit-work/3000/renderdoc/`; the overlay and hotkeys off
through the helper (`MaskOverlayBits(0,0)`, no `SetCaptureKeys`).

Built and used 2026-11-11 by one-off 9027 (the route is no longer missing): `cli.py launch --preload` + `cli.py
capture`, captures of a Tiny-map frame (51 lists, 572 draws; phases A-C confirmed by bound target) and the huge-save
limit (the 32-bit address space runs out at the capture): `renderdoc-capture.md`.

Sources: `research/sources/shell-gates/GATES.md` (Gate 3: Live in the game, verdict, Camera ownership),
`compositor-README.md` (Composite DLL, Results (i) and (v), checklist, limitations), `STUBS.md` ("The two splits"),
`stubs/coupling.md` sections 5.1-5.2 and the effort table, `tools/scan_exec.py`, `research/sources/REVIEW.md`
sections 5 and 7, `research/sources/memory-notes/civ5-shell-gates.md`.
