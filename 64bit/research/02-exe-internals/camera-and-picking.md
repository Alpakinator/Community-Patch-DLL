# Camera, view/projection and picking

Sources: `shell-gates/CAMERA.md` (2026-11-09/10, static analysis only: llvm-objdump + read-only Ghidra 12.1.4
decompile of a copy of the project; the running game was not touched), `shell-gates/GATES.md` ("Camera ownership"
and Gate 1 checklist), `shell-gates/stubs/coupling.md` (picking/heightmap), `shell-gates/stubs/terrain.md`.

DX11 VAs at ImageBase 0x400000 (TimeDateStamp 0x546CD0A8); DX9/Tablet table in section 7. Tags: **[C]**
confirmed in disassembly (and Ghidra where noted), **[I]** inferred, **[L]** needs a live run. *Corrected
2026-11-11 (epic 3000, issue 3004): section 9 holds values read from the running game; there "[L]" means verified live
(was: "Nothing in this file has been read from a running process yet except picking behaviour").*

## 1. Short answers

| Question | Answer | Conf. |
|---|---|---|
| Camera state | One heap object: `GameViewState` (GVS) = `*(g_Civ5App + 0xC0)`, `g_Civ5App = *(void**)0xCC8E74`. `FCamera` at GVS+0x48: FOV degrees, aspect, near, far, 4x4 camera-to-world (rows right / up / back / position). Zoom = camera height Z; the zoom *level* is `(20/3)*ln(Z/50)`. Yaw/pitch exist only as matrix rows | [C] layout, [I] zoom-level meaning |
| Exact view/projection | `GameViewState::UpdateViews` 0x51DF70 writes the final matrices to fixed `.data` globals every 3D frame (projection 0xDA12D0, views 0xDA1310 + 64*i, count 0xD90FA4, inverse views 0xDA1410, frustum planes 0xDA1510) | [C] |
| World/hex/screen | hex->world via matrix 0xD91030 + grid scale/offset + height field; world->hex via 0xD90FF0 + cube rounding; screen via `D3DXVec3Project` with viewport 0x2DD1FB8; picking 0x6668F0 unprojects onto a flat plane | [C] pipeline, [L] runtime constants |
| Can the DLL read it every frame? | Yes, with no `.text` writes; the root pointer is the one PR #13379's signature already resolves. **Built 2026-11-11 (issue 3007)** in `shell_composite.dll` as option B (read at every Present, read twice, checked against the map camera; no write at all): section 10 | [C] addresses and patterns (checked against the EXE file), [L stand-in only] behaviour; live run pending |
| Existing interface? | **None.** No DLL interface (`ICvDLLUserInterface`, `ICvEngineUtility`, ...) or Lua function returns camera, matrices or picking data; only setters (`lookAt(plot, CameraLookAtTypes)`, `centerCamera`, `releaseLockedCamera`, `lookAtSelectionPlot`) and the `CITY_ZOOM_*` defines (500/700/800 = camera heights, offset 20) | [C] |

## 2. Object graph

```
g_Civ5App = *(Civ5App**)0xCC8E74                      [C] set at 0x602451, cleared at 0x60248E
 +0x0C0  GameViewState*  (0x8D0 bytes, .\App\Civ5App.cpp line 0xF1F, ctor 0x64A860, vtbl 0xA20908)
 +0x0C4  0x5C8-byte object (ctor 0x60CA10)
 +0x0C8  0x724-byte object (ctor 0x523150)
 +0x0D0  InterfaceBuddy*  (PR #13379 name; +4 is the ICvDLLUserInterface the DLL receives)
 +0x13AC float "Minimum Zoom Level" (config.ini [User Settings], default 11.0)
```

GVS vtable slot 3 = `GameViewState::Update` 0x7253E0 (contains the string `GameViewState frame stalled`).

| GVS offset | Contents | Conf. |
|---|---|---|
| +0x044 | CameraController (name inferred; ctor 0x41BE50; first dword 0x40, no vtable) | [C] |
| +0x048 | FCamera (vptr 0xA9A1B0, ctor 0x8E5170) | [C] |
| +0x050 | fovY in **degrees** | [C] |
| +0x054 | aspect (backbuffer w/h) | [C] |
| +0x058..+0x074 | 4 (velocity, damping) pairs for `FCamera::Update` 0x8E4D70 | [C] |
| +0x078..+0x0B7 | camera-to-world 4x4, D3DX row-major: +0x078 right, +0x088 up, +0x098 back (view direction is -row2, getter 0x8E4A10), **+0x0A8 position xyz** | [C] |
| +0x108..+0x114 | frustum extents (ctor, from FOV) | [I] |
| +0x118 / +0x11C | near / far | [C] |
| +0x120..+0x128 | controller target xyz (+0x128 = target height) | [C] |
| +0x12C | pan speed (1.0) | [C] |
| +0x134 | zoom base height 50.0 | [C] |
| +0x130 | zoom step per wheel notch, 2.0 levels (ticket 0781) | [C] |
| +0x138 / +0x13C | min / max zoom level 11.0 / 20.5; every zoom step clamps to them as read at that step, so a heap write of +0x13C raises the maximum (ticket 0781, `research/05-graphics-composition/camera-extras.md` 2) | [C], [L] 2026-11-25 |
| +0x140 | current zoom level = `ln(targetZ / 50) * 6.6667` (0x4050C1, 0x55697D, 0x7422DE) | [C] |
| +0x144 | controller clock | [C] |
| +0x148 | key-motion bits (pan 1/2/4/8, rotate 0x10 / 0x20 = yaw +40 / -40 degrees a second, 0x40/0x80 pitch: no effect on the map camera). `Events.CameraStartRotatingCCW` sets 0x20 [L]; *corrected 2026-11-25 (ticket 0781): meaning was [I]* | [C] bits, [L] rotate |
| +0x149 | bit 0 = inertial glide | [C] |
| +0x150 | wrap sign (+-1) | [C] |
| +0x154..+0x160 | camera clamp box minX, maxX, minY, maxY. *Corrected 2026-11-25 (ticket 0781) [L]: not the map bounds: on a turn-18 Tiny save it was x -1440..-928, y 55..499 against a map of x +-1791.5, i.e. the bounds of the explored area [I]; was "(from the map bounds)". The check 0x47E750 / clamp 0x463510 tests the camera's ground point in y but the camera's own x, so it is exact only at yaw 0 and 180 (`camera-extras.md` 1.2)* | [C], [L] |
| +0x164..+0x1A4 | drag samples / glide | [C] |
| +0x1B4 | per-view active-systems mask (see `disable-systems-and-masks.md`) | [C] |
| +0x1B8 | render pass selector, copied from `[app+0x690]` | [C], meaning [I] |
| +0x1C0 | byte: world view active (Update returns early when 0) | [C] |
| +0x758 | view mode: **1 = 3D map, 2 = Strategic View** | [C] |
| +0x760 / +0x764 | mouse position in NDC | [C] (Ghidra) |
| +0x768 / +0x76C / +0x770 | mouse-over hex (cube x, y int; z float) | [C] (Ghidra) |
| +0x790..+0x798 | mouse ground point (world) | [C] (Ghidra) |

`InterfaceBuddy+0x90` = mouse-over **plot** as `int16 gridX, int16 gridY`, written by 0x6881D0 from the picking
function, read by `UI.GetMouseOverHex` (Lua C function 0x6F5940, reads `[[g_Civ5App+0xD0]+0x90]`). Despite the name
these are grid coordinates (Lua passes them to `Map.GetPlot`). [C]

### FCamera (vtable 0xA9A1B0: [0] dtor 0x8E5320, [1] Update(dt) 0x8E4D70, [2] GetViewProj 0x8E4A80) [C]

| VA | What |
|---|---|
| 0x8E4A80 | `GetViewProj(D3DXMATRIX* view, D3DXMATRIX* proj)`: view = D3DXMatrixInverse(camWorld); proj = D3DXMatrixPerspectiveFovRH(fov*pi/180, aspect, near, far). thiscall, `ret 8` |
| 0x8E5030 | GetFrustumPlanes(D3DXPLANE out[6]) |
| 0x8E4C00 / 0x8E50F0 | LookAt(eye, at, up) / LookAt(at, up) |
| 0x8E4940 / 0x8E4960 | SetFovAspect / SetNearFar |
| 0x8E49B0 / 0x8E49D0 | set / get position (+0x60) |
| 0x8E4A50 | get matrix row i |
| 0x8E4D20 / 0x8E4CC0 | multiply camWorld by RotationX / Translation |

### Controller (`this` = GVS+0x44) [C]

| VA | What |
|---|---|
| 0x6FBA80 | `GameViewState::UpdateWorldCamera`: `if (GVS+0x1C0)` controller.Update (0x741EB0), then UpdateViews 0x51DF70. Called from GVS::Update at 0x72591F and once at load (0x4034DC). The stub notes call the same function "the per-frame world update of every system" (it also runs the gated per-system updates, e.g. 0x6FBB93, 0x6FBCDF) |
| 0x741EB0 | controller update: glide, key panning 0x669B60, yaw about the target 0x65EE30 (about the point where the view axis meets z = 0, 40 degrees a second; ticket 0781), map-seam wrap 0x5E6AE0, clamp 0x463510; writes max / min / level to 0xD90FB8 / 0xD90FB4 / 0xD90FB0 and the zoom fraction to 0xD90FAC every frame |
| 0x556940 | SetPosition(vec3): target + camera position, recomputes zoom level |
| 0x4A3C00 / 0x4728C0 | SetFovAspect / SetNearFar wrappers |
| 0x6C9770 | RotateX (pitch) |
| 0x41E020 | Reset (target 0, speed 1, base 50, zoom limits 11 / 20.5) |

## 3. Projection parameters [C unless noted]

From the GVS camera init in 0x4686C0 and the per-frame 0x6F3ED0 (called at 0x72595C, after the views are built):
- `aspect = backbufferW / backbufferH` (`*(int*)0x301056C` / `*(int*)0x3010568`).
- `fovY_deg = sqrt(K^2 / (1 + aspect^2))`, K = 80 (literal 6400 at 0x4B33C2 and 0x6F438A). At init K comes from
  0x4D3AC0 and the result is floored at 26.5 deg. About an 80 deg diagonal FOV: 39.2 deg vertical at 16:9, 48 deg at
  4:3.
- `near = 50.0` (0xA0BB1C); `far = 0x4855C0()` (not decoded) [L].
- Initial pitch `RotateX(pi/4)` (0x9E21B4), then position offset (0, -600, 0) (0x4AB8B0); pitch then follows the
  PitchChange key bindings. The debug string `Camera Pitch: %4.2f` exists with no code reference found. How pitch
  follows zoom: [L].
- Zoom: level = `(20/3) * ln(Z / 50)`, limits 11 .. 20.5, so Z ranges from 50*e^(1.65) to 50*e^(3.075) [arithmetic
  from the formula; not measured].

A remote renderer should copy the matrix (or the final view), not rebuild it from angles.

## 4. Per-frame view globals [C]

`GameViewState::UpdateViews` 0x51DF70 (mode 1), disassembly and Ghidra agree:

```
cam.FCamera->GetViewProj(&g_View[0], &g_Proj)   // via 0x73BC00 -> vtbl[2]
cam.FCamera.GetFrustumPlanes(&g_Planes[0])      // 0x6433F0 -> 0x8E5030
g_NumViews = 1
if (g_MapWrapsX == 1)                           // *(int*)0xD90FBC
   for each side whose frustum plane crosses the seam:
      g_View[n] = Translation(+-g_WorldWidth, 0, 0) * g_View[0]; planes; g_NumViews++
for i < g_NumViews: g_InvView[i] = inverse(g_View[i]); g_ViewOffset[i] = g_View[i].row3 - g_View[0].row3
... 0x51B450 (top-down ortho at 0xDA1690 / 0xDA16D0[i], probably the overlay or fog projection [I])
```

| Global | Type | Meaning |
|---|---|---|
| 0xDA12D0 | D3DXMATRIX | projection (PerspectiveFovRH) |
| 0xDA1310 + 0x40*i | D3DXMATRIX[4] | view i (0 = real camera, others = map wrapped by +-W) |
| 0xDA1410 + 0x40*i | D3DXMATRIX[4] | inverse views |
| 0xDA1510 + 0x60*i | D3DXPLANE[4][6] | world-space frustum planes |
| 0xDA1AD0 + 0xC*i | float3[4] | view i translation minus view 0 translation |
| 0xD90FA4 | uint | number of views this frame (1..3 in 3D) |
| 0xD90FBC | int | 1 = map wraps in X |
| 0xD90FCC/D0/D4 | float | world AABB min x, y, z (z = 0) |
| 0xD90FD8/DC/E0 | float | world AABB max x, y, z (z = 255) |
| 0xD90FE4 | float | wrap period = world width (`64*W - 1`, or `-0x41` for one wrap type) |
| 0xD90FE8 | float | world height `(64*H - 1) * 0.866` |
| 0xD90FF0 | D3DXMATRIX | world (normalized) -> hex (cube) |
| 0xD91030 | D3DXMATRIX | hex -> world (xy) |
| 0xD90FC4 | ptr | -> GridHeightmap 0xD91270; 0 before a map loads |
| 0xD91318/1C, 0xD91324/28, 0xD91320 | float | grid +0xA8/+0xAC scale, +0xB4/+0xB8 offset, +0xB0 ground-height base ([L] values) |
| 0x2DD1FB8 | D3DVIEWPORT9 | {0, 0, W, H, 0.0, 1.0}; W/H from `[[0x2DD1D1C]+0x644/+0x648]` |
| 0x2DD1FD0 | D3DXMATRIX | identity "world" for projection |
| 0x301057C / 0x3010584 | ptr | `ID3D11Device*` / immediate `ID3D11DeviceContext*` (out-params of D3D11CreateDevice at 0x8BB6B1) |

Caveats [C that writers exist, I which scenes]: other scenes write the same globals with their own cameras
(0x4813D0, 0x510140, 0x6E4870, 0x73E520: scene objects with the same GetViewProj -> g_View[0]/g_Proj, g_NumViews = 1,
wrap-copy code as UpdateViews), so the block is "whatever is being rendered now", not "the map camera".
*Corrected 2026-11-11 (issue 3007, dumpbin of the EXE file) [C]: 0x6A9670 does **not** write the globals (no reference
to 0xDA12D0 / 0xDA1310 / 0xD90FA4 in its body): called with `this` = the leader scene object 0xDA2780 (from 0x507F23,
0x619C85, 0x6A373F, each right after the leader-camera setter 0x74EF80), it fills a caller-provided view snapshot (the
type built by ctor 0x43BFD0) from the leader scene's own FCamera (vtable call, frustum 0x8E5030, inverse 0x8A86B6). Callers
of the other writers: 0x4813D0 from 0x4A3166 (the 0x4495C0 object, FCamera subclass vtbl 0xA77748), 0x510140 from
0x4A4D2C, 0x6E4870 from 0x78F864, 0x73E520 from 0x78017E; which screens they are is [L]. (Was: 0x6A9670 listed as a
writer.)* Every writer, and UpdateViews itself at its end, calls 0x617F30, which copies the whole view block (g_Proj,
g_View[4], inverse views, planes, 0xDA1690.., 0xDA1B00.., g_NumViews) into a stack snapshot and passes it to a function
pointer from the table at `0x1A0E518 + 0x600 * [0x1A0E400]` [C]: a per-frame command buffer, so the render side
probably draws from a queued copy while the main thread already builds the next frame's views [I]. Consequence for a
reader at Present: the globals may be one frame ahead of the presented image [I, live check in section 10].
*Measured 2026-11-13 (issue 9039, huge save, D33, about 450,000 frames in two runs) [L]: every map frame runs UpdateViews exactly once,
on the main thread, before that frame's command lists execute and before its Present; no frame had UpdateViews after
its scene clear. The view block a Present reads is the presented frame's (research/05-graphics-composition/
lockstep-probe.md).* Read it right after UpdateViews, or check
`GVS+0x758 == 1` and compare `g_View[0]` with `inverse(FCamera.camWorld)`. Other cameras: cube-map capture
0x42D790; leader scene with two FCameras at 0xDA2780 + 0x6810 / + 0x68E8 (set by 0x74EF80); an FCamera subclass
(vtbl 0xA77748) at +0x15608 of the 0x4495C0 object (picking 0x4A3130).

Main-thread order inside GVS::Update 0x7253E0 [C call order, I thread]: `0x72591F UpdateWorldCamera` (controller,
then views), `0x72595C 0x6F3ED0` (re-applies FOV/aspect), then render submission. GVS::Update runs on the EXE's
main (UI/Lua) thread; the DLL's game core runs on its own thread.

Strategic View (mode 2): views come from the object at 0x1A0ADA0 (calls 0x46B150, 0x57C0B0); WorldAnchor uses
`0x1A0ADA0->0x4F9E00` instead of D3DXVec3Project; projections are ortho (`D3DXMatrixOrthoOffCenterRH`). Not decoded [L].
*Added 2026-11-11 (issue 3007) [C, dumpbin of UpdateViews 0x51DF70]: when `[GVS+0x758] != 1` UpdateViews jumps to
0x51E2D5; for mode 2 it sets `g_NumViews = 0x1A0ADA0->0x46B150(...)` (the number of strategic views, not wrap copies)
and per view writes 0xDA1AD0/0xDA1AD4 + 0xC*i, 0xDA1B00.., 0xDA1B30.., 0xDA1B60.., 0xDA1B90.., 0xDA1BC0.., 0xDA1BE0..,
0xDA1C00.. (rectangles and offsets from 0x57C0B0); it does **not** write g_View (0xDA1310) or g_Proj (0xDA12D0) in
that path, so in Strategic View those keep the last 3D frame's values (stale) while g_NumViews means something else.
A reader must test GVS+0x758 == 1 before using either. Other modes (not 1, not 2) skip both paths.*

## 5. Conversions and picking

- **Grid <-> hex** [C]: `ToHexFromGrid` (Lua, C function 0x5ED240): `hx = gx - (gy >> 1)`, `hy = gy`,
  `hz = -(hx + hy)`, then an integer affine transform (0x56D480, built by 0x5A87D0 from globals at 0x2DD1CD8;
  presumably identity [L]). Back: `gx = hx + (hy >> 1)`.
- **Hex -> world** [C structure, L constants]: `HexToWorld` (Lua C function 0x50F370 -> 0x6851C0, `this` =
  `*(void**)0xD90FC4`): `w = D3DXVec4Transform(hex, M_h2w@0xD91030); w.x = w.x*sx - ox; w.y = w.y*sy - oy;
  w.z = height(int((w.x+ox)/sx), int((w.y+oy)/sy))` (0x6D8B60), `w.w = 1`.
- **World -> hex**: `n = ((w.x+ox)/sx, (w.y+oy)/sy, 0, 1); h = Vec4Transform(n, M_w2h@0xD90FF0)`, cube rounding 0x5DA590.
- MapSetup 0x535BF0 builds M_w2h as `Scale(1, 0.866, 1) * RotZ(30 deg) * Scale(1/(64*0.866), 1/64, 1) * Shear(y -=
  0.5x) * Translate(hex origin, wrap-type dependent) * CubeExpand` (0x43F850: z = -x - y). Hexes are **64 world
  units apart in x**, odd rows shifted +32, rows ~55.4 units apart with sy = 0.866 [C matrices, I sy]. Do not
  hard-code: read M_w2h, M_h2w, sx, sy, ox, oy (0xD91318..0xD91328) once per map load (0x535BF0 ends by setting
  `*(0xD90FC4) = 0xD91270`). `coupling.md` gives the GridHeightmap values: sx 1.0 (`[0xB9A6D4]`), sy 0.866
  (`[0xB9A6D8]`), height scale 0.2 (`[0xB9A6DC]`), ox = W*64/2, oy = H*64*0.866/2.
- **World -> screen** [C]: world anchors 0x522910 (`WorldAnchorControl::SetWorldPosition` path):
  `D3DXVec3Project(out, world, &g_Viewport@0x2DD1FB8, &g_Proj@0xDA12D0, &g_View[i], &I@0x2DD1FD0)`, looping over
  all g_NumViews copies and keeping the one on screen (bounds `[[0x2DD1D1C]+0x644/0x648]`). Backbuffer pixels.
- **Screen -> world, mouse picking** `GVS::UpdateMouseOverHex` 0x6668F0 [C, Ghidra]: `cam.GetViewProj(&V, &P)`;
  unproject NDC at z=0 and z=1; `D3DXPlaneIntersectLine` with the ground plane `(0, 0, 1, [0xD91320] * -60.0)`, i.e.
  **z = +12** (= 0.2 * 60; *corrected 2026-11-11, issue 3008 [L]: the plane's d is -12, so the plane is z = +12, not
  z = -12; posted mouse moves at three zooms matched UI.GetMouseOverHex 18 of 18 with z = +12, 4 of 6 with z = -12;
  was "ground plane at height 0.2 * -60"*); world -> hex -> round -> grid; results to GVS+0x790 (world point),
  GVS+0x768 (hex) and `InterfaceBuddy+0x90` (plot). **Flat plane, not the terrain mesh.** It returns at once while
  the grid's ready byte `[0xD91279]` is 0. Only Strategic View picking calls HexToWorld for a height.
- **Live evidence [L, GATES.md]**: hex picking by posted `WM_MOUSEMOVE` worked in both the all-on baseline and B4
  (UI.GetMouseOverHex followed the cursor: 85,22 / 83,23 / 86,21; identical hexes in B4). Picking therefore survives
  the switchable half of the world being off.

## 6. Reading the camera from the DLL each frame (proposal; built 2026-11-11 as option B, see section 10)

Locating, no new code bytes. Each site is found by a byte pattern the product checks; the patterns themselves are
in `shell/common/exe_camera.h` (section 10). (Byte patterns replaced by these descriptions, 2026-11-18, D83 audit.)
1. `&g_Civ5App`: PR #13379's verified `LoadCvGameCoreDLL_SetEngineUserInterface` (DX11 0x47BA70) loads the global's
   address as the immediate operand of its first instruction (at site+2, 0xCC8E74); the next instruction reads its
   member +0xD0, which is the cross-check.
2. Globals block: at 0x51DFBD UpdateViews pushes two addresses and calls GetViewProj: the first push's immediate
   (+1) is &g_Proj, the second's (+6) &g_View. The function head at 0x51DF70 reads the view mode from the GVS at
   offset 0x758 (the offset sits at head+0x14). The viewport and world globals come from the WorldAnchor site
   0x522B70, which pushes world, proj and viewport for `D3DXVec3Project`. Each pattern matches exactly once in all
   three EXEs.
3. `FCamera::GetViewProj` 0x8E4A80: takes camWorld from FCamera+0x30 and calls the D3DXMatrixInverse thunk 0x8A86B6.
   Check that `*(void**)(GVS+0x48)` is a vtable whose slot 2 is this function.

Options:
- **A (recommended): heap vptr swap on the map camera.** Copy the 3-entry FCamera vtable 0xA9A1B0 into DLL memory,
  replace slot 2 with a thunk (`__thiscall`, 2 stack args, `ret 8`) that calls 0x8E4A80 then publishes view/proj,
  camWorld (GVS+0x78), fov, aspect, near, far, GVS+0x758, g_Viewport and the wrap data (0xD90FE4, 0xD90FBC) with a
  sequence counter in shared memory. Heap write only (respects the PR #13379 no-`.text`/`.rdata` rule). Fires on the
  main thread inside UpdateViews once per 3D frame, also from picking 0x6668F0 and 0x7117F0 (deduplicate); not in
  Strategic View. Re-install on each new `g_Civ5App->+0xC0` value; the FCamera dtor resets the vptr itself.
- **B: hook-free polling** from any thread of 0xD90FA4, 0xDA12D0, 0xDA1310.., GVS+0x758, GVS+0x48..; matrices are
  written with `rep movsd` and D3DX calls on the main thread, so read twice or rebuild view/proj from camWorld +
  fov/aspect/near/far and check `inverse(camWorld) == g_View[0]`. One frame of lag.
- **C: D3D11 interception** to validate what the GPU gets: context = `*(void**)0x3010584`; swap the context
  instance's vptr (heap) and wrap Map (14) / Unmap (15) / UpdateSubresource (48) / VSSetConstantBuffers (7) /
  PSSetConstantBuffers (16). The engine stages constants in CPU blocks (0x8CFD50 returns `block+4` and sets a dirty
  byte; 0x643F9B writes `view[i] * proj`); find the view-projection CB by content (also transposed), cross-check with
  RenderDoc. Shader bytecode in `.shader` is stripped (no RDEF names).

Implication (GATES.md): camera ownership is not a blocker for the shell. The EXE keeps owning the camera (input,
zoom, `UI.LookAt`) and the DLL forwards the matrices to the server each frame.

## 7. DX9 and Tablet builds (signatures matched exactly once per EXE) [C signatures, I struct offsets]

| Build | g_Civ5App | GetViewProj | UpdateViews | g_Proj | g_View[0] | g_NumViews | viewport | world | WorldAnchor site |
|---|---|---|---|---|---|---|---|---|---|
| DX11 `CivilizationV_DX11.exe` | 0xCC8E74 | 0x8E4A80 | 0x51DF70 | 0xDA12D0 | 0xDA1310 | 0xD90FA4 | 0x2DD1FB8 | 0x2DD1FD0 | 0x522B70 |
| DX9 `CivilizationV.exe` | 0xCB8CF4 | 0x96BA80 | 0x453BE0 | 0xD91120 | 0xD91160 | 0xD80DF4 | 0x2DC1DB8 | 0x2DC1DD0 | 0x57B630 |
| Tablet `CivilizationV_Tablet.exe` | 0xCCA8F0 | 0x8E6F70 | 0x73EBA0 | 0xDA2D50 | 0xDA2D90 | 0xD92A24 | 0x2DD3FA0 | 0x2DD3FB8 | 0x558A60 |

The g_Civ5App sites match PR #13379's columns (0x47BA70 / 0x72B7F0 / 0x4CB690). Struct offsets (GVS+0x44/+0x48/+0x758,
FCamera fields) are assumed identical across builds because the signatures that encode them matched byte for byte.

## 8. Static-analysis-only vs live

- **Static only**: every address, layout and formula above; the vptr-swap proposal; matrix globals; the DX9/Tablet
  table.
- **Live**: only hex picking by posted mouse moves (baseline and B4) and `UI.LookAt` on a selected unit (Gate 1
  checklist). Gate 3's server drew a test scene, not the game's camera.
- **Open [L]**: the 0x2DD1CD8 hex-orientation transform (runtime grid scale/offset and `far`: read live
  2026-11-11, section 9); whether UI 3D scenes (leader screen, unit preview, city view) overwrite g_View/g_Proj mid-frame
  (statically: the leader scene's 0x6A9670 does not, four other scene writers do, section 4; which screens they are is
  open); which thread runs GVS::Update and whether the DLL outlives the GVS on exit to menu; Strategic View's own
  projection (0x1A0ADA0, 0x4F9E00; that UpdateViews leaves g_View/g_Proj stale in mode 2 is [C] since 2026-11-11);
  how pitch follows zoom (0x669B60 / 0x7117F0); the CB upload path (0x8CFD50 blocks; 0x643B60, 0x4FB320, 0x5184xx);
  whether terrain/water use g_ViewOffset (0xDA1AD0) and whether g_Proj carries jitter or stereo offset.

Method notes (CAMERA.md): `.text` 0x401000-0x9D5D30; the D3DX9 math thunks the camera code calls (look-at and
perspective matrices, inverse, multiply, project and unproject, plane-line intersection, vector transform) lie at
0x8A86A4..0x8A8734; the inverse is 0x8A86B6 (item 3 above) (thunk list replaced by this line, 2026-11-18, D83 audit);
Lua C functions found from `pushcclosure`/`setfield` pairs
(0x92396A / 0x923928): `UI.LookAt` 0x42F310, `UI.CenterCamera` 0x4333D0, `UI.IsCameraMoving` 0x5D6940,
`UI.GetMouseOverHex` 0x6F5940, `UI.GetStraightZoom` 0x6A6890, `HexToWorld` 0x50F370, `ToHexFromGrid` 0x5ED240;
`Events.CameraViewChanged` / `CameraProjectionChanged` accessors 0x408220 / 0x614790. No Lua getter exists for camera
position, zoom or matrices. An old commented-out NOP patch at 0x51E031 in `CvDllGame.cpp` (removed by PR #13379) sat
inside UpdateViews' wrap-copy test.

## 9. Runtime values (read live 2026-11-11, epic 3000, issue 3004) [L]

Read by `shell/tools/gt_reader.py` (ReadProcessMemory by PID, rebased on the EXE's module base, TimeDateStamp
0x546CD0A8 checked) on Goldberg instance c5h-b, DLL `i2-shell-test` b92373aeb, all world systems on (instance
config), windowed 2554 x 1411. Huge save `C5H-VP8P-HUGE_0350` t350 (128 x 80, wraps) and Tiny `C5H-3000-TINY-SETUP`
t18 (56 x 36, wraps). Dumps, screenshots and a README: `D:/c5-64bit-work/i2-ground-truth/` (not committed).

- **Module base 0x00740000** in all six runs of 2026-11-11 (0x00F60000 on 2026-11-10): rebase every address.
- **Grid constants** (identical matrices on both maps): `M_h2w` rows (63.9986, 0.00094, 0, 0), (32.0, 64.0019, 0, 0),
  (0, 0, 0, 0), (32.0007, 128.0033, 0, 1); `M_w2h` rows (0.015625, -2.3e-7, -0.015625, 0), (-0.0078125, 0.0156247,
  -0.0078122, 0), (0,0,0,0), (0.5, -2, 1.5, 1). sx 1.0, sy 0.866, height scale 0.2 ([0xD91320]), ox = 64 W / 2 (4096 /
  1792), oy 2327.808 / 1108.48 = 0.866 * 64 * (H + 4) / 2 [I from the two values], wrap flag 1, wrap period 64 W - 1
  (8191 / 3583), world AABB x +-(64 W - 1)/2, y +-2327.375 / +-1108.047, z 0..255; world height 4654.75 / 2216.094.
- **GridHeightmap 0xD91270 layout** [C from HeightAtGrid 0x6D8B60 and the tile lookup 0x5BCE00; L values]: +0x09 ready
  = 1; +0x0C / +0x10 samples X / Y = 64 W / **64 (H + 4)** (8192 x 5376, 3584 x 2560: four plot rows of margin);
  +0x14 tiles per row (X/16), +0x18 tile rows (Y/16), +0x1C directory (8-byte entries: word +0 != 0 -> constant tile
  with value word +4, else dword +4 = index into the pool), +0x24 tile count, +0x28 pool (256 ushort per 16 x 16 tile,
  row-major). Every tile was a pool tile on both maps (0 constant tiles). +0x9C capacity 243,200; +0xA0..+0xB8 =
  X, Y, sx, sy, 0.2, ox, oy; +0xC0 / +0xC4 = X, Y * 0.866. *Corrects `stubs/coupling.md` 2.1 ("+0x10 / +0x1C
  directory and tile pool"): +0x10 is samples Y; the directory is +0x1C and the pool +0x28.*
- **Height**: `z = u16 * [0x9F9E34] * [grid+0xB0]` with [0x9F9E34] = 1/255 (0.0039215689) and 0.2, i.e. **z = v/255 *
  0.2** (coupling.md confirmed). Sample index = `(int(w.x), int(w.y))` of `(hx, hy, 0, 1) * M_h2w`; x wraps mod X.
- **HexToWorld** (Lua `HexToWorld(ToHexFromGrid(Vector2(x, y)))`) equals `(hx, hy, 0, 1) * M_h2w`, then `x*sx - ox`,
  `y*sy - oy`, z from the heightmap, to within 0.0005 world units in x/y and exactly in z for nine plots on two maps
  (`gt_validate.py`). Lua `GridToWorld(x, y)` raised in the channel's InGame context and was not compared.
- **Camera** at the six captured views: `num_views` 1; projection = PerspectiveFovRH with fovY **29.01 deg** (aspect
  1.81006; proj[1][1] = 3.8647 = cot(14.51 deg)); section 3's formula with K = 80 would give 38.7 deg at this aspect,
  so K or the formula differs at run time [L value; why not resolved]; **near 50, far 4000**; pitch 45 deg
  at zoom level 14.256 (load default) and at 11.0 (four wheel ticks in = the minimum), camera height 424.26 and 261.0;
  view mode +0x758 = 1. `g_View[0]` = inverse(GVS camWorld) (row-vector convention). The globals did not change
  across a screenshot (read before and after, byte-identical, six of six).
- **World -> screen**: projecting world points with the dumped g_View[0], g_Proj and viewport {0, 0, 2554, 1411, 0, 1}
  (D3DXVec3Project, identity world) lands on the EXE's own hex highlights (`Events.SerialEventHexHighlight`) with a
  **median error of 0.5 px** on both maps (outline drawn at 0.90-0.925 of the hex corner radius, fitted; 59-64 % of
  edge samples within 3 px, the rest hidden by models and UI).
- Hex lattice: plot centres 64 units apart in x, odd rows +32, rows 55.43 apart (64 * 0.866); corners = mean of the
  centre and two neighbouring centres.

## 10. The camera read built in `shell_composite.dll` (issue 3007, 2026-11-11) [C static, L stand-in; L in the game 2026-11-11, issue 3008: "Live answers" at the end]

Code: `shell/common/exe_camera.h` (addresses, patterns, checks), `shell/composite/shell_composite.cpp` (section "EXE
camera"), the section layout and the server's grid scene in `research/05-graphics-composition/shared-texture-and-compositor.md`
section 7.

**Choice: option B (read at every Present, twice, with consistency checks), not option A (heap vptr swap).** Why:
- A's thunk sees only GetViewProj's out-parameters (view 0 and the projection). The wrap copies and g_NumViews are
  written by UpdateViews after GetViewProj returns, so the globals have to be read anyway; A adds a write for nothing
  B lacks.
- Slot 2 is also called from picking (0x6668F0, 0x7117F0) into locals: A must tell those calls apart.
- A writes into an EXE heap object whose lifetime the DLL does not own (re-created with the GVS); B writes nothing in
  the EXE at all, not even on the heap.
- A gains no timing: UpdateViews and every other writer hand a copy of the view block to the render side through
  0x617F30 (section 4), so any reader sees the main thread's newest camera, not necessarily the presented one.
- B's failure modes are each detected and published as a status: a torn read (two reads differ, retried up to 4
  times: TORN), another scene's camera in the globals or globals not updated (g_View[0] != inverse(camWorld):
  INCONSISTENT), wrap copies that are not `Translation(+-period, 0, 0) * view0` (INCONSISTENT; that order and the
  period 0xD90FE4 are [C] from 0x51E052..0x51E093), Strategic View (GVS+0x758 != 1: NOT_MAP), no GVS (NO_GVS), a GVS
  whose +0x48 is not the FCamera vtable (BAD_GVS); the server re-checks every block that says OK. *Corrected 2026-11-11
  (verifier 9031, stand-in [C]): a NaN in g_View[0] or a wrap view is NOT detected: the consistency helpers skip NaN, so
  such a camera is published OK and the server accepts it (defect V1, shell-foundations-verification.md section 3).*

**Validation, once, before the hooks are installed** [C, every pattern checked against `CivilizationV_DX11.exe` on
disk by `shell/tests/test_camera.py::test_signatures_match_exe_on_disk`, 2026-11-11]: base = `GetModuleHandle(NULL)`
(the test host passes a fake image's base in `VPSHELL_EXE_BASE`); MZ/PE header; TimeDateStamp 0x546CD0A8; 11 byte
patterns at fixed VAs with their relocated immediates compared against `VA - 0x400000 + base` (the patterns are in
`shell/common/exe_camera.h`; the quoted bytes and instructions were replaced by what each site shows, 2026-11-18, D83
audit): 0x47BA70 the load of g_Civ5App (0xCC8E74) and its +0xD0 member; 0x51DF70 UpdateViews' head, which reads the
view mode at GVS+0x758 and takes the map path when it is 1; 0x51DFA0 the camera controller at GVS+0x44; 0x51DFBD the
call to 0x73BC00 with g_Proj and g_View pushed and the controller in `ecx`; 0x51DFFE the store of 1 to g_NumViews and
the test of g_MapWrapsX; 0x51E052 the load of the wrap period 0xD90FE4; 0x522B70 WorldAnchor, which pushes world, proj
and viewport; 0x8E4A80 GetViewProj (camWorld at FCamera+0x30); 0xA9A1B0 the FCamera vtable (three relocated entries);
0x6851C0 HexToWorld (M_h2w pushed, sx at +0xA8, ox at +0xB4); 0x50F370 the load of the GridHeightmap pointer
0xD90FC4. Any mismatch: `camera: REFUSED: ...` naming the pattern and byte, status REFUSED, nothing read.
`call 0x73BC00` is made with `ecx` = GVS+0x44 (the controller), which forwards to FCamera vtbl[2] (correction of the
section 4 text "cam.FCamera->GetViewProj": the call goes through the controller) [C].

**Per Present** (render thread, inside the Present hook, under SEH; 5 exceptions disable it for good, each one pauses
it for 1 s): `[g_Civ5App] -> +0xC0` GVS; g_NumViews, g_Proj, g_View[0..3], the viewport, the wrap period; from the GVS
the vptr at +0x48, fov +0x50, aspect +0x54, camWorld +0x78, near/far +0x118/+0x11C, view mode +0x758. Read twice and
compared byte for byte. Checks: `camWorld * view0 = I` within 2e-3 (3x3 and w column) and 0.5 world units
(translation row); every wrap view within 1 unit of `Translation(+-period) * view0`; g_Proj against
PerspectiveFovRH(GVS fov, aspect, near, far) is information only (flag). The grid constants (0xD90FBC..0xD91328,
GridHeightmap +0x09/+0x0C/+0x10) are re-read when a key (pointer 0xD90FC4, ready byte, sample counts, ox, oy) changes
and every 600 Presents; `gridSeq` increments when anything changed.

**How the reader knows it is not looking at the map camera** (the open question of section 4): GVS+0x758 != 1 ->
NOT_MAP (Strategic View: UpdateViews leaves g_View/g_Proj stale, section 4); another scene that writes the globals
(0x4813D0, 0x510140, 0x6E4870, 0x73E520) with its own camera leaves `g_View[0] != inverse(GVS camWorld)` ->
INCONSISTENT; the leader screen's 0x6A9670 does not write the globals at all [C], so whether the map camera stays
valid behind a leader screen is for the live run. The server hides the grid for these statuses (INCONSISTENT / TORN
only after 3 consecutive frames; a single inconsistent frame, e.g. `UI.LookAt` between UpdateViews and Present [I],
keeps the last good camera).

**Offline accuracy against the EXE's hex outlines** (`shell/tests/test_camera_offline.py`, 2026-11-11, ground truth of
section 9: camera_validate.json of the huge save and the Tiny game, 2554 x 1411): the server renders its grid with
the dumped camera and constants (its own vertex generation and vertex shader, D3D11 rasterisation, `--dump`), and
every grid pixel of an outlined plot is compared with the EXE's magenta highlight of that plot
(Events.SerialEventHexHighlight; drawn at about 0.9 of the corner radius, so the grid is rendered at
`--grid-scale 0.9`):

| Game, variant | grid px on outlined plots | vs EXE outline: median / within 1 px / within 3 px / p75 | vs Python reference: median / p95 / max |
|---|---|---|---|
| huge, heightmap, scale 0.9 | 6151 | **0.0 px / 57.5 % / 60.6 %** / 19.7 px | 0.28 / 0.84 / 3.6 px |
| huge, flat z 12, scale 0.9 | 6148 | 0.0 px / 57.6 % / 60.7 % / 19.8 px | 0.27 / 0.46 / 0.55 px |
| huge, heightmap, scale 1.0 | 3396 | 15.5 px / 0 % / 0 % | 0.28 / 0.56 / 5.5 px |
| Tiny, heightmap, scale 0.9 | 4881 | **0.0 px / 63.8 % / 65.3 %** / 22.8 px | 0.30 / 1.13 / 42.6 px (one stray pixel; p99 3.1) |
| Tiny, flat z 12, scale 0.9 | 4891 | 0.0 px / 58.0 % / 62.0 % / 23.2 px | 0.27 / 0.46 / 0.54 px |
| Tiny, heightmap, scale 1.0 | 2685 | 15.8 px / 0.2 % / 0.5 % | 0.32 / 1.14 / 4.2 px |

Reading it: where the EXE's outline is visible, the grid lies on it (the median 0 px means the 1-px grid line falls
inside the EXE's thick magenta line; 57-64 % of the grid pixels are within 1 px); the other ~35-40 % sit where models,
tooltips or the screen edge hide the magenta, the same share as `gt_validate.py`'s projected samples (59 / 64 % within
3 px, 2026-11-11), so it is occlusion, not error. Against the reference projection the server is exact to the pixel
grid with flat z (max 0.55 px = rasterisation); with the heightmap its 4 straight segments per edge leave up to
1-4 px where the terrain bends inside an edge. At scale 1.0 (true hex edges) the distance to the EXE's outline is the
outline's inset, ~15.5 px at this zoom. Flat z costs nothing measurable here (the outlined plots are flat land at
z 12); on hills it would. Evidence: `shell/evidence/3000/camera/offline_*.json` and the pytest log there; the images
in `D:/c5-64bit-work/3000/w3-suite/` (not committed).

**Live checklist (for the worker that drives c5h-b; expected log lines)**
1. Composite log at load: `camera: EXE image at 0x00XX0000: TimeDateStamp 0x546CD0A8 and 11 byte patterns match -
   camera read enabled (delta 0x...)`. A `camera: REFUSED: ...` line: stop, record it.
2. Front end, before a game: record the status line (expected `camera: status - -> NO_GVS` or BAD_GVS, and
   `camera: grid constants read (grid seq 1): valid 0 ...`); no `EXCEPTION`.
3. After the huge save loads: `camera: grid constants read (grid seq N): valid 1 ptr 0x... ready 1 samples 8192x5376
   (map 128x80 plots) wrap 1 period 8191.0 sx 1.0000 sy 0.8660 hscale 0.200 ox 4096.000 oy 2327.808`, then
   `camera: status ... -> OK` and `camera: first OK read on thread T: fov 29.0x deg aspect 1.81... near 50.0 far 4000.0
   ... viewport 2554x1411`. Record T against the thread id of the stats lines (the Present thread) and whether it is
   the EXE's main thread.
4. Stats lines: `cam=OK`, `cam_ok` = `frames`, `cam_bad=0`; record `cam_retry` (non-zero = another thread writes the
   globals during Present) and `cam_us_mean / cam_us_p99` (cost of the read per Present, not yet measured live).
5. Server (`shell_server.exe --scene grid --grid-scale 0.9 --heightmap <gt_reader heights dump of the same save>`,
   ini `blend=alpha`): `grid: built for grid seq N: map 128 x 80 plots ... z from the heightmap`, `grid: state ... ->
   OK`, `grid 1s: state=OK ... srv_incons=0 lag_max<=1`.
6. Accuracy: at zoom 14.26 (load default), 11.0 (four wheel ticks in) and 20.5 (out), and during a slow pan, highlight
   five plots with `Events.SerialEventHexHighlight` and compare the grid with the magenta outline as
   `test_camera_offline.py` does (median expected <= 1.5 px still; during a pan expect up to one frame of lag, measure
   it).
7. Pan across the seam: stats `cam_views=2`, `cam=OK` (not INCONSISTENT: proves the wrap-copy check on live data),
   cyan grid beyond the seam on the hexes.
8. Strategic View on: `camera: status OK -> NOT_MAP (present ..., view mode 2, views N ...)`, server `-> HIDDEN`;
   off: `NOT_MAP -> OK`.
9. Leader screen opened and closed: record every `camera: status` line (expected [I]: OK throughout, since 0x6A9670
   does not write the globals; INCONSISTENT if one of the four scene writers runs there).
10. Kill switch: `camera=0` -> `camera: status OK -> OFF`, server `-> HIDDEN`; `camera=1` -> `OFF -> OK`.
11. Exit to the main menu and load the Tiny game: `NO_GVS` (or similar) in between, then `grid constants read (grid
    seq N+1) ... samples 3584x2560 (map 56x36 plots)`, `OK`; no EXCEPTION line in the whole run.
12. Clicks: with posted `WM_MOUSEMOVE`, `UI.GetMouseOverHex` against the grid hex under the cursor (the published
    camera, unprojected onto the plane z = +12, section 5; corrected 2026-11-11).

**Live answers (issue 3008, 2026-11-11 03:58-04:39 EST, c5h-b, D33, huge save t350, DLL i2-shell-test b92373aeb,
`shell_composite.dll` of this branch; evidence `shell/evidence/3000/live/3008_*`) [L]:**
1. `camera: EXE image at 0x00FE0000` (run 1) / `0x00B40000` (runs 2, 3) `: TimeDateStamp 0x546CD0A8 and 11 byte patterns match - camera read
   enabled` in all three launches; no REFUSED.
2. Front end and load: `INCONSISTENT` at the first Present (views 0, no map), grid seq 1 valid 0; the grid constants
   arrive in two steps (seq 2: pointer set, ready 0; seq 3: `valid 1 ... samples 8192x5376 (map 128x80 plots) wrap 1
   period 8191.0 sx 1.0000 sy 0.8660 hscale 0.200 ox 4096.000 oy 2327.808`, as expected), then `INCONSISTENT -> OK`
   about 28 s later when the map camera starts. No NO_GVS seen (the GVS exists from the front end on).
3. `first OK read on thread T`: T is the EXE's **main thread** (the thread that installed the focus guard and created the
   window; 58700 / 19080 in runs 1/2) and every Present is on it. The globals are written by `UpdateViews` on the same
   thread earlier in the same frame (the hang stack shows UpdateViews -> frame loop on that thread), and `cam_retry` was 0
   in all 1,202 stats seconds: **no other thread writes the globals during Present; the published camera is the
   presented frame's camera.**
4. `cam_ok == frames`, `cam_bad = 0` in normal play; `cam_us_mean` 2.3 us per Present (run 3, 207 s).
5. Server with `--heightmap`: `grid: built for grid seq 3: map 128 x 80 plots, 491520 line vertices ... z from the
   heightmap`, `state -> OK`, `lag_max` 0-1 in steady views.
6. Accuracy: **against the EXE's own hex outlines at an identical camera** (the 3004 stock highlight shots of
   `D:/c5-64bit-work/i2-ground-truth/huge`, view matrix within 0.042 units of the live one, zoom 11): grid pixels on the
   outlined hexes **median 0.0 px, 84.3 % within 1 px, 85.0 % within 3 px** (the rest occluded in the stock shot)
   (`3008_grid_vs_stock_outline_z11.json`). The EXE draws no hex highlight in D33, with or without Overlay (run 3:
   Overlay on, highlight shots identical to plain ones), so at zoom 17 and 20.5 the live grid was measured against the
   reference projection of the camera read at the shot: median 0.24 / 0.24 px, p95 0.49 / 0.46 px (zoom 11: 0.29 px);
   the picking check (12.) ties those cameras to the EXE.
   **Against the EXE's own outline at zoom 17 and 20.5 (W7, 2026-11-11 07:29-07:31, second launch) [L]:** the stock
   configuration (all systems on, so the EXE draws `SerialEventHexHighlight`), same DLL and save t350, the composite with
   `insert=present` so the server's grid (`--scene grid --grid-scale 0.9 --heightmap <3004 huge dump>`) is drawn over the
   frame; highlight shot (grid hidden by `camera=0`), plain shot and grid shot at ONE still camera (view matrix change
   over the three shots 0.0000), plots (100,30) (101,30) (100,31) (99,29) (102,31) at Memphis, cameras identical to W5's
   (zoom 17 position (2335.88, -1181.95, 639.71); zoom 20.5 via the clamp). `grid_accuracy.py` (grid pixel -> nearest
   visible magenta): zoom 17 **median 0.0 px**, 63.6 % within 1 px, 68.9 % within 3 px; zoom 20.5 **median 0.0 px**, 57.0 % /
   62.9 %: the rest are grid pixels where the EXE's outline is hidden by the city, banners and icons (`3008_w7_stock_
   outline_vs_grid_z17.jpg`, `_z20.5.jpg`). The occlusion-free direction (`outline_to_grid.py`: from the centre line of
   every VISIBLE magenta band pixel to the nearest grid pixel; the EXE's band is 6-10 px wide, the grid line 1 px): zoom 17
   median 1.0 px, 84.9 % within 1 px, 92.8 % within 2 px, p95 3 px; zoom 20.5 median 0.0 px, 93.1 % within 1 px, 97.4 %
   within 2 px, p95 2 px. Reference projection (as W5): 0.24 / 0.25 px median. **On land the grid sits on the EXE's own
   outline within about 1 px at all three zooms** (zoom 11: W5, median 0.0 px) [L].
   **Over water it does not (new, W7) [L]:** in a still shot over the ocean at zoom 17 (`3008_w7_pan_water_z17_static_
   crop.jpg`) the grid lies about 22 px below the EXE's highlight (correlation shift (-2.2, -21.6) px; on land the same
   method gives (0.2, 0.4) px). The 3004 heightmap holds **z = 0 under water** (12.02 on flat land), while the EXE draws the
   highlight over water at about z = 12 (the water surface, the same height as the picking plane, section 5): projecting
   z 0 instead of 12 moves a point 26 px down at that camera. The server's grid must clamp z to the water surface (12)
   on water plots [I fix; L cause by the numbers]. *Fix built 2026-11-11 09:1x (issue 3012): `shell_server.exe --water-z Z`
   (default 12; negative = off) raises heightmap z below Z to Z. Offline against the 3004 ground truth (6/6 tests pass):
   huge hm s0.9 unchanged (57.6 % of grid pixels within 1 px of the EXE outline, median 0 px); Tiny hm s0.9 60.7 % -> 59.7 %
   (median 0 px both), so some coastal outline vertices sit below 12 in the EXE's own drawing [I]; the water shot of W7 was
   not re-rendered offline, so the 22 px -> ~0 px claim is not yet verified [I].* *Measured live 2026-11-11 by verifier 9031
   (open ocean at (86,19), zoom 16.5, stock config, same camera both ways): without the clamp the grid lies **23 px below**
   the EXE's outline, with `--water-z 12` **5 px above** it (coast at Giza: +6 -> -4 px). So the fix is right in direction
   but overshoots: the EXE's water outline lies near z = 10, not 12 [I by interpolation];
   `research/05-graphics-composition/shell-foundations-verification.md` section 4.* *Corrected 2026-11-13 (one-off
   9035, issue 3012): the default is now `--water-z 10` (was 12). Basis: epic 3200's fit of the EXE's highlight band over
   open water in six live stock shots, best at z 10.0 (median 0.63 / 1.55 / 1.04 px against 4.51 / 8.06 / 8.60 px at 12;
   `terrain-surface.md`, "Water outline height"), and the field's water plane 0xD91274 = 10.117 [C dump]. The two live
   shifts of 9031 at the same open-ocean camera (+23 px at z 0, -5 px at z 12) put the zero crossing at z 9.86 on a
   straight line (-2.33 px per unit z at zoom 16.5), so z 10 leaves about -0.3 px there [I]. **Measured live 2026-11-13 (c5h-c,
   same plots, zoom 16.256): z 10 puts the grid on the EXE's water outline, best shift (0, 0), 100 % of the centre line
   within 1 px; z 12 at the same camera -5 px** [L] (`shell-foundations-verification.md` section 10). The picking plane stays z = +12 (section 5): the EXE's water outline
   (z ~10) and its water picking plane are not at the same height [I].*
   **Pan lag, measured (W7) [L]:** the EXE's own key panning (posted RIGHT / LEFT held, `tools/harness/input.key`,
   `shell/tools/stock_acc.py pan`) at zoom 17 runs at about **1,900 px/s** screen speed (camera read by
   ReadProcessMemory every ~4 ms: 20 ms bins 1,000-2,900 px/s, the camera steps unevenly per game frame). A
   back-buffer shot in the middle of the pan, both the EXE's highlight and the grid in one frame, correlated by
   `shell/tools/shift_match.py`: over water (static water offset subtracted) **18.3 px** behind at ~1,900 px/s (1,604
   px/s by the +-60 ms window) = **9.6-11.4 ms effective camera age**; over land **40.2 px** at ~1,870 px/s = **21.5 ms**.
   The server's texture age at the draw was `age_mean` 8.0-8.9 ms (game 110-206 fps, server 60 Hz). So the grid's
   camera is the texture age plus up to one server period (16.7 ms) plus up to one game frame old: **10-22 ms, i.e.
   18-40 px at a 1,900 px/s key pan at zoom 17** (two frames measured; the error is speed x that age, zero at rest:
   the frame after the pan stops is exact). A server at the game's frame rate, or the composite drawing with the newest
   camera block (reprojection), would cut this to the texture age or below [I]. (W5's estimate, 8.3-9.0 ms x speed,
   was the lower bound.)
   **The terrain scene's pacing (epic 3200, issue 3205, 2026-11-11; design [C], live figures: 3207 below, 2026-11-12):**
   `shell_server.exe --scene terrain` has no fixed rate: it polls the camera block every 0.5 ms and draws and publishes
   once per new block (frameStamp changed), so the up-to-one-server-period term above (16.7 ms at the grid scene's 60 Hz)
   becomes the poll interval plus the draw: stand-in, huge field, RTX 4080, 2554 x 1411, CPU in draw() 0.18 ms and GPU
   0.73-1.66 ms per draw (per-second means; one-off probe, not a PERFORMANCE.md series), 1,749 draws for 1,749 blocks.
   Expected effective camera age with it [I; first live trial below]: the texture age (the block of Present N is drawn
   about 1-2 ms after it, and the composite inserts it at the next frame's tagged clear) plus up to one game frame, i.e.
   no worse than the 10-22 ms above and near its low end; what would cut it further is reprojection in the shell
   (decision D58 2, next epic).
   **Live, issue 3206 (W5, 2026-11-11) [L, not a PERFORMANCE.md series]:** in D33 on the huge save the terrain scene drew
   529,208 times for 529,208 camera blocks in 30 minutes (one draw per block, 0 stale); the composite's texture age at
   the insertion was `age_mean` 1.57 ms, `lat_mean` 1.08 ms at ~410 fps (one stats second, 15:34). A trial of W7's
   method with the terrain scene's pacing (stock configuration, because the EXE draws no highlight in D33;
   `--scene terrain --overlay-grid 1` with a transparent terrain and the grid drawn with the terrain's camera,
   `insert=present blend=alpha`, zoom 17, `shell/evidence/3200/w5_pan_3207.py` around `stock_acc.py pan`, then
   `pan_offset.py`): six mid-pan frames at 1,470-1,990 px/s, offsets 8.2-24.4 px, **effective camera age 4.1-16.3 ms**
   (median 12.9 ms; static offset not subtracted; raw `D:/c5-64bit-work/3200/w5/pantrial/`). Inside the 10-22 ms band
   and at most equal to it; the ten-frame series with its statistics is 3207's (plan in its tracker issue).
   **The series, issue 3207 (W8, 2026-11-12 22:42-22:49, old pins i2-shell-test b92373aeb, save C5H-3200-HUGE_0350,
   stock configuration, quiet lock held) [L]:** 12 mid-pan frames (6 RIGHT, 6 LEFT, 1,500 ms holds, shot 0.35 s after
   the key) at 1,518-1,847 px/s: **effective camera age 2.6-13.1 ms, mean 6.1 ms (sd 3.05, 95 % CI [4.2, 8.0]), median
   5.7 ms**, raw (static offset not subtracted; with the 7.8 px rest offset subtracted: median 0.9 ms, max 7.9 ms). Every
   frame is below 3000's 10-22 ms band's top and the mean below its bottom: the per-block pacing removes the up-to-16.7 ms
   server-period term. Caveat: `zoomto` overshot to zoom 18.26, so the pans ran there, not at 17 (the age in ms does not
   depend on it [I]). LEFT pans lag more than RIGHT (8.3 vs 3.9 ms mean, cause open). *Reanalysed 2026-11-13 by
   verification 9038 (`research/05-graphics-composition/terrain-surface-verification.md` 7): the raw age is the length of
   an offset that is 6-9 px at rest; along the pan, with each hex's rest offset subtracted, the age is -2.4 ms (RIGHT) and
   +0.65 ms (LEFT), mean -0.87 ms [-2.35, +0.60], i.e. not resolved (under one game frame); the LEFT/RIGHT difference is
   the static offset vector. The 6.1 ms is an upper bound.* Full table and per-frame values:
   `research/05-graphics-composition/shared-texture-and-compositor.md` 15.4; raw `D:/c5-64bit-work/3200/w8/huge_pan/`.
7. Seam pan: `cam_views=2`, `cam=OK` throughout, no INCONSISTENT: the wrap-copy check holds on live data.
8. Strategic View: **not run; toggling it hangs the EXE in D33** (StrategicView system off; disable-systems-and-masks.md
   section 5, bug 3010). With StrategicView on, NOT_MAP would be expected [I].
9. Leader screen (Isabella, 12 s): `cam=OK`, views 1, mode 1 throughout; no status change: the leader scene leaves the
   map camera valid (confirms [C]).
10. Kill switch: `camera=0` -> `OK -> OFF`, server `-> HIDDEN`; `camera=1` -> `OFF -> OK`; `camera_test=stall` ->
    server STALE within 16 frames.
11. **Map change, W7 2026-11-11 07:21-07:25 [L]:** `Events.ExitToMainMenu()` on the main thread, the Tiny save loaded in
    the same process from the main menu (posted clicks on the game's menus: the Lua channel is silent in the front end;
    shared-texture-and-compositor.md 9.4). Lines: exit -> `grid seq 4: valid 0`; in the menu the block stays `OK`
    (the view globals keep the last map's camera; no NO_GVS, as W5 saw at launch); load -> seq 5 (pointer, period
    3583.0), **seq 6 `valid 1 ... samples 3584x2560 (map 56x36 plots) wrap 1 period 3583.0 ... ox 1792.000 oy 1108.480`**,
    `OK -> INCONSISTENT` for 4 frames, `-> OK`; **no EXCEPTION** in the run. The grid rebuilt for seq 6 and lies on the
    Tiny hexes: against the 3004 stock highlight shots at the identical camera (zoom 11, plot (8, 24), view 0 within
    0.0064) median 0.0 px, 99.7 % within 1 px (`shell/evidence/3000/live/3008_w7_item11_*`).
12. Clicks: 18 of 18 posted mouse moves (six positions at zoom 11, 17, 20.5) gave the same plot from
    `UI.GetMouseOverHex` as the published camera unprojected onto z = +12 (`3008_pick_z*.json`).

**Still not verified:** the four other scene writers' screens; DX9/Tablet EXEs; Strategic View (bug 3010). *(A map
change was verified 2026-11-11 by W7, item 11.)*
(Was: "Not verified: everything in the game ...; the thread that writes the globals relative to Present ...; the cost
of the read in the game; Strategic View and the leader screen live; the four other scene writers' screens.")

## 11. Wheel zoom: every wheel message is one fixed step (ticket 5042, FR4/FR5 of the third speedup list, 2026-11-24)

The question (the Architect's T12, `research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`):
on a trackpad or a smooth (high-resolution) wheel, Windows sends many `WM_MOUSEWHEEL` messages with deltas below one
notch (120); does the EXE take each as a whole zoom step, and can the patch sum the deltas into whole notches in the
window messages, without a code write?

### 11.1 The path of a wheel message [C: read-only headless Ghidra decompile of build 4390913, 2026-11-24 15:55-16:10 EST, impl-5042]

- **The main loop** (0x81DAE0, the app's run loop; caller 0x602380): each pass drains the whole queue with
  `PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)` and, for each message, `TranslateAcceleratorW` (when the app has an
  accelerator table), `IsDialogMessageA` over the app's registered dialog windows, then `TranslateMessage` and
  `DispatchMessageW`; only `WM_MOUSEMOVE` is coalesced (a second `PeekMessageW` without `PM_REMOVE` skips a mouse
  move followed by another). When the queue is empty the app's tick (vtable +0xC) runs one frame. Both `PeekMessageW`
  and `DispatchMessageW` are called through the EXE's import slots (`call [slot]`, the slot read at each call). So the
  wheel messages of one frame are all handled, one after another, before the next frame, and none is merged.
- **The window procedure** of `WinClass_FXS` (registered in 0x81DD30, `lpfnWndProc` 0x81DFF0) calls 0x81DF00 (which
  handles `WM_ACTIVATE` and the IME message 0x281 itself), then 0x81B4B0, which passes every other message to the app
  object's virtual handler (vtable +0x10); `DefWindowProcW` only when that returns 0. The game view state's mouse entry
  0x501770 is GVS vtable slot 5 (referenced from 0xA2091C, the GVS vtable being 0xA20908) and sends messages
  0x200..0x20E to the world view's input handler 0x609D80 as (message, wParam, lParam) `[C]`; that the app's handler
  reaches it synchronously, inside `DispatchMessageW`, is `[I]` from this chain and is what 11.3's timing tests.
- **The world view's wheel branch** (in 0x609D80): in Strategic View with the per-view bit 0x40000 set it calls
  0x5807C0 (in when `wParam >= 0`, else out); otherwise it calls the zoom-in command 0x5426B0 when `(int)wParam >= 0`
  and the zoom-out command 0x583620 otherwise (each calls the current camera mode's handler from a table at
  0x2A102C8 + mode * 0x600). **Only the sign of wParam is read**: the delta's size is ignored, a delta of 0 counts as
  "in", and a message of 15 zooms exactly as far as one of 120.
- **The scroll panel's wheel handler** (0x6B49C0; that it is the control behind Lua's `ScrollPanel` is `[I]`, from
  its scroll value and three child controls): it hit-tests the panel
  and its scroll bars against the cursor and moves the scroll value by the panel's fixed step, down when
  `(short)HIWORD(wParam) < 1`, up otherwise: sign only as well. A UI control that takes the wheel ends its path there;
  the world view gets the message only when no control under the cursor took it.
- A debug or free camera handler (0x643070) does scale by the delta (`delta * constant` added to a float, clamped);
  it is not the map's camera path `[I: no caller found by reference, reached through a vtable]`.
- **Build 26577625 [C: byte read, 2026-11-24 16:20 EST]:** the main loop (181 instructions), the mouse entry (11), the
  world view's wheel branch (31) and the scroll panel's handler (259) are instruction-for-instruction the same in the
  golden copy of 26577625 as in 4390913 once addresses are masked (`llvm-objdump` of
  `F:/c5-64bit-work/golden/civ5-steam-build{4390913,26577625}-ws/CivilizationV_DX11.exe`; the 26577625 addresses stay
  local, D202). What is measured on 4390913 below holds for 26577625.

### 11.2 The instrument and the driver (ticket 5042; not product code)

- `perf/evidence/5042/wheel_probe.cpp` (`build.bat OUTDIR`, clang-cl x86; loaded with `bg_helper32.exe inject <pid>
  <dll>`): writes only the EXE's import slots of `USER32!PeekMessageW` and `USER32!DispatchMessageW` (D136/D228). It
  logs every `WM_MOUSEWHEEL` taken off the queue, times each dispatch (QueryPerformanceCounter around the original
  `DispatchMessageW`: the window procedure, the UI's hit tests and the zoom command), sums the dispatches of each drain
  and logs the frame intervals after it. Its prototype mode (`sum 1` in `wheel_cmd.txt`) is the proposed change: deltas
  are added up, a remainder of the other sign or older than 400 ms is dropped, a message that completes a notch passes
  with the whole notches as its delta, every other wheel message is turned into `WM_NULL` before the EXE sees it.
- `perf/evidence/5042/standin_smoke.py` checks the probe on the harness stand-in (hidden desktop, posted wheel
  messages; 2026-11-24 16:08 EST, ALL OK): summing off, all 5 messages reach the window unchanged; summing on, 8 x +15
  reach it as one +120, 4 x +30 then 4 x -30 as +120 and -120, one +1 as nothing, 60 + 30 + 40 as one +120, and
  60, 600 ms, 60 as nothing.
- `perf/evidence/5042/wheel_5042.py` is the laptop driver (a `realinput.py job` with one more op, `w5042`; the script
  `perf/evidence/5042/script_run.json` runs it unattended): real input through the HI real-input lane's one fenced
  `SendInput` site (`tools/hi/realinput.py`), zoom level and camera height read from GVS+0x140 / +0xB0 with
  `shell/tools/gt_reader.py`. Arms, each from fully zoomed out, one SendInput event per message: N1 one notch, N2 two
  notches 50 ms apart, D1 one message of +1, H2 2 x 60, H4 4 x 30, H8 8 x 15 (a high-resolution wheel's notch), T1 a
  trackpad-like flick of 10 messages adding to 120, T3 one of 16 messages adding to 360 (gaps 8 ms, a 125 Hz report
  rate); 3 repeats without and 2 with the prototype; then 5 bursts of 40 x +15 at the zoom-in limit (no camera change)
  for the per-message cost. `--lane posted` sends the same messages with `PostMessage` to the game's window instead.
  `perf/evidence/5042/report_5042.py` turns a run's `wheel_5042.json` into the table below.

### 11.3 Measured on the laptop `[L, laptop, 2026-11-24 17:09-17:12 EST, impl-5042]`

**Set-up.** Testbed job `20261124-170634-2e8e` (`wheel_5042.py job --ticket 5042 --load W5042-TINY_0008 --script
perf/evidence/5042/script_run.json`): instance `tb1`, the laptop's own Steam EXE (build 4390913, stamp 0x546CD0A8;
11.1 shows the wheel code is the same in 26577625), the stock route (tb1's own 32-bit DLL), mods cp,vp,eui, the Tiny
save REL548-TINY_0008 (SHA-256 b2ceb663..., turn 8) shipped as `W5042-TINY_0008`, the cursor at the client centre (UI
px 768, 443) over the map. Probe injected, 2 of 2 slots. Results: `perf/evidence/5042/results/`, table
`perf/evidence/5042/acceptance-posted.txt`; the probe's log stays in `C:/c5-64bit-work/5042/laptop-2e8e/` (not in git).

**The SendInput lane was refused** in this job and in `20261124-170024-363e` (17:03): `LockApp.exe` ran in the owner's
session 1 with the session's lock flag set, the input desktop "Default", the foreground window `ApplicationFrameHost`
(the lock screen's frame), so `tools/hi/realinput.py` refused before any input (COMMON 22; the MC's answer #13168: run
the posted lane, the SendInput series after the laptop is unlocked). **The numbers below are the posted lane**: the
same `WM_MOUSEWHEEL` messages put into the game's queue with `PostMessage`. The EXE's loop takes posted and input
messages through the same `PeekMessageW` and reads nothing but wParam (11.1), so the zoom steps and the dispatch costs
are the EXE's; what the posted lane does not show is how Windows turns a device's events into messages (11.5).

**Zoom table**: 20.5 (fully out), 18.5, 16.5, 14.5, 12.5, 11.0: five notch steps of 2.0 zoom levels, the last one cut
by the 11.0 limit (the load default 14.256 is off this grid because the camera starts there).

| Arm (messages, delta sum) | zoom steps, summing off (3 runs) | zoom steps, prototype on (2 runs) | messages dispatched off / on |
|---|---|---|---|
| N1 one notch (1, 120) | 1, 1, 1 | 1, 1 | 1 / 1 |
| N2 two notches 50 ms apart (2, 240) | 2, 2, 2 | 2, 2 | 2 / 2 |
| D1 one message of +1 (1, 1) | 1, 1, 1 | 0, 0 | 1 / 0 |
| H2 2 x 60 (2, 120) | 2, 2, 2 | 1, 1 | 2 / 1 |
| H4 4 x 30 (4, 120) | 4, 4, 4 | 1, 1 | 4 / 1 |
| H8 8 x 15, a high-resolution wheel's notch (8, 120) | 5+ (the whole range), x3 | 1, 1 | 8 / 1 |
| T1 trackpad-like flick (10, 120) | 5+, x3 | 1, 1 | 10 / 1 |
| T3 longer flick (16, 360) | 5+, x3 | 3, 3 | 16 / 3 |

So **every message is one full zoom step**: one high-resolution notch (8 x 15) or one trackpad flick worth one
notch zooms from fully out to fully in, and a single message of +1 zooms as far as a notch. *(Posted messages only. With
real input, 11.3b (2026-11-25), Windows merges the events of a flick into about one message per frame, so the same
notch zooms 3 steps at 30 fps, not 5+; every message is still one step.)* With the prototype the
small-delta arms zoom exactly as a notched wheel of the same total does, and the notched arms are unchanged. Every
message sent was taken off the queue once (TAKE lines) in 39 of the 40 arm runs; the 40th (D1, first run) and the
second of the 5 bursts lost their start mark to a race in the driver's command file (fixed after the run: the file is
now renamed into place), and the probe's log shows their messages taken and dispatched as in the others.

**Cost per message (FR4).** Each wheel message's whole handling runs inside `DispatchMessageW` (window procedure, the
UI's hit tests, the zoom command):

| What | n | median | mean | p90 | max |
|---|---|---|---|---|---|
| one dispatch that moves the zoom (us) | 151 | 171 | 306 | 698 | 1,135 |
| one dispatch at the zoom-in limit, no camera change (us) | 160 | 50 | 68 | 135 | 489 |
| a drain of 40 messages at the limit, all in one frame (us) | 4 | 2,592 | 2,702 | 3,209 | 3,209 |
| whole flick, T1 (10 messages, 3 or 4 drains) / T3 (16 messages, 4 drains), summed (ms) | 3+3 | 3.5 / 3.9 | | | 2.7-4.1 |
| frame interval without input / the 6 frames after a burst (ms) | 120 / 24 | 33.4 / 32.1 | 33.7 / 32.4 | 38.0 / 35.9 | 43.8 / 52.3 |

A flick's messages arrive over 72-120 ms, so the EXE handles 4-5 per frame, about 1 ms of a 33 ms frame; the frame
interval after a burst of 40 is no longer than without input (the testbed instance runs at the 30 fps cap, so the
work fits the cap's slack). **FR4's figure of about 12 ms per trackpad flick is not reproduced here**: 2.7-4.1 ms
per flick of 10-16 messages on the laptop's i7-7700HQ, Tiny map, most of it the zoom commands (a message at the limit
costs 50 us). With summing a flick costs one or three dispatches (0.4-0.6 ms each), so the saving is 2-3.5 ms per
flick, spread over 3-4 frames: a side effect, not a reason for the change. `[L, posted lane, one launch, workstation
numbers not taken]` *(2026-11-24 22:57: a second launch, 11.3a, gave the same zoom steps at 3-6x lower costs.)*

### 11.3a Second launch: SendInput refused again, the posted lane repeated `[L, laptop, 2026-11-24 22:50-22:58 EST, impl-5042, second lease]`

**Set-up.** After the MC's "laptop unlocked" (#13435), MC-B's window 22:50-23:20 (desk 0136 #13449). One headed
interactive job `20261124-225010-ec79` (`wheel_5042.py job --ticket 5042 --load W5042-TINY_0008 --idle 600`, no
`--script`; in game 115 s after the launch) on `tb1`, the same EXE (4390913, stamp 0x546CD0A8), route, mods, save
and cursor point as 11.3; a probe frame first (`live.py shot --method grab`, Visual Checks
`vc-20261124-225312-laptop-4180`: the game full-screen at turn 9, no lock screen visible), then the op `w5042 --lane
real`, then `--lane posted` in the same game, then `stop` (job rc 0, `restore_ok` true). Results:
`perf/evidence/5042/results/*-20261124-225010-ec79.json`, table `perf/evidence/5042/acceptance-posted-ec79.txt`.

**SendInput refused again (22:53)**, before any input: `realinput.py state` (read-only, 22:53 and 22:57) read
`LockApp.exe` pid 2004 in session 1, session flag locked, connect state 1 (WTSConnected), input desktop "Default",
the game the foreground window and not minimised, idle 4,893 s, screen saver off, display-off after 0 s on mains (never)
and 300 s on battery. So `lock_ui` is true and `refusal()` refuses, although a whole-screen grab shows the game: a
`LockApp.exe` resident in the session while the grab shows the desktop is the case realinput.py cannot tell apart
from a lock screen up `[I: the lock screen's curtain may be up on the physical display while the session's own
desktop still renders; not read]`. Not got round (COMMON 22); question #13453, the MC's answer #13460: the session is
still flagged locked, the owner is asked to unlock it, the SendInput series stays the follow-up. *(Corrects the
reading of 2026-11-24 22:3x that the laptop was unlocked, taken from 0685's runs of 21:23-21:40, which sent no real
input `[I]`.)* Before a real-input job, `live.py send JOB '{"op": "realinput", "args": ["state"]}'` in a live session (or
the same read in a short job) shows `lock_ui` in a second, before a series is planned around it.

**The posted lane, second launch (22:54-22:57)**: every arm's zoom steps, messages taken and messages dispatched are
**identical** to 11.3's first launch, off and on (the table is the same row for row; D1's first run now has its
start mark, the driver's rename fix holds: 40 of 40 arm runs complete). The costs were lower in this launch:

| What | launch 1 (17:09, 11.3) median / p90 | launch 2 (22:54) median / p90 | n (launch 2) |
|---|---|---|---|
| one dispatch that moves the zoom (us) | 171 / 698 | 31 / 117 | 152 |
| one dispatch at the zoom-in limit (us) | 50 / 135 | 18.5 / 27.6 | 200 |
| a drain of 40 messages at the limit (us) | 2,592 / 3,209 | 854 / 1,206 | 5 |
| frames after a burst / without input (ms) | 32.1 / 33.4 | 33.2 / 33.4 | 30 / 120 |

The 3-6x difference between the launches is not explained `[I: the laptop's power state or what else ran at 17:09
(0713's quiet series had just ended), neither read; both jobs were functional, not quiet, so neither is a timing
series by COMMON 23]`. Either way a 10-16 message flick costs at most 4 ms of handling over 3-4 frames, the frame
interval after a burst is the same as without input, and FR4's 12 ms is not reproduced. The go of 11.4 rests on the
zoom-step result, which both launches show identically.

### 11.3b The SendInput series: Windows merges a flick into one message per frame `[L, laptop, 2026-11-25 07:19-07:29 EST, impl-5042, 5042 v2]`

**Set-up.** MC-B's window 07:16-07:55 (desk 0136 #14371). One headed interactive job `20261125-071644-7c6c`
(`wheel_5042.py job --ticket 5042 --load W5042-TINY_0008 --idle 900`, no `--script`) on `tb1`, the same EXE (4390913,
stamp 0x546CD0A8), stock route, mods cp,vp,eui, save `W5042-TINY_0008` (turn 9), the game **windowed** this time (the
laptop's screen 1536 x 864 logical px), the cursor at the client centre (768, 443), the 30 fps cap (frames 33.3 ms).
Lock check first (07:19, `realinput state` in the job): `lock_ui` false, session flags 0, input desktop "Default",
`LockApp.exe` pid 2004 still resident with no window (0732's reading); probe frame Visual Checks
`vc-20261125-071934-laptop-97bd`. Then the op `w5042 --lane real` (07:19:54-07:23:11, 3 runs off, 2 on, 5 bursts; no
refusal) and `w5042 --lane posted` in the same game (07:25:37-07:28:54), then `stop` (rc 0, `restore_ok` true).
Results: `perf/evidence/5042/results/{real,posted}-20261125-071644-7c6c.json`, tables
`perf/evidence/5042/acceptance-{real,posted}-7c6c.txt` (with the taken deltas per run, `report_5042.py --probe-tsv`).

**Windows merges the wheel events of a flick.** With SendInput (one event per message, gaps as in 11.2) the EXE takes
fewer messages than events were sent, and their deltas are sums: H8 (8 x 15 over 56 ms) arrives as 3 messages, e.g.
45, 60, 15, taken about 33 ms apart, one per frame; T1 (10 events adding to 120 over 72 ms) as 3 or 4 (44, 61, 15);
T3 (16 events adding to 360 over 120 ms) as 4 or 5 (195, 114, 37, 14). In all 40 arm runs the taken deltas add up
exactly to the sum sent. The posted lane in the same game took one message per message posted (8 x 15, 10, 16), as in
11.3 and 11.3a. `[L]` for the counts and sums; `[I]` for the mechanism: the system adds a new wheel input event's
delta to a `WM_MOUSEWHEEL` of the same window that is still waiting in the queue, and the EXE drains its queue once
per frame (11.1), so it sees about one wheel message per frame while a flick lasts. Posted messages are not merged.

| Arm (events, delta sum, span) | messages taken, real (off runs) | zoom steps real, off / on | messages taken, posted | zoom steps posted, off / on |
|---|---|---|---|---|
| N1 (1, 120) | 1, 1, 1 | 1, 1, 1 / 1, 1 | 1 | 1, 1, 1 / 1, 1 |
| N2 (2, 240, 50 ms) | 2, 2, 2 | 2, 2, 2 / 2, 2 | 2 | 2, 2, 2 / 2, 2 |
| D1 (1, +1) | 1, 1, 1 | 1, 1, 1 / 0, 0 | 1 | 1, 1, 1 / 0, 0 |
| H2 (2 x 60, 8 ms) | 2, 1, 1 | 2, 1, 1 / 1, 1 | 2 | 2, 2, 2 / 1, 1 |
| H4 (4 x 30, 24 ms) | 2, 2, 2 | 2, 2, 2 / 1, 1 | 4 | 4, 4, 4 / 1, 1 |
| H8 (8 x 15, 56 ms) | 3, 3, 3 | 3, 3, 3 / 1, 1 | 8 | 5+, 5+, 5+ / 1, 1 |
| T1 (10, 120, 72 ms) | 3, 3, 3 | 3, 3, 3 / 1, 1 | 10 | 5+, 5+, 5+ / 1, 1 |
| T3 (16, 360, 120 ms) | 4, 5, 5 | 4, 5+, 5+ / 3, 3 | 16 | 5+, 5+, 5+ / 3, 3 |

So, with real input, **stock zooms one step per frame that a flick spans**, not one per event and not one per
notch: one high-resolution notch or a one-notch trackpad flick is 3 steps at 30 fps (1 is right), a three-notch flick
4-5 (3 is right). The error therefore grows with the frame rate `[I: at 60 fps about twice the messages for the same
flick; not measured]`, and a merged message carrying a notch or more counts as one step whatever it carries (T3's
first message of 195; the bursts' 240-345, at the limit so no steps shown). With the prototype the real arms zoom
exactly the notch total, as on the posted lane: H2-T1 one step, T3 three (it passed 120, 120, 120), N1 and N2
unchanged, D1 nothing. The go of 11.4 now rests on real input as well.

**Cost (real input, same launch).** One dispatch that moves the zoom: n 74, median 73 us, p90 136, max 198 (posted,
same game: n 152, median 39 us, p90 110); a flick is 3-5 dispatches, 0.2-0.4 ms in all, over 3-5 frames. The bursts at
the limit (40 events of +15 in one SendInput call) arrived as 2 or 3 messages each (e.g. 270, 255, 45, about 133 ms
apart), median 105 us each; their deltas add to 570 or 585 of the 600 sent `[I: the missing 15-30 not explained; not
read further]`. Frames after the bursts 33.3 ms median (n 67) against 33.3 without input (n 120). FR4's 12 ms per flick
is further from reproduced on real input: the merge already caps the EXE's work at one wheel message per frame.

### 11.4 Go: sum the deltas in the proxy's `PeekMessageW` import slot (no code write)

**Go `[L for the mechanism, the prototype on the laptop, posted lane (11.3, 11.3a) and real input (11.3b); I for the
product's details below]`.** Where it hooks: the
EXE's import slot of `USER32!PeekMessageW`, from the proxy, as `proxy/src/proxy_kbhook.cpp` already points two USER32
slots of the EXE at its own thunks (a data write found by name in the import directory, D228; no EXE code byte). The
main loop (0x81DAE0) takes every message through that slot with `PM_REMOVE`, so the thunk sees each wheel message
before `TranslateMessage`, the UI and the world view. The behaviour proven by the prototype (11.2, 11.3):

- add each `WM_MOUSEWHEEL` delta to a remainder; drop the remainder when the sign changes or after 400 ms without a
  wheel message; when the remainder reaches a whole notch, let the message through with the whole notches as its delta
  and keep the rest; otherwise turn the message into `WM_NULL` (the loop dispatches it, nothing happens);
- a notched mouse is unchanged: its messages are whole notches with an empty remainder (N1, N2 identical on and off);
- the change covers every consumer of the wheel, the world view and the UI's scroll panels alike, which are sign-only
  too (11.1), so lists scroll a notch at a time on a trackpad as well; Lua input handlers see notch-sized deltas, as
  from a notched mouse.

For the Extras ticket (its own key in `[Extras]`, a bugfix whose default is the owner's; `docs/extras/flags-and-messages-design.md`):
(1) a message carrying two or more notches still moves one step in stock; with real input Windows merges the events
of one frame into one message (11.3b: 195, and 240-345 in the bursts), so a fast notched spin or a quick trackpad
flick does carry several notches in one message. The thunk should hand the extra notches out as further messages on
the next `PeekMessageW` calls (a pending count, no `PostMessage`), which also fixes that stock behaviour *(2026-11-25:
"may" became "should" after 11.3b's real-input series)*; (1a) the step count of a flick then no longer depends on the
frame rate (11.3b); (2) only the main loop's thread, only `PM_REMOVE`; the
mouse-move peek without `PM_REMOVE` is left alone; (3) clear the remainder on `WM_ACTIVATE` and focus loss; (4) the
thunk is installed at the proxy's boot, so the front end before the DLL loads keeps stock behaviour `[I]`; (5)
`WM_MOUSEHWHEEL` (0x20E) is not used by the world view (11.1) and needs nothing. Alternatives also without a code
write, not preferred: the `DispatchMessageW` slot (after `TranslateAccelerator` and `IsDialogMessage`, which ignore the
wheel, so it would work too) or a window subclass with `SetWindowLongPtrW` (another owner of the window procedure the
proxy would have to keep in order).

### 11.6 The product: `[Extras] FixSmoothWheel` (ticket 0755, 2026-11-25)

**What was built `[C: code and offline tests, 2026-11-25, impl-0755, branch impl-0755]`.** `proxy/src/proxy_wheel.{h,cpp}`,
option id 37 (`[Extras] FixSmoothWheel`, `kCatBugfix`, default 1 as the ticket proposes; the owner confirms it at the
merge: `kApprovedDefaults` in `options/tests/test_options.cpp`). Items (1)-(5) of 11.4 as built:

- **Where:** the EXE's import slot of `USER32!PeekMessageW`, found by name in the import directory (the walk of
  `proxy_kbhook.cpp`), one 4-byte data write (D228), only on a known build (`exebuild/include/vp_exe_build.h`: time stamp
  and image size both match); unknown build, no import or an unbound slot: off with one line (`LOUD:` when the build is
  known). Installed by `ProxyWheel::Install` from `proxy_rt.cpp`'s `DllGetGameContext` right after
  `ProxyShell::OnBoot` (mode=on only; the patch switched off never installs it); the slot given back by
  compare-and-swap in `DetachOnUnload` with the counters line `detach: wheel: ...`.
- **Which calls:** the main loop's only: the thread that booted the proxy (the EXE's main thread, the same premise as
  `proxy_kbhook.h` `[I]`), `hWnd` NULL, no message range, `PM_REMOVE` set. Every other call (another thread, a filter,
  `PM_NOREMOVE`: the loop's mouse-move peek) is forwarded unchanged and counted.
- **Summing (the core, `ProxyWheel::Take`):** a remainder of the other sign, or one older than 400 ms (`> 400`,
  GetTickCount at the take), is dropped first; a delta of 0 changes nothing; whole notches leave the remainder
  (truncated toward zero, so the rest keeps its sign). 0 notches: the message becomes `WM_NULL` (message, wParam, lParam
  0; hwnd, time, pt kept). 1 or more: the message passes with a delta of one notch, the key-state word kept; so a
  notched wheel's +-120 with an empty remainder is byte-identical to stock.
- **Merged notches (item 1):** the further notches of a message (e.g. 11.3b's 195, or a 240) are a pending count and a
  copy of the message as it passed; each of the main loop's next calls returns one before anything is taken off the
  queue (so they keep the queue's order, before a later `WM_QUIT`), at most 32 per message (the rest counted as
  `capped`). No `PostMessage`. The step count of a flick therefore no longer depends on the frame rate (item 1a).
- **Focus (item 3):** `WM_ACTIVATE` and the focus messages are sent, never returned by `PeekMessageW`, so the thunk
  compares, at each main-loop call while it holds a remainder or pending notches (at most 400 ms after a partial
  notch), whether the foreground window belongs to the main thread with what it was when that state was formed; a
  change (the game gained or lost the foreground) drops both. A remainder formed while another program is in front
  (Windows' "scroll inactive windows") is kept while that stays so. An activation round trip shorter than the time
  between two main-loop calls is not seen `[I]`; it leaves at most a part of a notch.
- **Not touched:** `WM_MOUSEHWHEEL` (item 5).
- **Off** (`FixSmoothWheel = 0` or `EnableBugfixes = 0`): one line `wheel: off (...)`, the slot never written.

**Offline proof `[C, 2026-11-25 12:0x EST, impl-0755]`:** `python -I proxy/tests/wheel/test_wheel.py --scratch <dir>`:
`wheel_test` 118/118 (the proxy's toolchain and MSVC x86 ASan with its canary caught; `--expect-fail` exits 1 with one
FAIL line) over a fake image of both builds and a scripted queue: 11.3's and 11.3b's arms as sent and as Windows merged
them (N1, N2, D1, H2, H4, H8 = 45/60/15, T1 = 44/61/15, T3 = 195/114/37/14 gives 1,1,0,1 = 3 steps, the bursts
270/255/45 = 2,2,0), the sign change, 400 ms kept and 401 dropped, the tick wrap, a notched message byte for byte,
merged 360 = three of 120 from one queue read, 32767 = 32 handed out and 241 capped, the forwarded calls untouched,
the foreground rules, the 10 s counters line, the unload and its compare-and-swap, every refusal. `test_options`
1097/1097 (x86 and x64 trees), `test_options_schema` 25 OK (the installer page's row: a bugfix checkbox under
`EnableBugfixes`, ticked).

**Live on the product path `[L, laptop, 2026-11-25 13:43-14:1x EST, impl-0755, MC-B's grant #14858/#14945]`.** Two
headed jobs, one per key value, `proxy/tests/wheel/live/laptop0755.sh` (each submitted after the one before ended):
`wheel_0755.py` = product_path.prep on `tb1` (the laptop's own EXE 4390913; the D28 layout with this branch's
server.exe, x64 DLL b84aa3e9... (the pins-0.1b deps DLL), proxy 76fc7b1c..., composite 502a7e89...; the owner's
`[Renderer]` set of capture_job; `purity.tsv` derived for b84aa3e9 laid beside the x64 DLL: `purity: loaded (pack=0)
... 140 method keys, 29 Lua names; bound to the x64 DLL b84aa3e90f51...`), save `W5042-TINY_0008` (REL548-TINY_0008,
b2ceb663...), then 5042's driver `run --sum-repeats 0` (its probe only a pass-through logger above the thunk, summing
off) by SendInput (real lane) and by PostMessage (posted lane), 3 runs an arm; the safe stop; restore by hash
(`backup_restore_ok` true, `provision.py verify` rc 0).

| Arm (events, delta sum) | stock (11.3b: real / posted) | key 1, real (3 runs) | key 1, posted (3 runs) | key 0, real | key 0, posted |
|---|---|---|---|---|---|
| N1 (1, 120) | 1 / 1 | 1, 1, 1 | 1, 1, 1 | 1, 1, 1 | 1, 1, 1 |
| N2 (2, 240) | 2 / 2 | 2, 2, 2 | 2, 2, 2 | 2, 2, 2 | 2, 2, 2 |
| D1 (1, +1) | 1 / 1 | 0, 0, 0 | 0, 0, 0 | 1, 1, 1 | 1, 1, 1 |
| H2 (2 x 60) | 1-2 / 2 | 1, 1, 1 | 1, 1, 1 | 1, 1, 1 | 2, 2, 2 |
| H4 (4 x 30) | 2 / 4 | 1, 1, 1 | 1, 1, 1 | 2, 1, 2 | 4, 4, 4 |
| H8 (8 x 15, a high-resolution notch) | 3 / 5+ | 1, 1, 1 | 1, 1, 1 | 3, 3, 2 | 5+ x3 (8 messages) |
| T1 (10, 120, a one-notch flick) | 3 / 5+ | 1, 1, 1 | 1, 1, 1 | 3, 3, 4 | 5+ x3 (10 messages) |
| T3 (16, 360, a fast three-notch flick) | 4-5+ / 5+ | 3, 3, 3 | 3, 3, 3 | 5+ x3 (6, 5, 5 messages) | 5+ x3 (16 messages) |

Key 1 (job `20261125-134227-54eb`, rc 0): **every arm zooms exactly its notch total on both lanes**, the notched arms
as stock. The proxy's lines: `wheel: [Extras] FixSmoothWheel: ... active (slot 0x01336A00, main loop thread 14676)`
and the counters on the same thread id as the boot (the premise of 11.6 holds: the boot thread is the main loop's),
at the end `wheel messages 1126: passed 886, held 240; merged messages 13, further notches handed out 13, capped 0;
remainders dropped: turned 0, older than 400 ms 11, foreground change 0; other calls forwarded 809` (the 809: the
loop's mouse-move peeks and other filtered calls). The 13 merged messages are real-lane flicks Windows merged past a
notch (11.3b), each handed out as its notch count. The bursts at the zoom-in limit (40 x 15 = 600 in one SendInput
call) reached the EXE as 4 (real) or 5 (posted) single notches. One dispatch that moves the zoom: real median 90 us
(n 30), posted 82 us.

Key 0 (job `20261125-135018-8292`, rc 0): `wheel: off (FixSmoothWheel = 0): every wheel message stays one step, as
stock`, no slot written, and **the stock counts of 11.3b** (one step per message the EXE takes; 5+ = the whole range
from fully out): real H8 3, 3, 2, T1 3, 3, 4, T3 5+; posted one message and one step per message sent; the bursts
reached the EXE as 3 merged messages (real) or 40 (posted). Results fetched to `F:/c5-64bit-work/0755/laptop/<job>/`
(`out/w0755.json`, `out/w0755/{real,posted}/wheel_5042.json`, the probe's log, vpproxy.log, server.log; not in git).
Trackpad and high-resolution wheel arms are synthetic SendInput events (11.5), not a physical device.

### 11.5 Not verified

- *(Done 2026-11-25, 11.3b: the SendInput series ran after the lock check read `lock_ui` false; it showed that
  Windows does merge the wheel events of a flick, one message per frame, which replaces the guess here that it never
  merges.)*
- The bursts' missing 15-30 of 600 on real input (11.3b); the step count at frame rates other than the 30 fps cap.
- Why the per-message costs differ 3-6x between the two launches (11.3a).
- A real trackpad or high-resolution wheel: nobody operates the laptop's touchpad; T1 and T3 are synthetic flicks.
- Build 26577625 live (the code is the same, 11.1); the workstation, the huge map, the product path (the proxy route).
- 11.6 (ticket 0755): *(done 2026-11-25 13:43-13:58: the product thunk live on the laptop, key on and off, and the boot
  thread is the main loop's.)* Still open: build 26577625 live with the thunk, a physical trackpad, frame rates other
  than the 30 fps cap, and a proxy unload and reload in one process (a mod change) with another module's hook written
  on top of the slot: the compare-and-swap then leaves the slot alone, and that module would call into the unloaded
  thunk `[I]`; no module of the product does this (only 5042's probe, a test instrument).

## 12. Hidden world anchors: every WorldAnchor is laid out and projected every frame (ticket 5041, FR1 of the third speedup list, 2026-11-25)

The question (the Architect's T11, `research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`,
entry FR1): interface elements pinned to a place in the world (`<WorldAnchor>` controls: unit flags, city banners) are
projected to the screen every frame, also when they are hidden; how many hidden ones does a late VP + EUI game carry,
and what do they cost in frame time? A fix would be a code write, so it could only be an extra behind "Full EXE
Optimizations" (D228).

### 12.1 What the EXE does [C: read-only headless Ghidra decompile of build 4390913, 2026-11-25 10:55-11:00 EST, 5041-impl]

- **The anchor's layout slot** (vtable slot 4, offset +0x10; WorldAnchor's is 0x78AB00, the data word at 0xA54D98):
  calls the projection 0x522910 first, clears a few layout flags, then the generic layout 0x521890 (which calls slot 4
  of every child control, then folds the children's rectangles into its own), then folds its children's bounds once
  more. **No test of the control's hidden bit anywhere on this path.**
- **The projection** 0x522910 (1,250 bytes): in the map view, one `D3DXVec3Project` per view copy (g_NumViews, 1 or 2
  with the wrap) of the anchor's world position (`this + 0x9C`, three floats), with the view globals of section 4. If
  the projected rectangle (offsets at `this + 0x8C..0x98`) is inside the backbuffer: flag 0x2000 cleared, the position
  rounded (`floor`/`ceil`) and set (0x717730), and the depth worked out by `D3DXVec3Transform` and two
  `D3DXVec4Transform`; else flag 0x2000 ("off screen") set and the rounded position set. In Strategic View (view mode
  2 at GVS+0x758) it projects through 0x4F9E00 instead and calls no `D3DXVec3Project`. The one `D3DXVec3Project` call
  site's return address is 0x522B8B (the call goes through the import thunk 0x8A8734).
- **SetWorldPosition** (Lua `SetWorldPositionVal`, 0x57F210): stores the position at `this + 0x9C..0xA4`, then projects
  at once.
- **Hidden** = bit 0x2 of the flags dword at `this + 0x40`: Lua `SetHide` (0x75F870, through vtable slot 20, +0x50; the
  base implementation 0x65F2F0 sets or clears the bit) and `IsHidden` (0x4770B0, reads `flags >> 1 & 1`). The parent
  pointer is at `this + 0x38` (`ChangeParent`'s implementation 0x40E2D0 removes the control from it first) [C, offset
  read from that function only].
- **Drawing and hit tests do skip**: WorldAnchor's slots 5 to 9 (0x735330, 0x69DE20, 0x513720, 0x5535A0, 0x69C440)
  return at once when the UI manager's "hide world anchors" bit (bit 0 of `[0x2DD1D1C] + 0x6EC`, set by the Lua
  `UIManager:SetHideWorldAnchors`, 0x48FDF0 / 0x64E4B0) or the anchor's off-screen flag 0x2000 is set. So the
  per-frame waste is the layout and projection of hidden anchors, not their drawing.
- **Build 26577625** [C, `perf/evidence/5041/eqcheck.py`, 2026-11-25 10:58]: the five functions above (projection,
  WorldAnchor layout slot, generic layout, SetWorldPosition, SetHide) are carried to the new build with high
  confidence and are byte-equal outside the relocation and branch masks (`perf/evidence/5041/eqcheck.txt`; its negative
  run, one byte flipped, fails the first function). The live numbers below, taken on 4390913, hold for 26577625 [I].

### 12.2 Why the interface makes them [C: the installed Lua, read 2026-11-25]

Both unit-flag files make one flag (a `WorldAnchor` with its child controls) for **every unit of every player** and
hide the ones the active team cannot see: stock (`Assets/DLC/Expansion2/UI/InGame/UnitFlagManager.lua`, `OnUnitCreated`
for every unit, the hide from the fog state) and EUI as VP ships it (`MODS/(3a) VP - EUI Compatibility Files/LUA/
UnitFlagManager.lua`, `CreateNewFlag` for every unit of every player, `flag.Anchor:SetHide(isHiddenByFog or
isInvisibleToActiveTeam)`; spare flags are parked under `g_ScrapControls`). City banners exist for revealed cities. So
hidden anchors are not a property of an exotic mod: they grow with the number of units on the map, and every VP game
has them. Without EUI the count would be about the same [I, not measured].

### 12.3 The instrument (ticket 5041; not product code)

`perf/evidence/5041/anchor_probe.cpp` (`build.bat`; injected with `bg_helper32 inject`): writes two import slots of the
EXE only (`d3dx9_42!D3DXVec3Project`, `user32!PeekMessageW`; no instruction byte, D136/D228). Frames are counted from
`PeekMessageW` drain to drain (as 5042). Each `D3DXVec3Project` call is counted by its return address; calls from
0x522B8B are classified by the anchor (`input - 0x9C`): shown, hidden itself (flag bit 0x2), or under a hidden ancestor
(the parent chain at +0x38). v2 charges the time from the end of one anchor's call to the start of the next to the first
anchor's class (the rest of its projection and its children's layout; gaps above 50 us are counted apart) and has the
A/B switch `skip 1`: a hidden anchor's call is not passed on and returns a point far off screen, so the EXE takes its
off-screen branch (flag 0x2000 set, not drawn; it is hidden anyway). This is the in-process prototype of the FR1 fix,
done through the import slot. v3 watches every anchor that was hidden in the previous frame and is shown now (the
skip check). The driver `anchor_5041.py` (a `live_session.py` job with the op `a5041`; `--vsync-off` sets
`GraphicsSettingsDX11.ini WaitForVSync = 0` for the job and restores it by SHA-256), the script of the quiet job
`script_quiet.json`, the report `report_5041.py`. The probe's own work (classification, timing) adds about 0.4 ms to
each frame in both arms alike; the costs below exclude it except the "span".

### 12.4 The count `[L, oldpc job 20261125-110240-4a78, 2026-11-25 11:05-11:07 EST, 5041-impl]`

Build 4390913 (oldpc has no 26577625; byte-equal code, 12.1), the stock EXE path (no proxy, no server), the H save
`TS0438-H-T300` (Huge Continental Drift, turn 300, CP + VP + EUI, SHA-256 `308a96bb...`), the harness frame cap (30 fps),
window 1914 x 1051. Lua at the load: 1,616 units of 32 players, 214 on plots the active team sees, 30 invisible to it;
176 cities, all revealed, 23 visible.

| Window | Frames | `D3DXVec3Project` calls per frame (all from 0x522B8B) | shown | hidden itself | under a hidden parent | time inside the calls per frame |
|---|---|---|---|---|---|---|
| rest, 10 s | 318 | 2,932 (min = max) | 1,436 | 1,446 | 50 | 424 us |
| 6 drag-pans, 1 s each | 389 | 2,932 (min = max in each) | 1,436 | 1,446 | 50 | 411-420 us |
| rest after the drags | 302 | 2,932 | 1,436 | 1,446 | 50 | 425 us |

**Every one of the 2,932 world anchors is projected in every frame, at rest as in a pan; 1,496 of them (51 %) are
hidden.** The count does not depend on the camera. The 1,436 "shown" ones include anchors that are off screen but not
hidden (they get flag 0x2000 and are not drawn).

### 12.5 The cost, smoke `[L, oldpc job 20261125-111247-bd64, 2026-11-25 11:15-11:17 EST, functional, not quiet]`

The same game setup, the cap set to 0 (no wait) but `WaitForVSync = 1`, so every frame is 16.67 ms (60 Hz): frame time
says nothing here; the cost per frame is read from the probe. Two repeats of off/on blocks, 3 drags each.

| Arm | Frames | `D3DXVec3Project` of the hidden anchors per frame | time after their calls (rest of the projection, children) | first-to-last anchor span per frame |
|---|---|---|---|---|
| off (as the EXE does) | 653 (drags) | 173 us | 103 us | 971 us |
| on (hidden not projected) | 641 (drags) | 40 us (the probe's write and timer only) | 125 us | 863 us |

The shown anchors cost 113-116 us inside the calls and 88-89 us after them in both arms. So all 2,932 anchors take about
0.45 ms of the EXE's frame on oldpc, the hidden half about 0.25 ms, and not projecting the hidden ones saves about
0.1 ms per frame: the off-screen branch they then take costs about 20 us more than the on-screen one, and the rest of
their layout stays. (The smoke's 60 Hz frames left the CPU idle between frames; the quiet series below is the number.)

### 12.6 The quiet A/B `[L, oldpc quiet job 20261125-112007-90df, 2026-11-25 11:20-11:32 EST, 5041-impl]`

Metric: frame time = `PeekMessageW` drain end to drain end on the EXE's main thread (the probe), over the frames of
drag-pans (posted left-button drags 40 % <-> 65 % of the client width at 55 % height, 1,000 ms, `tools/hi/actions.drag`).
Build 4390913, stock EXE path, H save `TS0438-H-T300` (CP + VP + EUI; statistics logging as the save's mod options,
not changed), window 1914 x 1051, the harness frame cap set to 0 and `WaitForVSync = 0` for the job (restored by
SHA-256, `restored_equal` true; the instance restored and verified, `restore_ok` true). One game, 10 repeats, each an
off block and an on block (order alternating per repeat), each block 1 s settle, 4 drags (1 s still between) and 5 s
at rest; every frame logged. Quiet place: alone, CPU mean 37.4 % (max 64.7 %), **disturbed**: foreign cores 0.161
against the limit 0.10 (svchost 41.6 s and Defender 28.9 s of CPU over the job, `results/90df-quiet-summary.json`);
free commit 8.5 GB at the launch. Report: `perf/evidence/5041/report-90df.txt` (`report_5041.py`).

| Arm | Drag frames | mean | sd | median | p95 | hidden anchors: inside D3DX / after the call, per frame | anchors' span per frame |
|---|---|---|---|---|---|---|---|
| off (projected, as the EXE does) | 12,490 | 5.865 ms | 0.668 | 5.686 | 7.123 | 112.6 / 86.2 us | 998 us |
| on (hidden not projected, skip) | 12,669 | 5.799 ms | 0.657 | 5.634 | 7.017 | 38.8 (the probe only) / 77.1 us | 920 us |

- **Paired by repeat** (the mean drag-frame time of each block): on - off = **-0.069 ms (-1.17 %)**, sd 0.205, **95 %
  interval [-0.215, +0.077] ms** ([-3.7, +1.3] %), n 10, smallest detectable difference about 0.13 ms. **Inside the
  noise.** The first two repeats swung by +-0.4 ms (the disturbance); without them (post hoc, an indication only)
  -0.085 ms, 95 % interval [-0.138, -0.032], n 8.
- **Why it moves, from the probe's in-frame clock** (no noise of the frame around it): skipping the hidden anchors'
  `D3DXVec3Project` removes about 74 us of call time per frame (112.6 -> 38.8; 38.8 us is the probe's own write and
  timer) and 9 us of what follows the calls (86.2 -> 77.1: their off-screen branch with rounding, not their children's
  layout, which stays). The anchors' span falls by 79 us (998 -> 920 us). So the fix FR1 describes saves **about
  0.08 ms per frame** on this save on oldpc: 1.4 % of a 5.9 ms uncapped frame, 0.5 % of a 60 Hz frame. All 2,932
  anchors cost about 0.38 ms per frame of the EXE's own time (calls plus after-call time, both classes); the hidden
  half about 0.20 ms.
- The probe's own work (classification and timers on every call) is in both arms; it inflates the span (998 us against
  about 0.38 ms of EXE work) and every frame alike.

### 12.7 The skip check `[L, oldpc job 20261125-113413-f328, 2026-11-25 11:37-11:41 EST, functional]`

MC #14761: does an anchor hidden while `skip 1` is on land at the right place on its first shown frame? The two AI turns
at the end of the quiet job brought 81 anchors from hidden to shown, none of them on screen (all their projections were
off the window), so a planned case was run: `skipcheck.sh` puts the camera on a fogged foreign land unit
(`skipcheck_find.lua`, `UI.LookAt`), waits 3 s, makes its plot visible to the active team (`skipcheck_vis.lua`,
`Plot:ChangeVisibilityCount(team, 1, -1, true, false)`, on the job's save copy only; the flag manager unhides the flag),
3 s, then -1. Probe WATCH lines (flags read at the call's entry, i.e. what the previous frame's layout left):

| Case | Anchors shown | Flags entering the first shown frame | Projection in that frame | Flags entering the next frame |
|---|---|---|---|---|
| skip 1 (unit at plot 104,28) | 3 (frame 686) | 0x2009: off screen (the skip's result while hidden) | 954.0, 436.3 (the window's centre, where the camera put the plot), passed on | 0x0009: off-screen flag cleared |
| skip 0 (unit at plot 94,16) | 1 (frame 2227) | 0x0009 (projected while hidden, on screen) | 955.0, 436.5, passed on | 0x0009 |

So with the skip the anchor's first shown frame is projected normally (a shown anchor is never skipped) and leaves that
frame's layout with the right position and the off-screen flag clear, the same as without the skip. The one difference:
it enters that frame marked off screen. Drawing tests the flag (12.1), so if the frame's drawing ran before its layout,
the flag would appear one frame later than without the skip; it never appears in a wrong place [C for the flag test, I
for the order of layout and drawing within a frame, not measured]. The rectangle at `this + 0x10..0x1C` stayed
(-32, -32, 80, 64) throughout: it is the anchor's local box, not its screen position (the position is written through
0x717730, not read here).

### 12.8 Verdict: NO-GO (2026-11-25, 5041-impl)

- Hidden anchors are many in VP + EUI (1,496 of 2,932 per frame on the H save; one per unit out of sight, in stock's
  flag file too), so the count did not stop the ticket.
- The frame-time difference of the drag-pan A/B is **inside the noise** (95 % interval -0.215 to +0.077 ms), which the
  acceptance names a no-go; and the probe's in-frame clock bounds the gain at **about 0.08 ms per frame** (1.4 % of an
  uncapped 5.9 ms frame, 0.5 % at 60 Hz) on oldpc's CPU. A code write behind "Full EXE Optimizations" (D228) is not
  worth that.
- An import-slot form (the probe's skip in a product thunk on the `D3DXVec3Project` slot, no code write) would have to
  read the hidden bit and walk the parent chain (5 levels here) for each of the 2,932 calls: tens of microseconds per
  frame, much of the 0.08 ms it saves [I, not measured without the probe's timers]. Not proposed either.
- The bigger share (about 0.38 ms of all anchors' layout per frame) is the layout pass itself; skipping a hidden
  anchor's whole layout (children included) would need a code write in the layout slot, and is bounded by the hidden
  half, about 0.20 ms per frame [measured as calls plus after-call time, 12.6].

### 12.9 Not verified

- Build 26577625 live (the code is byte-equal, 12.1); the product path (proxy, server, composite: the anchors are the
  EXE's own interface, so the absolute cost per frame is the same code; its share of the product's shorter EXE frame is
  larger [I]); the workstation's CPU; maps with more units (the cost grows with the unit count).
- An undisturbed repeat of the quiet series (this one ran with foreign cores 0.161 against 0.10).
- The order of layout and drawing within a frame (12.7).
- Without EUI (stock's flag file, 12.2): the count of hidden anchors.
