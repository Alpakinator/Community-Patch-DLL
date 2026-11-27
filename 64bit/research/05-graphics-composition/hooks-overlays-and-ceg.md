# Hooks, overlays and CEG

What can and cannot be hooked or patched in the shell, learned the hard way on 2026-11-09 (four crashes during the
live Gate 3 run) and from PR #13379's EXE-interop notes.

## 1. The Steam overlay recursion crash

**Symptom:** 3 crashes (stack overflow; dumps in `%LOCALAPPDATA%\CrashDumps`) when the game changed resolution with
the compositor's hooks active. (The fourth crash on the way to the working resolution change was the Lua-channel
deadlock in section 4.)

**Mechanism (GATES.md):**
1. A resolution change makes the game create a **new swap chain** (and window).
2. `gameoverlayrenderer.dll` (Steam overlay) re-hooks `Present` on the new chain and records whatever function it
   finds in the vtable slot as "the original". That function was **our** hook.
3. Our hook chains to whoever re-pointed the slot (by design: "if somebody else re-points a slot the hook chains to
   the new target").
4. From then on: dxgi -> overlay -> our hook -> dxgi -> overlay -> ... recursed **122-178 deep** until the stack
   overflowed.

**Control run:** with the compositor's hooks off, the same resolution change survived. So the overlay alone is fine;
the interaction is the bug.

**Fix (applied live, commits e1934c319 / b9ffe3d99):**
- Hook `IDXGIFactory::CreateSwapChain` / `CreateSwapChainForHwnd`, and **step our swap-chain slots aside while the
  game creates a swap chain**, so the overlay records dxgi's own function as its original.
- Never chain to a foreign module that rewrites our slot.
- Step aside during the original `Present`.

With that, the resolution-change round trip (2560x1440 -> 1920x1080 -> back) passed.

(Superseded 2026-11-10 by issue 3006: the DLL now uses MinHook inline hooks and writes no vtable slot, so the
step-aside is gone; see section 2 for why the recursion cannot recur. Not yet verified live with the overlay.)

Note: the stand-in work had listed "coexistence with the Steam overlay or RenderDoc" as untested; the nofocus
README notes the overlay is "only injected into Steam-launched games".

## 2. The general rule for a product (GATES condition 2)

"Vtable patching fights inline hookers (Steam overlay; likely also RTSS, Discord, capture tools)." The
CreateSwapChain step-aside fixed the one case seen. For a product:

- use **inline hooks** (MinHook v1.3.4 source is at `%USERPROFILE%\tools\minhook-v1.3.4`, commit
  c3fcafdc10146beb5919319d0683e44e3c30d537; not built yet), **or**
- put the composite into the **EXE's own render path via the DLL** instead of D3D hooks.

REVIEW.md section 4 lists this as a structural limit that is "Known work, not a research question".

Inline hooks here mean hooks in system modules (d3d11/dxgi/user32), as the focus guard already does in user32;
they are not a licence to patch the EXE (next section).

**Done on the stand-in (2026-11-10/11, issue 3006; not yet verified live):** `shell_composite.dll` no longer writes any
vtable. MinHook v1.3.4 (source copied to `shell/third_party/minhook/`, commit c3fcafdc1, BSD-2-Clause, licence and
NOTICE kept) is built from source by `shell\build.cmd` (x86 static, `hde32` only) and linked into the DLL. What
was learned [L stand-in, RTX 4080, Windows 11 26100, d3d11.dll 10.0.26100, 2026-11-10 23:45-00:00]:

- **Targets come from the vtables of real objects** (`shell/composite/vtable_index.c` gives the slots; the vtables are
  only read). Discovery: a dummy device + blit and flip swap chains + `CreateDXGIFactory` factory + a deferred context
  at load; then at adoption the host's own device, immediate context and a deferred context created on the host's
  device; then every context the host creates (CreateDeferredContext hook). 18 distinct targets on the stand-in.
- **Immediate and deferred contexts have different implementation functions** for `OMSetRenderTargets`,
  `ClearRenderTargetView`, `ClearDepthStencilView` and `ExecuteCommandList`; `FinishCommandList` is one function.
  **The device's feature level changes them too:** a feature-level-10.0 device uses other `OMSetRenderTargets` and
  `ExecuteCommandList` implementations than an 11.x device (probe, `d3d11.dll+0x1B2AB0` vs `+0x1946F0`). So the DLL
  hooks each distinct target once, up to 4 per method, each with its own trampoline. `CreateDeferredContext1` and
  `CreateDeferredContext2` are one function; all four CreateDeferredContext variants share a signature and one hook.
  A `D3D11_CREATE_DEVICE_SINGLETHREADED` device cannot create deferred contexts (the probe crashed on the null
  context), irrelevant for the game, which records on deferred contexts.
- **Device and swap-chain vtables live in the modules** (`d3d11.dll`, `dxgi.dll`); only the context vtable is
  per-object heap memory. Without our patches the immediate context's `ClearRenderTargetView` slot did not change
  across 6 Presents and ExecuteCommandLists (probe): the old "runtime rewrites the slot every Present" was the runtime
  restoring its own value over a patched slot. With inline hooks this is moot: the slot keeps pointing at d3d11's
  function, whose prologue jumps to our detour.
- **Inline hooks also see the runtime's own internal calls**: `ExecuteCommandList(list, FALSE)` resets the immediate
  context's state by calling the `OMSetRenderTargets` implementation (one null bind per executed list in the trace).
  Our handlers ignore them (no back buffer, no deferred context), but every hook body must be cheap and re-entrant.
- **Only `d3d11.dll` and `dxgi.dll` are hooked.** A target in any other module (a wrapper such as RenderDoc's, an
  overlay that replaced the vtable entry), in our own module or in the EXE image is refused and logged (D4).
- **Enabling costs a thread freeze**: each `MH_EnableHook` suspends every other thread; ~100 ms each on this busy
  machine (a system-wide thread snapshot). The DLL queues the enables and applies them in one `MH_ApplyQueued`:
  18 targets in 156 ms at load; at adoption in the game expect one such freeze if the game's device has
  implementations the dummy did not show. *Live 2026-11-11 (3008) [L]: the game's device (FL 11.0) used the same 18 targets as the
  dummy device ("18 targets, 0 changed" at adoption in all three launches), so there was no freeze at adoption; the one
  freeze is the load-time commit, 124.8 ms for 18 hooks at DllGetGameContext (main menu).*
  *Under load (ticket 0494, 2026-11-17, stand-in, workstation beside 6 heavy jobs at about 80 % CPU) [L stand-in]:
  `MH_ApplyQueued` for the 18 load-time hooks took **9,069.6 ms** (`hooks committed (dummy device): 18 enabled in
  9069.6 ms`), and the host's frame that loaded the DLL took 8,926 ms; minutes later on the same machine 165.5 ms
  (6,547 ms on 2026-11-12, verification 9038 V1). So on a loaded machine the commit can hold the loading thread for
  seconds; in the game that is the thread at DllGetGameContext [I: not measured live under load]. Why it takes that
  long (the system-wide thread snapshot, or suspending threads that wait for a core) was not measured [I]. The stand-in
  tests that count host time from the host's start now wait for the commit line (shell/README.md, "Timing checks").*
  *Later the same day, same series, 8 to 11 heavy jobs at 74-86 % CPU [L stand-in]: load-time commits of 5,984.6,
  9,745.2 and 10,667.1 ms; and the lockstep UpdateViews hook's own enable (`lsprobe: hook ENABLED`, a second
  `MH_ApplyQueued` from the control thread) came 5.4 s after its `hook created` line, the host drawing 122 frames in
  those 5.4 s with one frame of 5,137 ms: the freeze holds every other thread of the process, so in the game a slow
  enable would freeze the game once at that moment [I: not measured live under load].*
- **Kill switches**: `hook_present`, `hook_resize`, `hook_create_swapchain`, `hook_create_deferred`, `hook_clear`,
  `hook_clear_dsv`, `hook_omset`, `hook_finish`, `hook_exec` in the ini, applied live with
  `MH_QueueDisableHook/EnableHook` + `MH_ApplyQueued`; `mode=off` disables every hook. The test
  `shell/tests/test_tagged.py::test_kill_switches_and_mode_off` reads the first 5 bytes of every target from the host
  process: with a switch off they equal the original prologue (`8B FF 55 8B EC` on every target here), with it on they
  differ, and only that switch's targets change; with mode=off all 18 are original and no stats are produced.
  Trampolines and detours are never freed (the DLL pins itself), so a thread inside a detour while its hook is
  disabled finishes normally.

**Why the Steam-overlay recursion cannot recur with inline hooks [I, not verified live: no overlay on the stand-in].**
The recursion needed our hook to find "the original" by reading the vtable slot at call time and to chain to whoever
had re-pointed it. A MinHook detour never reads a vtable: it calls its own trampoline, which holds the copied first
instructions of the d3d11/dxgi function and jumps back into its body. Cases: (a) the overlay hooks the vtable after
us: the slot still holds dxgi's function (we never changed it), the overlay records that as its original, and the call
goes overlay -> dxgi entry -> our detour -> our trampoline -> dxgi body; (b) the overlay inline-hooks the same
prologue after us: its trampoline contains our jump, so overlay -> our detour -> our trampoline -> dxgi; (c) it hooked
before us: MinHook relocates its jump into our trampoline, so us -> overlay -> dxgi. No path re-enters a hook from its
own trampoline. The step-aside of the swap-chain slots during CreateSwapChain is therefore removed. One residual
case: disabling our hook (kill switch) after another hooker patched over our prologue makes MinHook write the original
bytes back over the other hooker's jump, silently unhooking it; to be checked live with the overlay.

## 3. CEG: no code-byte patches in the EXE

*Note 2026-11-19 (ticket 0582): this section describes build 4390913. Firaxis' build 26577625 (2026-11-19) has no CEG
at all and nothing in its place (`../02-exe-internals/new-exe-build-26577625.md` B.3, static analysis): the reason below no longer applies
to that build; whether `.text`/`.rdata` writes are then used is the owner's decision (D200), and a live check is still to run.*

- PR #13379's EXE-interop notes (LoneGazebo/Community-Patch-DLL, open, by vaderkos) report that **Steam's CEG hashes
  ~80% of `.text` and two thirds of `.rdata`** in the loaded image, and that a mismatch **crashes the game later, far
  from the change**.
- Consequence (REVIEW.md section 4): "everything must be data patches, import slots and vtables". Every instrument
  built so far follows that:
  - `MemoryImports`, `EngineAllocTrace`: chained thunks in the EXE's msvcr90 import slots;
  - `EngineQueueGuard`: the EXE's `kernel32!InterlockedExchangeAdd` import slot;
  - the camera read path: swap a heap object's vtable pointer (CAMERA.md);
  - the River/Arrows stub: one heap write to a render mask (STUBS.md);
  - the focus guard: inline hooks in the process' copy of **user32**, "never the CEG-protected EXE image".
- A second reason against an inline hook at an EXE function entry (alloc-trace-README): a 5-byte jump over a
  function whose first instruction is 1 byte (`push esi` at 0x82c090) cannot be written atomically for a thread
  between the first and second instruction.
- Patched-byte checks: the instruments verify the EXE build by PE TimeDateStamp (DX11 0x546CD0A8, DX9 0x546CCB59,
  Tablet 0x546CD5F8) and byte patterns before touching any slot, and leave other builds alone.

## 4. The Lua-channel deadlock (CommitResolutionOptions)

- To drive the game's own resolution change, `OptionsManager.CommitResolutionOptions()` was first called through
  the DLL's Lua channel. **That hung the game for good**: the channel runs chunks on the game-core thread while it
  holds the engine lock; the call does `SetWindowPos`, which waits on the window's (main) thread, which was waiting
  on that same lock. Diagnosed with cdb `!cs` (`!cs -o` per the civ5-game-ui memory note). The only way out was to
  kill the game.
- Not a compositor bug. Rule: window-touching engine calls must run **on the main thread** - register a one-shot
  `Events.LocalMachineAppUpdate` handler from the channel and make the call there. That is how the successful round
  trip was done ("via OptionsManager on the main thread").
- Side effect: it persisted 1920x1080 and MSAA into `GraphicsSettingsDX11.ini`; back that file up first (it was
  restored at the end of the session).

## 5. The 2 s retarget gap (GATES condition 3)

After a resolution change the game presents on a new swap chain; the compositor only adopts it after the old
target has been silent for 2 s, so the world shows **black for two seconds**. Fix proposed: adopt the new swap chain
directly from the CreateSwapChain hook. The server side is not the bottleneck: it delivered the new size in 0.25 s.

**Implemented 2026-11-10 (issue 3006) [L stand-in; not yet verified live]:** the `CreateSwapChain` /
`CreateSwapChainForHwnd` hooks mark the new swap chain, and it becomes the target at its own first Present (decided
on the render thread, so the creating thread touches no state). `adopt_on_create=1` (default) / `0` (old rule).
`test_host --recreate-at 4` destroys its window and swap chain and creates new ones;
`test_tagged.py::test_swap_chain_adopted_at_creation` (asserts < 0.3 s resp. 1.8-3 s): with 1 the new chain was adopted
in the same millisecond as the host's recreation log line (00:01:16.531, its first Present) and the following stats show
0 missed insertions; with 0 the old rule switched 1.984 s later (2026-11-11 00:01).

**Live in the game 2026-11-11 (issue 3008, c5h-b, D33, huge save) [L]:** three windowed resolution changes
(2554x1411 -> 1914x1051 -> 130x10 (a 0x0 "back" target, see below) -> 2554x1411) through `OptionsManager` on the main
thread: each `CreateSwapChain` was followed by adoption at the new chain's first Present 35-67 ms later, the previous
server texture drawn (stretched) from 55-93 ms, the right-size server texture from 311-349 ms; one missed insertion per
change (the first frame's lists were recorded before adoption). **The 2 s black gap is gone.** The game reused its
window (same hwnd) for the new swap chain. No `ResizeBuffers` call was seen. Pitfall: this instance's
`GraphicsSettingsDX11.ini` holds windowed resolution 0x0 (= desktop size), so "restore the cached value" set 0x0 and
the game made a 130x10 swap chain; the round trip was finished with an explicit 2560x1440. `GraphicsSettingsDX11.ini`
was restored by hash afterwards.

## 6. Not tested (GATES condition 4 and compositor-README limitations)

| Case | Why not | Notes from the sources |
|---|---|---|
| Exclusive fullscreen | would take the foreground; `civ_bg launch` refuses `FullScreen != 0` | code path `SetFullscreenState -> WM_SIZE -> ResizeBuffers` uses the release/re-acquire path the resize tests passed; host supports it (`--allow-fullscreen`, F2) |
| Alt-tab with the real focus | only minimise/restore without activation was possible without stealing the owner's focus | |
| Multi-monitor moves | not run | a cloaked window stays invisible wherever it lands (nofocus README) |
| A second GPU / hybrid laptop | not run | server logs a LUID MISMATCH; start it with `--luid` = the LUID the DLL logs |
| Drawing with the game's real camera matrices | server draws a test scene | matrices readable by static analysis (CAMERA.md) |
| D3D debug layer, RenderDoc alongside | not run | do not combine RenderDoc and the helper in one session |
| `ResizeBuffers1`, `ClearView`, `OMSetRenderTargetsAndUnorderedAccessViews`, `ResolveSubresource` insertion; UAV / stream-out state restore | not implemented | still not implemented after 3006: a back-buffer bind through `OMSetRenderTargetsAndUnorderedAccessViews` is not tagged |
| Inline hooks (3006) next to the Steam overlay, RenderDoc, other hookers | stand-in only; live 2026-11-11 (3008) only next to the harness's focus guard (user32/win32u inline hooks): no conflict in three launches; the Goldberg test instances have no Steam overlay. **Steam overlay, thin client (no compositor), issue 0323 [L, 2026-11-15, laptop's own Steam install, `installer-notes.md` 10.3]:** in a game Steam itself started, the overlay is injected into the EXE (`GameOverlayRenderer.dll`) **and into `server.exe`** (`GameOverlayRenderer64.dll`; Steam's `logs\gameoverlay_renderer.txt`: "Current process: server.exe ... GameID = 8930"), and `gameoverlayui64.exe` connects to the EXE's pid. 10 turns, save, load and exit ran with both injected, no crash, no FALLBACK, no D125 report. **Shift+Tab opened and closed the overlay once at the main menu (mods active) and once in game**: the overlay was drawn, and after it closed the picture was normal; no crash, no D125 report (`installer-notes.md` 10.4; DIAGNOSTIC pictures in the laptop's Visual Checks store, issue 0323). This is the live overlay check that 0031 waits for, **for the thin client only** | RenderDoc's wrapper objects would put targets outside d3d11/dxgi: refused and logged, so expect no composite under RenderDoc. The compositor (`ServerRenderer = 1`) next to the Steam overlay is still not run |

Sources: `research/sources/shell-gates/GATES.md` (Gate 3: bugs found and fixed live, verdict conditions 2-4,
Gate 1 instrument notes), `compositor-README.md` (Composite DLL, limitations), `alloc-trace-README.md` ("Why not the
inline hook"), `nofocus-README.md` (guard design, residual case 7), `TOOLING.md` (MinHook),
`research/sources/memory-notes/civ5-game-ui-skill.md` lesson 12, `research/sources/REVIEW.md` section 4.
